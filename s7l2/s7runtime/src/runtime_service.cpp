#include "s7/runtime_service.hpp"

#include <cmath>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/system/error_code.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "s7/io.hpp"
#include "s7/net.hpp"
#include "s7/element_registry.hpp"

namespace s7 {
namespace {

const boost::json::object& expect_object(const boost::json::value& value, const std::string_view name) {
    if (!value.is_object()) {
        throw std::invalid_argument("Expected object for '" + std::string(name) + "'.");
    }
    return value.as_object();
}

const boost::json::array& expect_array(const boost::json::object& object, const std::string_view key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_array()) {
        throw std::invalid_argument("Expected array field '" + std::string(key) + "'.");
    }
    return it->value().as_array();
}

std::string expect_string(const boost::json::object& object, const std::string_view key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_string()) {
        throw std::invalid_argument("Expected string field '" + std::string(key) + "'.");
    }
    return it->value().as_string().c_str();
}

auto signal_type_from_string(const std::string_view value) -> SignalType
{
    if (value == "DOUBLE" || value == "double") {
        return SignalType::DOUBLE;
    }
    if (value == "COMPLEX" || value == "complex") {
        return SignalType::COMPLEX;
    }
    if (value == "INT8") return SignalType::INT8;
    if (value == "UINT8") return SignalType::UINT8;
    if (value == "INT12") return SignalType::INT12;
    if (value == "UINT12") return SignalType::UINT12;
    if (value == "INT16") return SignalType::INT16;
    if (value == "UINT16") return SignalType::UINT16;
    throw std::invalid_argument("Unsupported signal type string: '" + std::string(value) + "'.");
}

auto parse_schema_port_bindings(const boost::json::object& root, const std::string_view field_name)
    -> std::vector<SchemaPortBinding>
{
    std::vector<SchemaPortBinding> bindings;
    const auto it = root.find(field_name);
    if (it == root.end()) {
        return bindings;
    }
    if (!it->value().is_array()) {
        throw std::invalid_argument("Expected array field '" + std::string(field_name) + "'.");
    }

    for (const auto& binding_value : it->value().as_array()) {
        if (!binding_value.is_object()) {
            throw std::invalid_argument("Expected object in field '" + std::string(field_name) + "'.");
        }

        const auto& binding_object = binding_value.as_object();
        SchemaPortBinding binding;
        binding.name = expect_string(binding_object, "name");
        binding.type = expect_string(binding_object, "type");
        binding.element = expect_string(binding_object, "element");
        if (const auto port_it = binding_object.find("port"); port_it != binding_object.end()) {
            if (port_it->value().is_uint64()) {
                binding.port = port_it->value().as_uint64();
            } else if (port_it->value().is_int64() && port_it->value().as_int64() >= 0) {
                binding.port = static_cast<std::uint64_t>(port_it->value().as_int64());
            } else {
                throw std::invalid_argument("Expected non-negative integer 'port' in schema port binding.");
            }
        }
        bindings.push_back(std::move(binding));
    }

    return bindings;
}

bool get_bool(const boost::json::object& object, const std::string_view key, bool default_value) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return default_value;
    }
    if (!it->value().is_bool()) {
        throw std::invalid_argument("Expected boolean field '" + std::string(key) + "'.");
    }
    return it->value().as_bool();
}

double get_f64(const boost::json::object& object, const std::string_view key, double default_value) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return default_value;
    }
    if (it->value().is_double()) {
        return it->value().as_double();
    }
    if (it->value().is_int64()) {
        return static_cast<double>(it->value().as_int64());
    }
    if (it->value().is_uint64()) {
        return static_cast<double>(it->value().as_uint64());
    }
    throw std::invalid_argument("Expected floating-point field '" + std::string(key) + "'.");
}

std::uint64_t get_u64(const boost::json::object& object, const std::string_view key, std::uint64_t default_value) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return default_value;
    }

    if (it->value().is_uint64()) {
        return it->value().as_uint64();
    }

    if (it->value().is_int64()) {
        const auto signed_value = it->value().as_int64();
        if (signed_value < 0) {
            throw std::invalid_argument("Field '" + std::string(key) + "' must be non-negative.");
        }
        return static_cast<std::uint64_t>(signed_value);
    }

    throw std::invalid_argument("Expected unsigned integer field '" + std::string(key) + "'.");
}

RuntimeSchemaDefinition parse_schema_definition(const boost::json::value& json_value) {
    const auto& root = expect_object(json_value, "root");

    RuntimeSchemaDefinition definition;
    definition.name = expect_string(root, "name");
    definition.exposed_inputs = parse_schema_port_bindings(root, "exposed_inputs");
    definition.exposed_outputs = parse_schema_port_bindings(root, "exposed_outputs");

    for (const auto& element_value : expect_array(root, "elements")) {
        const auto& element_obj = expect_object(element_value, "element");

        JsonElementDefinition element;
        element.id = expect_string(element_obj, "id");
        element.type = expect_string(element_obj, "type");

        const auto params_it = element_obj.find("params");
        if (params_it != element_obj.end()) {
            if (!params_it->value().is_object()) {
                throw std::invalid_argument("Expected object field 'params'.");
            }
            element.params = params_it->value().as_object();
        }

        if (const auto schema_it = element_obj.find("schema"); schema_it != element_obj.end()) {
            if (!schema_it->value().is_object()) {
                throw std::invalid_argument("Expected object field 'schema'.");
            }
            element.nested_schema = schema_it->value();
        }

        definition.elements.push_back(std::move(element));
    }

    for (const auto& connection_value : expect_array(root, "connections")) {
        const auto& connection_obj = expect_object(connection_value, "connection");

        JsonConnectionDefinition connection;
        connection.from_element = expect_string(connection_obj, "from_element");
        connection.from_port = get_u64(connection_obj, "from_port", 0);
        connection.to_element = expect_string(connection_obj, "to_element");
        connection.to_port = get_u64(connection_obj, "to_port", 0);

        definition.connections.push_back(std::move(connection));
    }

    const auto websocket_it = root.find("websocket");
    if (websocket_it != root.end()) {
        const auto& websocket_obj = expect_object(websocket_it->value(), "websocket");

        const auto server_it = websocket_obj.find("server");
        if (server_it != websocket_obj.end()) {
            const auto& server_obj = expect_object(server_it->value(), "server");
            definition.server.enabled = get_bool(server_obj, "enabled", false);
            definition.server.port = static_cast<std::uint16_t>(get_u64(server_obj, "port", 0));
        }

        const auto client_it = websocket_obj.find("client");
        if (client_it != websocket_obj.end()) {
            const auto& client_obj = expect_object(client_it->value(), "client");
            definition.client.enabled = get_bool(client_obj, "enabled", false);
            const auto url_it = client_obj.find("url");
            if (url_it != client_obj.end()) {
                if (!url_it->value().is_string()) {
                    throw std::invalid_argument("Expected string field 'websocket.client.url'.");
                }
                definition.client.url = url_it->value().as_string().c_str();
            }
        }
    }

    const auto simulation_it = root.find("simulation");
    if (simulation_it != root.end()) {
        const auto& simulation_obj = expect_object(simulation_it->value(), "simulation");
        definition.simulation.enabled = true;
        definition.simulation.periods = get_u64(simulation_obj, "periods", 20);
        definition.simulation.sample_rate_hz = get_u64(simulation_obj, "sample_rate_hz", 1);
        definition.simulation.frequency_hz = get_f64(simulation_obj, "frequency_hz", 1.0);
    }

    return definition;
}

auto build_schema_from_definition(const RuntimeSchemaDefinition& definition) -> std::unique_ptr<s7::Schema>
{
    auto schema = std::make_unique<s7::Schema>(definition.name);
    auto& registry = s7::ElementRegistry::instance();

    for (const auto& element : definition.elements) {
        const auto& params = element.params;

        if (element.nested_schema.is_object()) {
            const auto nested_definition = parse_schema_definition(element.nested_schema);
            schema->add_element(element.id, [nested_definition] {
                auto nested = build_schema_from_definition(nested_definition);
                return std::unique_ptr<s7::Element>(nested.release());
            });
            continue;
        }

        // Prefer registry-backed construction to support dynamically loaded plugins.
        if (const auto* descriptor = registry.find(element.type)) {
            schema->add_element(element.id, [descriptor, params] {
                const s7::ElementCreationContext local_ctx{descriptor->metadata(), params};
                return descriptor->create(local_ctx);
            });
            continue;
        }

        if (element.type == "Constant") {
            const auto value = get_f64(params, "value", 0.0);
            schema->add_element(element.id, [value] { return std::make_unique<s7::Constant>(value); });
            continue;
        }

        if (element.type == "Integrator") {
            const auto initial_value = get_f64(params, "initial_value", 0.0);
            schema->add_element(element.id, [initial_value] { return std::make_unique<s7::Integrator>(initial_value); });
            continue;
        }

        if (element.type == "Adder") {
            schema->add_element(element.id, [] { return std::make_unique<s7::Adder>(); });
            continue;
        }

        if (element.type == "Multiply") {
            schema->add_element(element.id, [] { return std::make_unique<s7::Multiply>(); });
            continue;
        }

        if (element.type == "AnalogOscillator") {
            const auto amplitude = get_f64(params, "amplitude", 1.0);
            const auto frequency_hz = get_f64(params, "frequency_hz", definition.simulation.frequency_hz);
            const auto sample_rate_hz = get_f64(params, "sample_rate_hz",
                                                static_cast<double>(definition.simulation.sample_rate_hz));
            const auto phase = get_f64(params, "phase", 0.0);
            schema->add_element(element.id, [amplitude, frequency_hz, sample_rate_hz, phase] {
                return std::make_unique<s7::AnalogOscillator>(amplitude, frequency_hz, sample_rate_hz, phase);
            });
            continue;
        }

        if (element.type == "NetSink") {
            const auto topic = expect_string(params, "topic");
            schema->add_element(element.id, [topic] { return std::make_unique<s7::NetSink>(topic); });
            continue;
        }

        if (element.type == "RealGnuplotFileSink") {
            const auto file_path = std::filesystem::path(expect_string(params, "file_path"));
            schema->add_element(element.id, [file_path] {
                return std::make_unique<s7::io::RealGnuplotFileSink>(file_path);
            });
            continue;
        }

        if (element.type == "RealCompactBinaryFileSink") {
            const auto file_path = std::filesystem::path(expect_string(params, "file_path"));
            schema->add_element(element.id, [file_path] {
                return std::make_unique<s7::io::RealCompactBinaryFileSink>(file_path);
            });
            continue;
        }

        if (element.type == "RealGnuplotFileSource") {
            const auto file_path = std::filesystem::path(expect_string(params, "file_path"));
            schema->add_element(element.id, [file_path] {
                return std::make_unique<s7::io::RealGnuplotFileSource>(file_path);
            });
            continue;
        }

        if (element.type == "RealCompactBinaryFileSource") {
            const auto file_path = std::filesystem::path(expect_string(params, "file_path"));
            schema->add_element(element.id, [file_path] {
                return std::make_unique<s7::io::RealCompactBinaryFileSource>(file_path);
            });
            continue;
        }

        throw std::invalid_argument("Unsupported runtime element type: '" + element.type + "'.");
    }

    for (const auto& connection : definition.connections) {
        schema->connect(connection.from_element,
                        static_cast<std::size_t>(connection.from_port),
                        connection.to_element,
                        static_cast<std::size_t>(connection.to_port));
    }

    for (const auto& binding : definition.exposed_inputs) {
        schema->expose_input(binding.name,
                             signal_type_from_string(binding.type),
                             binding.element,
                             static_cast<std::size_t>(binding.port));
    }

    for (const auto& binding : definition.exposed_outputs) {
        schema->expose_output(binding.name,
                              signal_type_from_string(binding.type),
                              binding.element,
                              static_cast<std::size_t>(binding.port));
    }

    return schema;
}

struct ParsedWebSocketUrl {
    std::string host;
    std::string port;
    std::string target;
};

ParsedWebSocketUrl parse_ws_url(const std::string& url) {
    const std::string prefix = "ws://";
    if (url.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("Only ws:// URLs are supported in this starter: '" + url + "'.");
    }

    const auto host_start = prefix.size();
    const auto path_pos = url.find('/', host_start);
    const auto host_port = path_pos == std::string::npos ? url.substr(host_start) : url.substr(host_start, path_pos - host_start);

    const auto colon_pos = host_port.rfind(':');
    if (colon_pos == std::string::npos || colon_pos == 0 || colon_pos + 1 >= host_port.size()) {
        throw std::invalid_argument("WebSocket URL must include host:port: '" + url + "'.");
    }

    ParsedWebSocketUrl parsed;
    parsed.host = host_port.substr(0, colon_pos);
    parsed.port = host_port.substr(colon_pos + 1);
    parsed.target = path_pos == std::string::npos ? "/" : url.substr(path_pos);
    return parsed;
}

} // namespace

RuntimeSchemaDefinition JsonSchemaLoader::load_from_file(const std::string& file_path) {
    std::ifstream in(file_path);
    if (!in) {
        throw std::runtime_error("Could not open schema file: '" + file_path + "'.");
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    boost::system::error_code ec;
    const auto parsed = boost::json::parse(buffer.str(), ec);
    if (ec) {
        throw std::runtime_error("JSON parsing failed for '" + file_path + "': " + ec.message());
    }

    return parse_schema_definition(parsed);
}

RuntimeService::RuntimeService(RuntimeSchemaDefinition definition)
    : definition_(std::move(definition))
    , runtime_instance_uuid_(boost::uuids::to_string(boost::uuids::random_generator()())) {}

const RuntimeSchemaDefinition& RuntimeService::schema() const {
    return definition_;
}

bool RuntimeService::has_simulation() const {
    return definition_.simulation.enabled;
}

std::uint64_t RuntimeService::simulation_step_count() const {
    if (!definition_.simulation.enabled) {
        return 0;
    }

    if (definition_.simulation.periods == 0 || definition_.simulation.sample_rate_hz == 0) {
        return 0;
    }

    if (definition_.simulation.frequency_hz <= 0.0) {
        throw std::invalid_argument("Simulation frequency must be positive.");
    }

    const auto steps = std::llround(static_cast<long double>(definition_.simulation.periods) *
                                    static_cast<long double>(definition_.simulation.sample_rate_hz) /
                                    static_cast<long double>(definition_.simulation.frequency_hz));
    if (steps < 0) {
        throw std::invalid_argument("Simulation step count computed as negative.");
    }

    return static_cast<std::uint64_t>(steps);
}

std::vector<std::filesystem::path> RuntimeService::output_files() const {
    std::vector<std::filesystem::path> files;

    for (const auto& element : definition_.elements) {
        if (element.type == "RealGnuplotFileSink" || element.type == "RealCompactBinaryFileSink") {
            const auto params_it = element.params.find("file_path");
            if (params_it != element.params.end() && params_it->value().is_string()) {
                files.emplace_back(params_it->value().as_string().c_str());
            }
        }
    }

    return files;
}

s7::Schema RuntimeService::build_schema() const {
    auto schema = build_schema_from_definition(definition_);
    return std::move(*schema);
}

void RuntimeService::print_summary(std::ostream& out) const {
    out << "runtime.schema=" << definition_.name << '\n';
    out << "runtime.instance_uuid=" << runtime_instance_uuid_ << '\n';
    out << "runtime.elements=" << definition_.elements.size() << '\n';
    out << "runtime.connections=" << definition_.connections.size() << '\n';
    out << "runtime.websocket.server.enabled=" << (definition_.server.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.server.port=" << definition_.server.port << '\n';
    out << "runtime.websocket.client.enabled=" << (definition_.client.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.client.url=" << definition_.client.url << '\n';
    out << "runtime.simulation.enabled=" << (definition_.simulation.enabled ? "true" : "false") << '\n';
    out << "runtime.simulation.periods=" << definition_.simulation.periods << '\n';
    out << "runtime.simulation.sample_rate_hz=" << definition_.simulation.sample_rate_hz << '\n';
    out << "runtime.simulation.frequency_hz=" << definition_.simulation.frequency_hz << '\n';
}

const std::string& RuntimeService::runtime_instance_uuid() const {
    return runtime_instance_uuid_;
}

std::string RuntimeService::create_schema_instance_uuid() const {
    return boost::uuids::to_string(boost::uuids::random_generator()());
}

void RuntimeService::start_websocket_server() const {
    if (!definition_.server.enabled) {
        return;
    }

    if (definition_.server.port == 0) {
        throw std::invalid_argument("WebSocket server is enabled but port is 0.");
    }

    boost::asio::io_context io_context;
    boost::asio::ip::tcp::acceptor acceptor(
        io_context,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), definition_.server.port));

    // This starter only verifies bind/listen behavior; accept loop and protocol are added later.
    acceptor.listen(boost::asio::socket_base::max_listen_connections);
}

void RuntimeService::connect_websocket_client() const {
    if (!definition_.client.enabled) {
        return;
    }

    if (definition_.client.url.empty()) {
        throw std::invalid_argument("WebSocket client is enabled but URL is empty.");
    }

    const auto parsed_url = parse_ws_url(definition_.client.url);

    boost::asio::io_context io_context;
    boost::asio::ip::tcp::resolver resolver(io_context);
    boost::beast::tcp_stream tcp_stream(io_context);
    auto websocket_stream = boost::beast::websocket::stream<boost::beast::tcp_stream>(std::move(tcp_stream));

    const auto results = resolver.resolve(parsed_url.host, parsed_url.port);
    websocket_stream.next_layer().connect(results);
    websocket_stream.handshake(parsed_url.host, parsed_url.target);
    websocket_stream.close(boost::beast::websocket::close_code::normal);
}

} // namespace s7
