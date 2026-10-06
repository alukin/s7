#include "s7/deployment_client.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <unordered_map>

#include "s7/base64url.hpp"
#include "s7/canonical_json.hpp"
#include "s7/schema_library.hpp"
#include "s7/type_inference.hpp"

namespace s7::deployment {

namespace json = boost::json;

namespace {

auto string_of(const json::object& object, const char* key) -> std::string
{
    const auto* value = object.if_contains(key);
    return value && value->is_string() ? std::string(value->as_string()) : std::string{};
}

auto describe(const json::object& response) -> std::string
{
    if (string_of(response, "type") == "ERROR") {
        return string_of(response, "code") + ": " + string_of(response, "message");
    }
    if (const auto* issues = response.if_contains("incompatibilities"); issues && issues->is_array()) {
        std::string text;
        for (const auto& issue : issues->as_array()) {
            if (issue.is_object()) {
                text += (text.empty() ? "" : "; ") + string_of(issue.as_object(), "code") + ": " + string_of(issue.as_object(), "message");
            }
        }
        if (!text.empty()) {
            return text;
        }
    }
    return json::serialize(response);
}

auto request_object(const std::string& type, const std::string& request_id) -> json::object
{
    return json::object{{"type", type}, {"request_id", request_id}, {"protocol_version", 1}};
}

// NODE_INFO.control_limits of a node; nodes that advertise none take
// configurations of up to 1 MiB directly (the MVP chunk size).
struct ControlLimits {
    std::uint64_t max_configuration_bytes{1024 * 1024};
    std::uint64_t max_chunk_bytes{1024 * 1024};
};

auto control_limits_of(const json::object& node_info) -> ControlLimits
{
    ControlLimits limits;
    if (const auto* advertised = node_info.if_contains("control_limits"); advertised && advertised->is_object()) {
        const auto& fields = advertised->as_object();
        auto read = [&](const char* key, std::uint64_t& into) {
            if (const auto* value = fields.if_contains(key); value && (value->is_uint64() || (value->is_int64() && value->as_int64() > 0))) {
                into = value->to_number<std::uint64_t>();
            }
        };
        read("max_configuration_bytes", limits.max_configuration_bytes);
        read("max_chunk_bytes", limits.max_chunk_bytes);
    }
    limits.max_chunk_bytes = std::clamp<std::uint64_t>(limits.max_chunk_bytes, 1, 1024 * 1024);
    return limits;
}

// Whether NODE_INFO lists the shared "utc" clock domain (its clock is
// synchronized within its bound).
auto has_utc_domain(const json::object& node_info) -> bool
{
    const auto* domains = node_info.if_contains("clock_domains");
    if (!domains || !domains->is_array()) {
        return false;
    }
    return std::ranges::any_of(domains->as_array(), [](const json::value& domain) {
        return domain.is_object() && string_of(domain.as_object(), "id") == "utc";
    });
}

// NODE_INFO.data_protocol of a node: packet versions and extensions ("0x8001").
struct DataProtocol {
    std::vector<std::uint64_t> packet_versions;
    std::vector<std::string> extensions;
};

auto data_protocol_of(const json::object& node_info) -> std::optional<DataProtocol>
{
    const auto* protocol = node_info.if_contains("data_protocol");
    if (!protocol || !protocol->is_object()) {
        return std::nullopt;
    }
    DataProtocol result;
    if (const auto* versions = protocol->as_object().if_contains("packet_versions"); versions && versions->is_array()) {
        for (const auto& version : versions->as_array()) {
            if (version.is_int64() || version.is_uint64()) {
                result.packet_versions.push_back(version.to_number<std::uint64_t>());
            }
        }
    }
    if (const auto* extensions = protocol->as_object().if_contains("extensions"); extensions && extensions->is_array()) {
        for (const auto& extension : extensions->as_array()) {
            if (extension.is_string()) {
                result.extensions.emplace_back(extension.as_string());
            }
        }
    }
    return result;
}

// Wire draft section 12.1 item 4: no in-band negotiation, so every stream's
// producer and consumers must share a packet version, and a stream with
// per-packet timestamps needs the TIMESTAMPS extension at both ends.
auto check_data_protocols(const json::object& diagram, const std::unordered_map<std::string, DataProtocol>& protocols)
    -> std::vector<std::string>
{
    std::vector<std::string> errors;
    const auto timestamps_extension = std::format("0x{:04X}", wire::kTimestampsExtension);
    for (const auto& stream : parse_streams(diagram)) {
        const auto& producer = protocols.at(stream.producer.node_uuid);
        const bool explicit_timestamps = !stream.sample_rate;
        for (const auto& consumer_end : stream.consumers) {
            const auto& consumer = protocols.at(consumer_end.node_uuid);
            const bool common_version = std::ranges::any_of(producer.packet_versions, [&](const auto version) {
                return std::ranges::find(consumer.packet_versions, version) != consumer.packet_versions.end();
            });
            if (!common_version) {
                errors.push_back("Stream " + wire::to_string(stream.stream_id) + ": nodes " + stream.producer.node_uuid + " and " +
                                 consumer_end.node_uuid + " share no data packet version.");
            }
        }
        if (explicit_timestamps) {
            std::vector<std::string> ends{stream.producer.node_uuid};
            for (const auto& consumer_end : stream.consumers) {
                ends.push_back(consumer_end.node_uuid);
            }
            for (const auto& node : ends) {
                const auto& extensions = protocols.at(node).extensions;
                if (std::ranges::find(extensions, timestamps_extension) == extensions.end()) {
                    errors.push_back("Stream " + wire::to_string(stream.stream_id) + " carries per-packet timestamps, which node " + node +
                                     " does not support (extension " + timestamps_extension + ").");
                }
            }
        }
    }
    std::ranges::sort(errors);
    errors.erase(std::ranges::unique(errors).begin(), errors.end());
    return errors;
}

} // namespace

auto infer_output_types(const json::object& diagram, const ElementRegistry& registry) -> OutputTypeLookup
{
    auto schema_json = diagram_schema(diagram);
    if (!schema_json.contains("connections")) {
        schema_json["connections"] = json::array{};
    }
    const auto schema = SchemaLibraryLoader::build_schema(schema_json, registry);
    auto resolved = TypeResolver::resolve(schema);
    if (!resolved.ok()) {
        throw std::invalid_argument("Diagram types do not resolve: " + resolved.errors.front());
    }
    auto types = std::make_shared<std::unordered_map<std::string, std::vector<SignalType>>>();
    for (std::size_t index = 0; index < schema.elements().size(); ++index) {
        auto& ports = (*types)[schema.elements()[index].id];
        for (const auto& port : resolved.factories[index]()->output_ports()) {
            ports.push_back(port.type);
        }
    }
    return [types](const std::string& element, const std::size_t port) -> std::optional<SignalType> {
        const auto it = types->find(element);
        if (it == types->end() || port >= it->second.size()) {
            return std::nullopt;
        }
        return it->second[port];
    };
}

auto BeastControlRequester::operator()(const RuntimeNode& node, const json::object& request) -> json::object
{
    auto& client = clients_[node.endpoint];
    if (!client) {
        auto endpoint = std::make_shared<transport::BeastRuntimeNetEndpoint>();
        endpoint->configure_client(node.endpoint, tls_);
        // Over TLS, the node's certificate must be the node the diagram names.
        if (endpoint->server_certificate() && !node.node_uuid.empty()) {
            const auto identity = transport::check_node_identity(endpoint->server_certificate(), node.node_uuid);
            if (identity != transport::NodeIdentity::Verified) {
                const auto presented = transport::node_uuid_of(*endpoint->server_certificate());
                throw std::runtime_error("The certificate of " + node.endpoint + " is " +
                                         (presented ? "node " + *presented : "not a node certificate") + ", not node " +
                                         node.node_uuid + ".");
            }
        }
        client = std::move(endpoint);
    }
    return parse_json_precise(client->request_control_json(json::serialize(request))).as_object();
}

auto deploy_diagram(json::object diagram, const OutputTypeLookup& output_types, const ControlRequester& request,
                    const DeployOptions& options) -> DeployResult
{
    DeployResult result;
    result.errors = assign_streams(diagram, output_types);
    if (!result.errors.empty()) {
        return result;
    }
    result.diagram = diagram;

    std::unordered_map<std::string, RuntimeNode> known;
    for (auto& node : parse_runtime_nodes(diagram)) {
        known.emplace(node.node_uuid, std::move(node));
    }
    std::vector<RuntimeNode> targets;
    for (const auto& uuid : placed_nodes(diagram)) {
        const auto& node = known.at(uuid); // assign_streams checked placement
        if (node.endpoint.empty()) {
            result.errors.push_back("Node " + uuid + " has no endpoint in 'runtime_nodes'.");
        }
        targets.push_back(node);
        result.nodes.push_back(NodeOutcome{node.node_uuid, node.endpoint, "error", {}});
    }
    if (!result.errors.empty()) {
        return result;
    }

    // Each node must be the one the diagram names, and report its active
    // revision and the data protocol it speaks.
    std::vector<std::uint64_t> active(targets.size());
    std::vector<ControlLimits> limits(targets.size());
    std::unordered_map<std::string, DataProtocol> protocols;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        try {
            const auto hello = request(targets[i], request_object("HELLO", "hello-" + std::to_string(i)));
            if (string_of(hello, "type") != "HELLO_OK" || string_of(hello, "node_uuid") != targets[i].node_uuid) {
                result.nodes[i].message = "The node at " + targets[i].endpoint + " is " + string_of(hello, "node_uuid") + ", not " +
                                          targets[i].node_uuid + ".";
                result.errors.push_back(result.nodes[i].message);
                continue;
            }
            const auto list = request(targets[i], request_object("LIST_DEPLOYMENTS", "list-" + std::to_string(i)));
            const auto* revision = list.if_contains("active_revision");
            active[i] = revision && (revision->is_int64() || revision->is_uint64()) ? revision->to_number<std::uint64_t>() : 0;
            const auto info = request(targets[i], request_object("GET_NODE_INFO", "info-" + std::to_string(i)));
            limits[i] = control_limits_of(info);
            // Deployed nodes compare times in the shared "utc" domain: each must
            // report its clock synchronized within its bound.
            if (!has_utc_domain(info)) {
                const auto* sync = info.if_contains("clock_sync");
                result.nodes[i].message = "The clock of the node at " + targets[i].endpoint + " is not synchronized (" +
                                          (sync ? json::serialize(*sync) : std::string("no clock_sync reported")) + ").";
                result.errors.push_back(result.nodes[i].message);
            }
            if (const auto protocol = data_protocol_of(info)) {
                protocols[targets[i].node_uuid] = *protocol;
            } else {
                result.nodes[i].message = "The node at " + targets[i].endpoint + " does not advertise its data protocol (NODE_INFO.data_protocol).";
                result.errors.push_back(result.nodes[i].message);
            }
        } catch (const std::exception& ex) {
            result.nodes[i].message = ex.what();
            result.errors.push_back(targets[i].endpoint + ": " + ex.what());
        }
    }
    if (!result.errors.empty()) {
        return result;
    }
    result.errors = check_data_protocols(diagram, protocols);
    if (!result.errors.empty()) {
        return result;
    }

    // The configuration every node receives, digested as the nodes will parse it.
    json::object configuration{{"diagram", diagram}};
    const auto content_digest = configuration_digest(parse_json_precise(json::serialize(configuration)));
    configuration["project_revision"] = options.project_revision.empty() ? "diagram:" + content_digest.substr(7, 16) : options.project_revision;
    configuration = parse_json_precise(json::serialize(configuration)).as_object();
    result.configuration_digest = configuration_digest(configuration);
    configuration["configuration_digest"] = result.configuration_digest;
    result.revision = *std::ranges::max_element(active) + 1;
    result.deployment_id = options.deployment_id.empty() ? "dep-" + result.configuration_digest.substr(7, 12) + "-r" +
                                                               std::to_string(result.revision)
                                                         : options.deployment_id;

    // Prepare everywhere; commit only if every node is ready. A configuration
    // larger than a node takes directly goes by chunked transfer first.
    const auto configuration_text = json::serialize(configuration);
    const auto transfer_id = "transfer-" + result.configuration_digest.substr(7, 16) + "-r" + std::to_string(result.revision);
    auto transfer = [&](const std::size_t i) {
        auto expect_type = [&](const json::object& response, const char* type) {
            if (string_of(response, "type") != type) {
                throw std::runtime_error("configuration transfer: " + describe(response));
            }
        };
        auto begin = request_object("BEGIN_CONFIGURATION_TRANSFER", "begin-" + std::to_string(i));
        begin["transfer_id"] = transfer_id;
        begin["total_bytes"] = configuration_text.size();
        begin["configuration_digest"] = result.configuration_digest;
        expect_type(request(targets[i], begin), "CONFIGURATION_TRANSFER_STARTED");
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(configuration_text.data());
        std::uint64_t index = 0;
        for (std::size_t offset = 0; offset < configuration_text.size(); offset += limits[i].max_chunk_bytes, ++index) {
            const auto length = std::min<std::size_t>(limits[i].max_chunk_bytes, configuration_text.size() - offset);
            auto chunk = request_object("CONFIGURATION_CHUNK", "chunk-" + std::to_string(i) + "-" + std::to_string(index));
            chunk["transfer_id"] = transfer_id;
            chunk["index"] = index;
            chunk["offset"] = offset;
            chunk["data"] = encode_base64url({bytes + offset, length});
            expect_type(request(targets[i], chunk), "CONFIGURATION_CHUNK_ACCEPTED");
        }
        auto end = request_object("END_CONFIGURATION_TRANSFER", "end-" + std::to_string(i));
        end["transfer_id"] = transfer_id;
        expect_type(request(targets[i], end), "CONFIGURATION_TRANSFER_COMPLETE");
    };
    std::vector<std::string> tokens(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        auto prepare = request_object("PREPARE_DEPLOYMENT", "prepare-" + std::to_string(i));
        prepare["deployment"] = json::object{
            {"deployment_id", result.deployment_id},
            {"revision", result.revision},
            {"base_revision", active[i]},
        };
        try {
            if (configuration_text.size() > limits[i].max_configuration_bytes) {
                transfer(i);
                prepare["deployment"].as_object()["configuration_transfer_id"] = transfer_id;
            } else {
                prepare["deployment"].as_object()["configuration"] = configuration;
            }
            const auto response = request(targets[i], prepare);
            if (string_of(response, "prepare_status") == "READY") {
                tokens[i] = string_of(response, "prepare_token");
                result.nodes[i].status = "ready";
            } else {
                result.nodes[i].status = string_of(response, "type") == "ERROR" ? "error" : "not_ready";
                result.nodes[i].message = describe(response);
                result.errors.push_back(targets[i].endpoint + ": " + result.nodes[i].message);
            }
        } catch (const std::exception& ex) {
            result.nodes[i].message = ex.what();
            result.errors.push_back(targets[i].endpoint + ": " + ex.what());
        }
    }
    auto token_request = [&](const char* type, const std::size_t i) {
        auto message = request_object(type, std::string(type) + "-" + std::to_string(i));
        message["deployment_id"] = result.deployment_id;
        message["revision"] = result.revision;
        message["prepare_token"] = tokens[i];
        return message;
    };
    if (!result.errors.empty()) {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (!tokens[i].empty()) {
                try {
                    static_cast<void>(request(targets[i], token_request("ABORT_DEPLOYMENT", i)));
                } catch (const std::exception&) {
                    // The preparation expires on its own.
                }
            }
        }
        return result;
    }

    for (std::size_t i = 0; i < targets.size(); ++i) {
        try {
            const auto response = request(targets[i], token_request("COMMIT_DEPLOYMENT", i));
            if (const auto* committed = response.if_contains("committed"); committed && committed->is_bool() && committed->as_bool()) {
                result.nodes[i].status = "committed";
            } else {
                result.nodes[i].status = "error";
                result.nodes[i].message = describe(response);
                result.errors.push_back(targets[i].endpoint + ": commit failed: " + result.nodes[i].message);
            }
        } catch (const std::exception& ex) {
            result.nodes[i].status = "error";
            result.nodes[i].message = ex.what();
            result.errors.push_back(targets[i].endpoint + ": commit failed: " + ex.what());
        }
    }
    result.committed = result.errors.empty();
    return result;
}

} // namespace s7::deployment
