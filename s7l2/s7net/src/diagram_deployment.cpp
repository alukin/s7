#include "s7/diagram_deployment.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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
    return json::object{
        {"stream_id", wire::to_string(stream.stream_id)},
        {"producer", endpoint_to_json(stream.producer)},
        {"consumers", std::move(consumers)},
        {"data_type", std::string(signal_type_name(stream.data_type))},
        {"max_elements_per_packet", stream.max_elements_per_packet},
        {"max_batch_latency_ms", stream.max_batch_latency_ms},
        {"send_log", json::object{{"max_packets", stream.send_log_max_packets}, {"max_block_ms", stream.max_block_ms}}},
        {"max_wait_ms", stream.max_wait_ms},
    };
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
            it->second.data_type != old.data_type || used_ids.contains(wire::to_string(old.stream_id))) {
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
            elements.push_back(json::object{
                {"id", stream_element_id(stream, "out")},
                {"type", "NetSink"},
                {"params", json::object{{"stream_id", wire::to_string(stream.stream_id)},
                                        {"type", std::string(signal_type_name(stream.data_type))},
                                        {"max_elements_per_packet", stream.max_elements_per_packet},
                                        {"max_batch_latency_ms", stream.max_batch_latency_ms},
                                        {"send_log_max_packets", stream.send_log_max_packets},
                                        {"max_block_ms", stream.max_block_ms}}},
            });
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
            elements.push_back(json::object{
                {"id", stream_element_id(stream, "in")},
                {"type", "NetSource"},
                {"params", json::object{{"stream_id", wire::to_string(stream.stream_id)},
                                        {"type", std::string(signal_type_name(stream.data_type))},
                                        {"max_wait_ms", stream.max_wait_ms}}},
            });
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
    return fragment;
}

auto producer_endpoints(const json::object& diagram, const std::string_view node_uuid) -> std::vector<std::string>
{
    std::unordered_map<std::string, std::string> endpoints;
    for (const auto& node : parse_runtime_nodes(diagram)) {
        endpoints[node.node_uuid] = node.endpoint;
    }
    std::vector<std::string> result;
    for (const auto& stream : parse_streams(diagram)) {
        const bool consumes = std::ranges::any_of(stream.consumers, [&](const auto& c) { return c.node_uuid == node_uuid; });
        if (!consumes || stream.producer.node_uuid == node_uuid) {
            continue;
        }
        const auto it = endpoints.find(stream.producer.node_uuid);
        if (it == endpoints.end() || it->second.empty()) {
            throw std::invalid_argument("Stream " + wire::to_string(stream.stream_id) + ": producer node " + stream.producer.node_uuid +
                                        " has no endpoint in 'runtime_nodes'.");
        }
        if (std::ranges::find(result, it->second) == result.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

} // namespace s7::deployment
