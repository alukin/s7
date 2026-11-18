// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/diagram_deployment.hpp"

#include <algorithm>
#include <numeric>
#include <queue>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include "s7/schema_library.hpp"
#include "s7/validation.hpp"
#include "s7/resampler.hpp"

namespace s7::deployment {

namespace json = boost::json;

namespace {

auto string_field(const json::object& object, const char* key, const std::string& what) -> std::string
{
    const auto* value = object.if_contains(key);
    if (!value || !value->is_string()) {
        throw std::invalid_argument(what + ": '" + key + "' must be a string.");
    }
    return std::string(value->as_string());
}

auto index_field(const json::object& object, const char* key, const std::string& what) -> std::size_t
{
    const auto* value = object.if_contains(key);
    if (!value || !(value->is_int64() || value->is_uint64()) || (value->is_int64() && value->as_int64() < 0)) {
        throw std::invalid_argument(what + ": '" + key + "' must be a non-negative integer.");
    }
    return static_cast<std::size_t>(value->to_number<std::uint64_t>());
}

auto u32_field(const json::object& object, const char* key, const std::uint32_t fallback) -> std::uint32_t
{
    const auto* value = object.if_contains(key);
    if (!value) {
        return fallback;
    }
    if (!(value->is_int64() || value->is_uint64()) || (value->is_int64() && value->as_int64() <= 0)) {
        throw std::invalid_argument(std::string("stream: '") + key + "' must be a positive integer.");
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(value->to_number<std::uint64_t>(), UINT32_MAX));
}

auto endpoint_from_json(const json::value& value, const std::string& what) -> StreamEndpoint
{
    if (!value.is_object()) {
        throw std::invalid_argument(what + " must be an object.");
    }
    const auto& object = value.as_object();
    return StreamEndpoint{string_field(object, "node_uuid", what), string_field(object, "element", what),
                          index_field(object, "port", what)};
}

auto endpoint_to_json(const StreamEndpoint& endpoint) -> json::object
{
    return json::object{{"node_uuid", endpoint.node_uuid}, {"element", endpoint.element}, {"port", endpoint.port}};
}

struct Connection {
    std::string from_element;
    std::size_t from_port{};
    std::string to_element;
    std::size_t to_port{};
};

auto connections_of(const json::object& schema) -> std::vector<Connection>
{
    std::vector<Connection> result;
    if (const auto* connections = schema.if_contains("connections")) {
        if (!connections->is_array()) {
            throw std::invalid_argument("schema: 'connections' must be an array.");
        }
        for (const auto& value : connections->as_array()) {
            if (!value.is_object()) {
                throw std::invalid_argument("schema: a connection must be an object.");
            }
            const auto& c = value.as_object();
            result.push_back(Connection{string_field(c, "from_element", "connection"), index_field(c, "from_port", "connection"),
                                        string_field(c, "to_element", "connection"), index_field(c, "to_port", "connection")});
        }
    }
    return result;
}

auto elements_of(const json::object& schema) -> const json::array&
{
    static const json::array empty;
    const auto* elements = schema.if_contains("elements");
    if (!elements) {
        return empty;
    }
    if (!elements->is_array()) {
        throw std::invalid_argument("schema: 'elements' must be an array.");
    }
    return elements->as_array();
}

// element id -> node_uuid ("" when unplaced)
auto placement_of(const json::object& schema) -> std::unordered_map<std::string, std::string>
{
    std::unordered_map<std::string, std::string> placement;
    for (const auto& value : elements_of(schema)) {
        if (!value.is_object()) {
            throw std::invalid_argument("schema: an element must be an object.");
        }
        const auto& element = value.as_object();
        const auto id = string_field(element, "id", "element");
        const auto* node = element.if_contains("node");
        placement[id] = node && node->is_string() ? std::string(node->as_string()) : std::string{};
    }
    return placement;
}

auto connection_name(const Connection& c) -> std::string
{
    return c.from_element + ":" + std::to_string(c.from_port) + " -> " + c.to_element + ":" + std::to_string(c.to_port);
}

auto stream_element_id(const StreamDescriptor& stream, const char* end) -> std::string
{
    return "__stream_" + wire::to_string(stream.stream_id) + "_" + end;
}

auto connection_json(const std::string& from, const std::size_t from_port, const std::string& to, const std::size_t to_port)
    -> json::object
{
    return json::object{{"from_element", from}, {"from_port", from_port}, {"to_element", to}, {"to_port", to_port}};
}

// A stream end element with the stream's timing parameters.
auto with_timing(json::object element, const StreamDescriptor& stream) -> json::object
{
    auto& params = element["params"].as_object();
    if (stream.sample_rate) {
        params["sample_rate"] = json::array{stream.sample_rate->numerator, stream.sample_rate->denominator};
    }
    if (stream.max_latency_ms && element.at("type") == "NetSource") {
        params["max_latency_ms"] = *stream.max_latency_ms;
    }
    return element;
}

auto timestamps_param(const StreamDescriptor& stream) -> json::array
{
    json::array roles;
    for (const auto& declaration : stream.timestamps) {
        roles.emplace_back(wire::to_string(declaration.role));
    }
    return roles;
}

// Each node can check the distributed serial-order/stream wait graph without
// loading plugins that exist only on other nodes. Its compiler separately
// verifies membership and real local dependencies.
void validate_schedule(const json::object& diagram)
{
    const auto& schedule = diagram.at("causal_schedule");
    if (!schedule.is_object()) throw std::invalid_argument("causal_schedule must be an object.");
    const auto& fields = schedule.as_object();
    const auto* version = fields.if_contains("version");
    if (!version || !(version->is_int64() || version->is_uint64()) || version->to_number<std::int64_t>() != 1)
        throw std::invalid_argument("Unsupported causal_schedule version.");
    const auto* nodes = fields.if_contains("nodes");
    if (!nodes || !nodes->is_object()) throw std::invalid_argument("causal_schedule.nodes must be an object.");
    const auto placed = placed_nodes(diagram);
    if (nodes->as_object().size() != placed.size()) throw std::invalid_argument("causal_schedule node membership mismatch.");
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::size_t> indexes;
    std::vector<std::vector<std::size_t>> next;
    std::vector<std::size_t> degree;
    const auto add_edge = [&](const Key& from, const Key& to) {
        if (!indexes.contains(from) || !indexes.contains(to)) throw std::invalid_argument("causal_schedule is missing a stream endpoint.");
        next[indexes.at(from)].push_back(indexes.at(to)); ++degree[indexes.at(to)];
    };
    for (const auto& node : placed) {
        const auto* order = nodes->as_object().if_contains(node);
        if (!order || !order->is_array()) throw std::invalid_argument("causal_schedule has no order for node " + node);
        std::optional<Key> previous;
        for (const auto& id : order->as_array()) {
            if (!id.is_string()) throw std::invalid_argument("causal_schedule element IDs must be strings.");
            Key key{node, std::string(id.as_string())};
            if (!indexes.emplace(key, indexes.size()).second) throw std::invalid_argument("Duplicate causal_schedule element: " + key.second);
            next.emplace_back(); degree.push_back(0);
            if (previous) add_edge(*previous, key);
            previous = std::move(key);
        }
    }
    for (const auto& stream : parse_streams(diagram)) {
        std::set<std::string> consumers;
        for (const auto& c : stream.consumers) consumers.insert(c.node_uuid);
        for (const auto& node : consumers)
            add_edge({stream.producer.node_uuid, stream_element_id(stream, "out")}, {node, stream_element_id(stream, "in")});
    }
    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < degree.size(); ++i) if (!degree[i]) ready.push(i);
    std::size_t count = 0;
    while (!ready.empty()) {
        const auto n = ready.front(); ready.pop(); ++count;
        for (const auto child : next[n]) if (--degree[child] == 0) ready.push(child);
    }
    if (count != degree.size()) throw std::invalid_argument("causal_schedule introduces a cross-node wait cycle.");
}

} // namespace

auto parse_runtime_nodes(const json::object& diagram) -> std::vector<RuntimeNode>
{
    std::vector<RuntimeNode> nodes;
    if (const auto* list = diagram.if_contains("runtime_nodes")) {
        if (!list->is_array()) {
            throw std::invalid_argument("'runtime_nodes' must be an array.");
        }
        for (const auto& value : list->as_array()) {
            if (!value.is_object()) {
                throw std::invalid_argument("runtime_nodes: an entry must be an object.");
            }
            const auto& node = value.as_object();
            RuntimeNode entry{string_field(node, "node_uuid", "runtime node"), {}, {}};
            if (const auto* name = node.if_contains("name"); name && name->is_string()) {
                entry.name = std::string(name->as_string());
            }
            if (const auto* endpoint = node.if_contains("endpoint"); endpoint && endpoint->is_string()) {
                entry.endpoint = std::string(endpoint->as_string());
            }
            if (node.contains("data_endpoint") || node.contains("data_security")) {
                entry.data_endpoint = string_field(node, "data_endpoint", "runtime node");
                entry.data_security = string_field(node, "data_security", "runtime node");
                const auto url = transport::parse_ws_url(entry.data_endpoint);
                if (entry.data_security != "mtls" && entry.data_security != "trusted_plaintext") {
                    throw std::invalid_argument("runtime node data_security must be 'mtls' or 'trusted_plaintext'.");
                }
                if (url.secure != (entry.data_security == "mtls")) {
                    throw std::invalid_argument("runtime node data_endpoint scheme conflicts with data_security.");
                }
            }
            nodes.push_back(std::move(entry));
        }
    }
    return nodes;
}

auto parse_streams(const json::object& diagram) -> std::vector<StreamDescriptor>
{
    std::vector<StreamDescriptor> streams;
    const auto* list = diagram.if_contains("streams");
    if (!list) {
        return streams;
    }
    if (!list->is_array()) {
        throw std::invalid_argument("'streams' must be an array.");
    }
    for (const auto& value : list->as_array()) {
        if (!value.is_object()) {
            throw std::invalid_argument("streams: an entry must be an object.");
        }
        const auto& object = value.as_object();
        StreamDescriptor stream;
        stream.stream_id = wire::parse_stream_id(string_field(object, "stream_id", "stream"));
        const auto what = "stream " + wire::to_string(stream.stream_id);
        stream.producer = endpoint_from_json(object.contains("producer") ? object.at("producer") : json::value{}, what + " producer");
        const auto* consumers = object.if_contains("consumers");
        if (!consumers || !consumers->is_array() || consumers->as_array().empty()) {
            throw std::invalid_argument(what + ": 'consumers' must be a non-empty array.");
        }
        for (const auto& consumer : consumers->as_array()) {
            stream.consumers.push_back(endpoint_from_json(consumer, what + " consumer"));
        }
        stream.data_type = parse_signal_type(string_field(object, "data_type", what));
        stream.max_elements_per_packet = u32_field(object, "max_elements_per_packet", stream.max_elements_per_packet);
        stream.max_batch_latency_ms = u32_field(object, "max_batch_latency_ms", stream.max_batch_latency_ms);
        if (const auto* log = object.if_contains("send_log")) {
            if (!log->is_object()) {
                throw std::invalid_argument(what + ": 'send_log' must be an object.");
            }
            stream.send_log_max_packets = u32_field(log->as_object(), "max_packets", stream.send_log_max_packets);
            stream.max_block_ms = u32_field(log->as_object(), "max_block_ms", stream.max_block_ms);
        }
        stream.max_wait_ms = u32_field(object, "max_wait_ms", stream.max_wait_ms);
        if (object.contains("max_latency_ms")) {
            stream.max_latency_ms = u32_field(object, "max_latency_ms", 0);
        }
        if (const auto* rate = object.if_contains("sample_rate")) {
            if (!rate->is_object()) {
                throw std::invalid_argument(what + ": 'sample_rate' must be an object.");
            }
            const auto& fields = rate->as_object();
            stream.sample_rate = stream::SampleRate{index_field(fields, "numerator", what + " sample_rate"),
                                                    index_field(fields, "denominator", what + " sample_rate")};
            if (stream.sample_rate->numerator == 0 || stream.sample_rate->denominator == 0) {
                throw std::invalid_argument(what + ": 'sample_rate' must be positive.");
            }
        }
        const auto* timestamps = object.if_contains("timestamps");
        if (!timestamps || !timestamps->is_array() || timestamps->as_array().empty()) {
            throw std::invalid_argument(what + ": 'timestamps' must declare at least one timestamp role.");
        }
        for (const auto& value : timestamps->as_array()) {
            if (!value.is_object()) {
                throw std::invalid_argument(what + ": a timestamp declaration must be an object.");
            }
            stream.timestamps.push_back(TimestampDeclaration{wire::parse_timestamp_role(string_field(value.as_object(), "role", what)),
                                                             string_field(value.as_object(), "clock_domain", what)});
        }
        if (!std::ranges::is_sorted(stream.timestamps, {}, &TimestampDeclaration::role) ||
            std::ranges::adjacent_find(stream.timestamps, {}, &TimestampDeclaration::role) != stream.timestamps.end()) {
            throw std::invalid_argument(what + ": timestamp roles must be distinct and in ascending role order.");
        }
        streams.push_back(std::move(stream));
    }
    return streams;
}

auto to_json(const StreamDescriptor& stream) -> json::object
{
    json::array consumers;
    for (const auto& consumer : stream.consumers) {
        consumers.push_back(endpoint_to_json(consumer));
    }
    json::array timestamps;
    for (const auto& declaration : stream.timestamps) {
        timestamps.push_back(json::object{{"role", wire::to_string(declaration.role)}, {"clock_domain", declaration.clock_domain}});
    }
    json::object out{
        {"stream_id", wire::to_string(stream.stream_id)},
        {"producer", endpoint_to_json(stream.producer)},
        {"consumers", std::move(consumers)},
        {"data_type", std::string(signal_type_name(stream.data_type))},
        {"max_elements_per_packet", stream.max_elements_per_packet},
        {"max_batch_latency_ms", stream.max_batch_latency_ms},
        {"send_log", json::object{{"max_packets", stream.send_log_max_packets}, {"max_block_ms", stream.max_block_ms}}},
        {"max_wait_ms", stream.max_wait_ms},
    };
    if (stream.sample_rate) {
        out["sample_rate"] = json::object{{"numerator", stream.sample_rate->numerator}, {"denominator", stream.sample_rate->denominator}};
    }
    out["timestamps"] = std::move(timestamps);
    if (stream.max_latency_ms) {
        out["max_latency_ms"] = *stream.max_latency_ms;
    }
    return out;
}

auto model_time_step_ns(const json::object& diagram) -> std::optional<std::int64_t>
{
    const auto* dt = diagram.if_contains("dt");
    if (!dt || dt->is_null()) {
        return std::nullopt;
    }
    const double seconds = dt->is_double() ? dt->as_double() : dt->is_int64() ? static_cast<double>(dt->as_int64())
                                                         : dt->is_uint64() ? static_cast<double>(dt->as_uint64())
                                                                           : -1.0;
    const auto ns = seconds * 1e9;
    if (!(ns >= 1.0) || ns > 9e18) {
        throw std::invalid_argument("'dt' must be a positive number of seconds (at least 1 ns).");
    }
    return std::llround(ns);
}

auto model_time_rate(const json::object& diagram) -> std::optional<Rate>
{
    if (!model_time_step_ns(diagram)) { // validates dt as model_time_step_ns does
        return std::nullopt;
    }
    const auto& dt = diagram.at("dt");
    return Rate::from_seconds(dt.is_double() ? dt.as_double() : dt.to_number<double>());
}

auto stream_timing(const json::object& diagram) -> StreamTiming
{
    StreamTiming timing;
    if (const auto rate = model_time_rate(diagram)) {
        // 1 / dt elements per second, the same exact rate as the nodes' model
        // time (not 1e9 / a rounded ns step, which misstates e.g. 44.1 kHz).
        timing.sample_rate = stream::SampleRate{rate->numerator, rate->denominator};
        timing.timestamps.push_back(TimestampDeclaration{wire::TimestampRole::ModelTime, "model"});
    } else {
        timing.timestamps.push_back(TimestampDeclaration{wire::TimestampRole::ProcessingTime, "utc"});
    }
    return timing;
}

auto diagram_schema(const json::object& diagram) -> const json::object&
{
    if (const auto* schema = diagram.if_contains("schema")) {
        if (!schema->is_object()) {
            throw std::invalid_argument("'schema' must be an object.");
        }
        return schema->as_object();
    }
    return diagram;
}

auto placed_nodes(const json::object& diagram) -> std::vector<std::string>
{
    std::vector<std::string> nodes;
    for (const auto& value : elements_of(diagram_schema(diagram))) {
        if (const auto* node = value.is_object() ? value.as_object().if_contains("node") : nullptr; node && node->is_string()) {
            const std::string uuid(node->as_string());
            if (std::ranges::find(nodes, uuid) == nodes.end()) {
                nodes.push_back(uuid);
            }
        }
    }
    return nodes;
}

auto assign_streams(json::object& diagram, const OutputTypeLookup& output_type) -> std::vector<std::string>
{
    std::vector<std::string> errors;
    std::vector<StreamDescriptor> previous;
    try {
        static_cast<void>(model_time_step_ns(diagram));
    } catch (const std::exception& ex) {
        return {ex.what()};
    }
    std::unordered_map<std::string, std::string> placement;
    std::vector<Connection> connections;
    std::set<std::string> known_nodes;
    try {
        previous = parse_streams(diagram);
        const auto& schema = diagram_schema(diagram);
        placement = placement_of(schema);
        connections = connections_of(schema);
        for (const auto& node : parse_runtime_nodes(diagram)) {
            known_nodes.insert(node.node_uuid);
        }
    } catch (const std::exception& ex) {
        return {ex.what()};
    }

    for (const auto& [element, node] : placement) {
        if (node.empty()) {
            errors.push_back("Element '" + element + "' is not placed on a node.");
        } else if (!known_nodes.contains(node)) {
            errors.push_back("Element '" + element + "' is placed on node " + node + ", which is not in 'runtime_nodes'.");
        }
    }

    // One stream per producing output port with consumers on other nodes.
    std::map<std::pair<std::string, std::size_t>, StreamDescriptor> wanted;
    for (const auto& c : connections) {
        const auto from = placement.find(c.from_element);
        const auto to = placement.find(c.to_element);
        if (from == placement.end() || to == placement.end()) {
            errors.push_back("Connection " + connection_name(c) + " refers to an unknown element.");
            continue;
        }
        if (from->second == to->second) {
            continue;
        }
        auto& stream = wanted[{c.from_element, c.from_port}];
        if (stream.consumers.empty()) {
            stream.producer = StreamEndpoint{from->second, c.from_element, c.from_port};
            auto timing = stream_timing(diagram);
            stream.sample_rate = timing.sample_rate;
            stream.timestamps = std::move(timing.timestamps);
            if (const auto type = output_type(c.from_element, c.from_port)) {
                stream.data_type = *type;
            } else {
                errors.push_back("The type of " + c.from_element + ":" + std::to_string(c.from_port) + " is not known.");
            }
        }
        stream.consumers.push_back(StreamEndpoint{to->second, c.to_element, c.to_port});
    }
    if (!errors.empty()) {
        return errors;
    }

    std::vector<StreamDescriptor> streams;
    std::unordered_set<std::string> used_ids;
    // Streams that continue keep their place in the list.
    for (const auto& old : previous) {
        const auto it = wanted.find({old.producer.element, old.producer.port});
        if (it == wanted.end() || it->second.producer.node_uuid != old.producer.node_uuid ||
            it->second.data_type != old.data_type || it->second.sample_rate != old.sample_rate ||
            it->second.timestamps != old.timestamps || used_ids.contains(wire::to_string(old.stream_id))) {
            continue;
        }
        auto stream = old; // id and settings stay
        stream.consumers = std::move(it->second.consumers);
        used_ids.insert(wire::to_string(stream.stream_id));
        streams.push_back(std::move(stream));
        wanted.erase(it);
    }
    for (auto& [_, stream] : wanted) {
        stream.stream_id = wire::random_stream_id();
        streams.push_back(std::move(stream));
    }
    json::array list;
    for (const auto& stream : streams) {
        list.push_back(to_json(stream));
    }
    diagram["streams"] = std::move(list);
    return {};
}

auto extract_node_schema(const json::object& diagram, const std::string_view node_uuid) -> json::object
{
    const auto& schema = diagram_schema(diagram);
    const auto placement = placement_of(schema);
    const auto streams = parse_streams(diagram);
    const auto on_node = [&](const std::string& element) {
        const auto it = placement.find(element);
        return it != placement.end() && it->second == node_uuid;
    };

    json::object fragment;
    const auto* name = schema.if_contains("name");
    fragment["name"] = (name && name->is_string() ? std::string(name->as_string()) : std::string("diagram")) + "@" +
                       std::string(node_uuid);
    json::array elements;
    for (const auto& value : elements_of(schema)) {
        auto element = value.as_object();
        if (on_node(string_field(element, "id", "element"))) {
            element.erase("node");
            elements.push_back(std::move(element));
        }
    }
    json::array connections;

    // stream ends present on this node
    std::map<std::pair<std::string, std::size_t>, const StreamDescriptor*> produced;          // producer port -> stream
    std::map<std::tuple<std::string, std::size_t, std::string, std::size_t>, const StreamDescriptor*> consumed; // (from, port, to, port)
    for (const auto& stream : streams) {
        if (stream.producer.node_uuid == node_uuid) {
            if (!on_node(stream.producer.element)) {
                throw std::invalid_argument("Stream " + wire::to_string(stream.stream_id) + ": producer '" + stream.producer.element +
                                            "' is not placed on this node.");
            }
            produced[{stream.producer.element, stream.producer.port}] = &stream;
            json::array consumer_nodes;
            for (const auto& consumer : stream.consumers) {
                if (std::ranges::find(consumer_nodes, json::value(consumer.node_uuid)) == consumer_nodes.end()) {
                    consumer_nodes.emplace_back(consumer.node_uuid);
                }
            }
            elements.push_back(with_timing(json::object{
                {"id", stream_element_id(stream, "out")},
                {"type", "NetSink"},
                {"params", json::object{{"stream_id", wire::to_string(stream.stream_id)},
                                        {"type", std::string(signal_type_name(stream.data_type))},
                                        {"max_elements_per_packet", stream.max_elements_per_packet},
                                        {"max_batch_latency_ms", stream.max_batch_latency_ms},
                                        {"send_log_max_packets", stream.send_log_max_packets},
                                        {"max_block_ms", stream.max_block_ms},
                                        {"consumer_nodes", std::move(consumer_nodes)},
                                        {"timestamps", timestamps_param(stream)}}},
            }, stream));
            connections.push_back(connection_json(stream.producer.element, stream.producer.port, stream_element_id(stream, "out"), 0));
        }
        bool consumes = false;
        for (const auto& consumer : stream.consumers) {
            if (consumer.node_uuid != node_uuid) {
                continue;
            }
            if (!on_node(consumer.element)) {
                throw std::invalid_argument("Stream " + wire::to_string(stream.stream_id) + ": consumer '" + consumer.element +
                                            "' is not placed on this node.");
            }
            consumed[{stream.producer.element, stream.producer.port, consumer.element, consumer.port}] = &stream;
            connections.push_back(connection_json(stream_element_id(stream, "in"), 0, consumer.element, consumer.port));
            consumes = true;
        }
        if (consumes) {
            elements.push_back(with_timing(json::object{
                {"id", stream_element_id(stream, "in")},
                {"type", "NetSource"},
                {"params", json::object{{"stream_id", wire::to_string(stream.stream_id)},
                                        {"type", std::string(signal_type_name(stream.data_type))},
                                        {"max_wait_ms", stream.max_wait_ms},
                                        {"producer_node", stream.producer.node_uuid},
                                        {"timestamps", timestamps_param(stream)}}},
            }, stream));
        }
    }

    for (const auto& c : connections_of(schema)) {
        const bool from_here = on_node(c.from_element);
        const bool to_here = on_node(c.to_element);
        if (from_here && to_here) {
            connections.push_back(connection_json(c.from_element, c.from_port, c.to_element, c.to_port));
        } else if (from_here && !produced.contains({c.from_element, c.from_port})) {
            throw std::invalid_argument("Connection " + connection_name(c) + " leaves this node without a stream.");
        } else if (to_here && !consumed.contains({c.from_element, c.from_port, c.to_element, c.to_port})) {
            throw std::invalid_argument("Connection " + connection_name(c) + " enters this node without a stream.");
        }
    }
    fragment["elements"] = std::move(elements);
    fragment["connections"] = std::move(connections);
    if (const auto* schedule = diagram.if_contains("causal_schedule")) {
        validate_schedule(diagram);
        const auto& fields = schedule->as_object();
        const auto* nodes = fields.if_contains("nodes");
        if (!nodes || !nodes->is_object()) throw std::invalid_argument("causal_schedule.nodes must be an object.");
        const auto* order = nodes->as_object().if_contains(node_uuid);
        if (!order || !order->is_array()) throw std::invalid_argument("causal_schedule has no element order for node " + std::string(node_uuid));
        for (const auto& id : order->as_array())
            if (!id.is_string()) throw std::invalid_argument("causal_schedule element IDs must be strings.");
        fragment["causal_order"] = *order;
    }
    return fragment;
}

auto plan_causal_schedule(json::object& diagram, const ElementRegistry& registry) -> std::vector<std::string>
{
    try {
        auto candidate = diagram;
        candidate.erase("causal_schedule"); // derive afresh, never trust a stale caller-supplied plan
        auto definition = diagram_schema(candidate);
        if (!definition.contains("connections")) definition["connections"] = json::array{};
        auto schema = SchemaLibraryLoader::build_schema(definition, registry);
        const auto rate = model_time_rate(candidate);
        if (rate) schema.set_rate(*rate);
        auto whole = rate ? detail::check_schema(schema) : detail::check_schema_structure(schema);
        const auto streams = parse_streams(candidate);
        if (!whole.errors.empty()) {
            std::string context;
            for (const auto& stream : streams) context += (context.empty() ? " Cross-node streams: " : ", ") + wire::to_string(stream.stream_id);
            auto errors = whole.errors;
            for (auto& error : errors) error = "Diagram validation: " + error + context;
            return errors;
        }
        if (streams.empty()) {
            diagram = std::move(candidate);
            return {};
        }
        if (whole.domain_rates.size() > 1 || !whole.coupled_regions.empty())
            return {"Distributed static schedules currently require one rate domain and no coupled numerical regions."};
        std::optional<Rate> declared_rate;
        for (const auto& element : whole.elements) {
            const auto pinned = element->declared_rate();
            const auto* coupled = dynamic_cast<const CoupledIntegration*>(element.get());
            if ((pinned && declared_rate && *pinned != *declared_rate) ||
                dynamic_cast<const RateChangingElement*>(element.get()) || (coupled && coupled->coupled_method() != CoupledMethod::None))
                return {"Distributed static schedules currently require one rate domain and no coupled numerical regions."};
            if (pinned) declared_rate = pinned;
        }

        struct Task {
            std::string node, id;
            int priority{1};
            bool port{};
            std::size_t degree{};
            std::vector<std::size_t> followers;
        };
        std::vector<Task> tasks;
        std::map<std::pair<std::string, std::string>, std::size_t> indices;
        std::map<std::string, detail::CheckedSchema> parts;
        for (const auto& node : placed_nodes(candidate)) {
            auto part = SchemaLibraryLoader::build_schema(extract_node_schema(candidate, node), registry);
            if (rate) part.set_rate(*rate);
            auto checked = rate ? detail::check_schema(part) : detail::check_schema_structure(part);
            if (!checked.errors.empty()) return {"Node " + node + ": " + checked.errors.front()};
            for (std::size_t i = 0; i < checked.elements.size(); ++i) {
                const auto& element = checked.elements[i];
                if (element->has_causal_memory()) continue; // published at step entry; committed by NodeRuntime
                const auto& id = checked.flat->elements()[i].id;
                const auto type = element->type_name();
                const auto priority = type == "NetSink" || type == "NetFrameSink" ? 0 :
                                      type == "NetSource" || type == "NetFrameSource" ? 2 : 1;
                indices.emplace(std::pair{node, id}, tasks.size());
                tasks.push_back(Task{node, id, priority, dynamic_cast<const detail::SchemaPort*>(element.get()) != nullptr, 0, {}});
            }
            parts.emplace(node, std::move(checked));
        }
        const auto edge = [&](const std::pair<std::string, std::string>& from, const std::pair<std::string, std::string>& to) {
            const auto a = indices.find(from), b = indices.find(to);
            if (a != indices.end() && b != indices.end()) {
                tasks[a->second].followers.push_back(b->second);
                ++tasks[b->second].degree;
            }
        };
        for (const auto& [node, part] : parts)
            for (const auto& c : part.flat->connections()) edge({node, c.from_element}, {node, c.to_element});
        for (const auto& stream : streams) {
            std::set<std::string> consumers;
            for (const auto& consumer : stream.consumers) consumers.insert(consumer.node_uuid);
            for (const auto& consumer : consumers) {
                const auto from = std::pair{stream.producer.node_uuid, stream_element_id(stream, "out")};
                const auto to = std::pair{consumer, stream_element_id(stream, "in")};
                if (!indices.contains(from) || !indices.contains(to)) throw std::invalid_argument("Stream endpoints missing from causal plan.");
                edge(from, to);
            }
        }
        const auto later = [&tasks](std::size_t a, std::size_t b) {
            return std::tie(tasks[a].priority, tasks[a].node, tasks[a].id) > std::tie(tasks[b].priority, tasks[b].node, tasks[b].id);
        };
        std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(later)> ready(later);
        for (std::size_t i = 0; i < tasks.size(); ++i) if (tasks[i].degree == 0) ready.push(i);
        json::object orders;
        for (const auto& [node, part] : parts) { static_cast<void>(part); orders[node] = json::array{}; }
        std::size_t visited = 0;
        while (!ready.empty()) {
            const auto i = ready.top(); ready.pop(); ++visited;
            const auto& task = tasks[i];
            if (!task.port) orders.at(task.node).as_array().emplace_back(task.id);
            for (const auto child : task.followers) if (--tasks[child].degree == 0) ready.push(child);
        }
        if (visited != tasks.size()) return {"Cross-node stream precedence contains a direct-feedthrough cycle."};
        candidate["causal_schedule"] = json::object{{"version", 1}, {"nodes", std::move(orders)}};
        validate_schedule(candidate);
        diagram = std::move(candidate);
        return {};
    } catch (const std::exception& ex) {
        return {std::string("Cannot plan diagram: ") + ex.what()};
    }
}

auto producer_endpoints(const json::object& diagram, const std::string_view node_uuid) -> std::vector<transport::DataPeer>
{
    std::unordered_map<std::string, transport::DataPeer> endpoints;
    for (const auto& node : parse_runtime_nodes(diagram)) {
        endpoints[node.node_uuid] = {node.data_endpoint.empty() ? node.endpoint : node.data_endpoint,
                                     node.node_uuid, node.data_security == "trusted_plaintext"};
    }
    std::vector<transport::DataPeer> result;
    for (const auto& stream : parse_streams(diagram)) {
        const bool consumes = std::ranges::any_of(stream.consumers, [&](const auto& c) { return c.node_uuid == node_uuid; });
        if (!consumes || stream.producer.node_uuid == node_uuid) {
            continue;
        }
        const auto it = endpoints.find(stream.producer.node_uuid);
        if (it == endpoints.end() || it->second.url.empty()) {
            throw std::invalid_argument("Stream " + wire::to_string(stream.stream_id) + ": producer node " + stream.producer.node_uuid +
                                        " has no endpoint in 'runtime_nodes'.");
        }
        auto peer = it->second;
        if (std::ranges::find(result, peer) == result.end()) {
            result.push_back(std::move(peer));
        }
    }
    return result;
}

} // namespace s7::deployment
