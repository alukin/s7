#include "s7/runtime_service.hpp"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

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
            if (const auto tls_it = server_obj.find("tls"); tls_it != server_obj.end()) {
                const auto& tls_obj = expect_object(tls_it->value(), "websocket.server.tls");
                definition.server.tls.enabled = get_bool(tls_obj, "enabled", false);
                if (const auto cert_it = tls_obj.find("cert_file"); cert_it != tls_obj.end() && cert_it->value().is_string()) {
                    definition.server.tls.cert_file = cert_it->value().as_string().c_str();
                }
                if (const auto key_it = tls_obj.find("key_file"); key_it != tls_obj.end() && key_it->value().is_string()) {
                    definition.server.tls.key_file = key_it->value().as_string().c_str();
                }
                if (const auto ca_it = tls_obj.find("ca_file"); ca_it != tls_obj.end() && ca_it->value().is_string()) {
                    definition.server.tls.ca_file = ca_it->value().as_string().c_str();
                }
                definition.server.tls.require_client_cert = get_bool(tls_obj, "require_client_cert", true);
            }
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
            if (const auto tls_it = client_obj.find("tls"); tls_it != client_obj.end()) {
                const auto& tls_obj = expect_object(tls_it->value(), "websocket.client.tls");
                definition.client.tls.enabled = get_bool(tls_obj, "enabled", false);
                if (const auto cert_it = tls_obj.find("cert_file"); cert_it != tls_obj.end() && cert_it->value().is_string()) {
                    definition.client.tls.cert_file = cert_it->value().as_string().c_str();
                }
                if (const auto key_it = tls_obj.find("key_file"); key_it != tls_obj.end() && key_it->value().is_string()) {
                    definition.client.tls.key_file = key_it->value().as_string().c_str();
                }
                if (const auto ca_it = tls_obj.find("ca_file"); ca_it != tls_obj.end() && ca_it->value().is_string()) {
                    definition.client.tls.ca_file = ca_it->value().as_string().c_str();
                }
                definition.client.tls.require_client_cert = get_bool(tls_obj, "require_client_cert", true);
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
            schema->add_element(element.id, [topic]() -> std::unique_ptr<s7::Element> {
                return std::make_unique<s7::NetSink>(topic);
            });
            continue;
        }

        if (element.type == "NetSource") {
            const auto topic = expect_string(params, "topic");
            schema->add_element(element.id, [topic]() -> std::unique_ptr<s7::Element> {
                return std::make_unique<s7::NetSource>(topic);
            });
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

[[nodiscard]] auto detect_os_family() -> std::string
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

[[nodiscard]] auto detect_architecture() -> std::string
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm32";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

[[nodiscard]] auto default_node_storage_dir() -> std::filesystem::path
{
    if (const char* explicit_dir = std::getenv("S7_NODE_STATE_DIR")) {
        return std::filesystem::path(explicit_dir);
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".s7runtime" / "default-node";
    }
    return std::filesystem::temp_directory_path() / "s7runtime-default-node";
}

[[nodiscard]] auto load_or_create_node_uuid(const std::filesystem::path& node_dir) -> std::string
{
    std::filesystem::create_directories(node_dir);
    const auto path = node_dir / "node_uuid.txt";
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        std::string existing;
        std::getline(in, existing);
        if (!existing.empty()) {
            return existing;
        }
    }

    const auto generated = boost::uuids::to_string(boost::uuids::random_generator()());
    std::ofstream out(path, std::ios::trunc);
    out << generated;
    return generated;
}

[[nodiscard]] auto build_control_descriptor(const std::string& node_uuid,
                                            const transport::RuntimeNetEndpointPtr& endpoint,
                                            const RuntimeSchemaDefinition& definition) -> ControlNodeDescriptor
{
    ControlNodeDescriptor descriptor;
    descriptor.node_uuid = node_uuid;
    descriptor.node_name = "node-" + node_uuid.substr(0, 8);
    descriptor.display_name = descriptor.node_name;
    descriptor.instance_id = boost::uuids::to_string(boost::uuids::random_generator()());
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &now_time);
#else
        gmtime_r(&now_time, &tm);
#endif
        std::ostringstream stamp;
        stamp << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        descriptor.started_at = stamp.str();
    }
    descriptor.os_family = detect_os_family();
    descriptor.os_version = "unknown";
    descriptor.architecture = detect_architecture();
    descriptor.target_triple = descriptor.architecture + "-" + descriptor.os_family;
    descriptor.listen_url = definition.server.enabled
        ? ((definition.server.tls.enabled ? "wss://" : "ws://") + std::string("127.0.0.1:") + std::to_string(definition.server.port))
        : std::string{};

    std::set<std::string> unique_types;
    for (const auto& element_desc : ElementRegistry::instance().all_descriptors()) {
        unique_types.insert(element_desc.type_name());
    }
    descriptor.element_types.assign(unique_types.begin(), unique_types.end());

    ControlLibraryInfo registry_library;
    registry_library.library_id = "runtime-registry";
    registry_library.kind = "builtin";
    registry_library.version = "0.1.0";
    registry_library.target_triple = descriptor.target_triple;
    registry_library.origin = "registry";
    registry_library.elements = descriptor.element_types;
    descriptor.libraries.push_back(std::move(registry_library));

    if (endpoint && endpoint->backend_name() == "beast") {
        descriptor.hardware_capabilities.push_back("net:websocket");
    }
    return descriptor;
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
    , runtime_instance_uuid_(load_or_create_node_uuid(default_node_storage_dir()))
    , control_plane_(nullptr)
    , network_endpoint_(nullptr)
    , opened_server_endpoints_()
    , opened_client_endpoints_()
{
    if (definition_.server.enabled || definition_.client.enabled) {
        network_endpoint_ = std::make_shared<s7::transport::BeastRuntimeNetEndpoint>();
    } else {
        network_endpoint_ = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
    }

    control_plane_ = std::make_shared<ControlPlaneMvp>(
        build_control_descriptor(runtime_instance_uuid_, network_endpoint_, definition_),
        default_node_storage_dir());

    if (const auto beast_endpoint = std::dynamic_pointer_cast<s7::transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
        std::weak_ptr<ControlPlaneMvp> weak_control = control_plane_;
        beast_endpoint->set_control_request_handler([weak_control](const std::string& request_json) {
            const auto locked = weak_control.lock();
            if (!locked) {
                return std::string{"{\"type\":\"ERROR\",\"protocol_version\":1,\"code\":\"INTERNAL_ERROR\",\"message\":\"Control service unavailable\"}"};
            }
            return locked->handle_request_json(request_json);
        });
    }
    RuntimeNetContext::set_endpoint(network_endpoint_);
}

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
    out << "runtime.transport.backend=" << (network_endpoint_ ? network_endpoint_->backend_name() : std::string{"none"}) << '\n';
    out << "runtime.elements=" << definition_.elements.size() << '\n';
    out << "runtime.connections=" << definition_.connections.size() << '\n';
    out << "runtime.websocket.server.enabled=" << (definition_.server.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.server.port=" << definition_.server.port << '\n';
    out << "runtime.websocket.server.tls.enabled=" << (definition_.server.tls.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.server.tls.cert_file=" << definition_.server.tls.cert_file << '\n';
    out << "runtime.websocket.server.tls.ca_file=" << definition_.server.tls.ca_file << '\n';
    out << "runtime.websocket.client.enabled=" << (definition_.client.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.client.url=" << definition_.client.url << '\n';
    out << "runtime.websocket.client.tls.enabled=" << (definition_.client.tls.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.client.tls.cert_file=" << definition_.client.tls.cert_file << '\n';
    out << "runtime.websocket.client.tls.ca_file=" << definition_.client.tls.ca_file << '\n';
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

auto RuntimeService::network_endpoint() const -> transport::RuntimeNetEndpointPtr {
    return network_endpoint_;
}

auto RuntimeService::handle_control_request_json(const std::string& request_json) -> std::string
{
    if (!control_plane_) {
        throw std::runtime_error("Control plane is not initialized.");
    }
    return control_plane_->handle_request_json(request_json);
}

void RuntimeService::add_seed_peer(const std::string& endpoint)
{
    if (!control_plane_) {
        return;
    }
    control_plane_->add_seed_peer(endpoint);
}

void RuntimeService::start_websocket_server() const {
    if (!definition_.server.enabled) {
        return;
    }

    if (definition_.server.port == 0) {
        throw std::invalid_argument("WebSocket server is enabled but port is 0.");
    }

    if (!network_endpoint_) {
        throw std::runtime_error("Runtime transport endpoint is not configured.");
    }

    if (const auto beast_endpoint = std::dynamic_pointer_cast<s7::transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
        beast_endpoint->configure_server(definition_.server.port, definition_.server.tls);
    }

    const auto control_stream = "server:" + std::to_string(definition_.server.port) + ":/control";
    const auto data_stream = "server:" + std::to_string(definition_.server.port) + ":/data";

    const auto control_open = network_endpoint_->open_endpoint({
        .endpoint_name = "server/control",
        .stream_name = control_stream,
        .allow_publish = true,
        .allow_receive = true,
    });
    if (!control_open.ok) {
        throw std::runtime_error("Failed to start control endpoint: " + control_open.error);
    }

    const auto data_open = network_endpoint_->open_endpoint({
        .endpoint_name = "server/data",
        .stream_name = data_stream,
        .allow_publish = true,
        .allow_receive = true,
    });
    if (!data_open.ok) {
        network_endpoint_->close_endpoint(control_open.handle);
        throw std::runtime_error("Failed to start data endpoint: " + data_open.error);
    }

    opened_server_endpoints_.push_back(control_open.handle);
    opened_server_endpoints_.push_back(data_open.handle);
}

void RuntimeService::connect_websocket_client() const {
    if (!definition_.client.enabled) {
        return;
    }

    if (definition_.client.url.empty()) {
        throw std::invalid_argument("WebSocket client is enabled but URL is empty.");
    }

    if (!network_endpoint_) {
        throw std::runtime_error("Runtime transport endpoint is not configured.");
    }

    const auto parsed_url = s7::transport::parse_ws_url(definition_.client.url);

    if (const auto beast_endpoint = std::dynamic_pointer_cast<s7::transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
        beast_endpoint->configure_client(definition_.client.url, definition_.client.tls);
    }

    const auto control_open = network_endpoint_->open_endpoint({
        .endpoint_name = "client/control",
        .stream_name = "client:" + parsed_url.host + ":" + parsed_url.port + parsed_url.target + ":/control",
        .allow_publish = true,
        .allow_receive = true,
    });
    if (!control_open.ok) {
        throw std::runtime_error("Failed to connect control endpoint: " + control_open.error);
    }

    const auto data_open = network_endpoint_->open_endpoint({
        .endpoint_name = "client/data",
        .stream_name = "client:" + parsed_url.host + ":" + parsed_url.port + parsed_url.target + ":/data",
        .allow_publish = true,
        .allow_receive = true,
    });
    if (!data_open.ok) {
        network_endpoint_->close_endpoint(control_open.handle);
        throw std::runtime_error("Failed to connect data endpoint: " + data_open.error);
    }

    opened_client_endpoints_.push_back(control_open.handle);
    opened_client_endpoints_.push_back(data_open.handle);
}

} // namespace s7
