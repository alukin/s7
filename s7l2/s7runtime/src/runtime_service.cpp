#include "s7/runtime_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include <boost/system/error_code.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

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
    return parse_signal_type(value);
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

std::uint16_t get_u16_port(const boost::json::object& object, const std::string_view key, const std::uint16_t default_value)
{
    const auto value = get_u64(object, key, default_value);
    if (value > 0xFFFFu) {
        throw std::invalid_argument("Field '" + std::string(key) + "' exceeds uint16 port range.");
    }
    return static_cast<std::uint16_t>(value);
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
            definition.server.port = get_u16_port(server_obj, "port", 0);
            if (const auto bind_it = server_obj.find("bind_address"); bind_it != server_obj.end()) {
                if (!bind_it->value().is_string()) {
                    throw std::invalid_argument("Expected string field 'websocket.server.bind_address'.");
                }
                definition.server.bind_address = std::string(bind_it->value().as_string());
            }
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

    if (const auto control_it = root.find("control"); control_it != root.end()) {
        const auto& control_obj = expect_object(control_it->value(), "control");
        definition.control.studio_key_purpose_oid = expect_string(control_obj, "studio_key_purpose_oid");
        definition.control.studio_certificate_sha256 = expect_string(control_obj, "studio_certificate_sha256");
        auto& fingerprint = definition.control.studio_certificate_sha256;
        std::erase(fingerprint, ':');
        std::ranges::transform(fingerprint, fingerprint.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (fingerprint.size() != 64 || !std::ranges::all_of(fingerprint, [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); })) {
            throw std::invalid_argument("control.studio_certificate_sha256 must be a SHA-256 fingerprint (64 hex digits).");
        }
    }

    if (const auto clock_it = root.find("clock"); clock_it != root.end()) {
        const auto& clock_obj = expect_object(clock_it->value(), "clock");
        definition.clock.max_error_ms = get_f64(clock_obj, "max_error_ms", definition.clock.max_error_ms);
        definition.clock.assume_synchronized = get_bool(clock_obj, "assume_synchronized", false);
        if (!std::isfinite(definition.clock.max_error_ms) || definition.clock.max_error_ms <= 0.0) {
            throw std::invalid_argument("clock.max_error_ms must be a positive number.");
        }
    }

    if (const auto execution_it = root.find("execution"); execution_it != root.end()) {
        const auto& execution_obj = expect_object(execution_it->value(), "execution");
        definition.execution.step_rate_hz = get_f64(execution_obj, "step_rate_hz", 0.0);
        if (!std::isfinite(definition.execution.step_rate_hz) || definition.execution.step_rate_hz < 0.0) {
            throw std::invalid_argument("execution.step_rate_hz must be a non-negative number.");
        }
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

        // Registry elements: built-ins, network elements and plugins (generic ones get inferred types).
        if (const auto* descriptor = registry.find(element.type)) {
            schema->add_element(s7::make_element_spec(element.id, *descriptor, params));
            continue;
        }

        // File I/O elements are not in the registry yet.
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

// Returns the canonical lower-case form, or nullopt if the text is not a UUID.
[[nodiscard]] auto canonical_uuid(const std::string& text) -> std::optional<std::string>
{
    if (text.size() != 36) {
        return std::nullopt;
    }
    try {
        return boost::uuids::to_string(boost::uuids::string_generator{}(text));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void write_node_uuid_file(const std::filesystem::path& path, const std::string& node_uuid)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Could not create node UUID file at '" + path.string() + "'.");
    }
    out << node_uuid << '\n';
    if (!out) {
        throw std::runtime_error("Failed while writing node UUID at '" + path.string() + "'.");
    }
}

// Reads node_uuid.txt from the state directory. When the node has a certificate,
// its UUID is authoritative: it is stored on first start and must match afterwards.
[[nodiscard]] auto load_or_create_node_uuid(const std::filesystem::path& state_dir,
                                            const std::optional<std::string>& certificate_uuid) -> std::string
{
    const auto path = state_dir / "node_uuid.txt";
    std::optional<std::string> stored;
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        std::string existing;
        std::getline(in, existing);
        if (!existing.empty()) {
            stored = canonical_uuid(existing);
            if (!stored) {
                throw std::runtime_error("Node UUID file '" + path.string() + "' does not contain a valid UUID.");
            }
        }
    }

    if (certificate_uuid) {
        if (stored && *stored != *certificate_uuid) {
            throw std::runtime_error("Node state directory '" + state_dir.string() + "' belongs to node " + *stored +
                                     ", but the certificate identifies node " + *certificate_uuid + ".");
        }
        if (!stored) {
            write_node_uuid_file(path, *certificate_uuid);
        }
        return *certificate_uuid;
    }

    if (stored) {
        return *stored;
    }
    const auto generated = boost::uuids::to_string(boost::uuids::random_generator()());
    write_node_uuid_file(path, generated);
    return generated;
}

[[nodiscard]] auto select_identity_certificate(const RuntimeSchemaDefinition& definition) -> std::optional<std::filesystem::path>
{
    if (definition.server.enabled && definition.server.tls.enabled && !definition.server.tls.cert_file.empty()) {
        return std::filesystem::path(definition.server.tls.cert_file);
    }
    if (definition.client.enabled && definition.client.tls.enabled && !definition.client.tls.cert_file.empty()) {
        return std::filesystem::path(definition.client.tls.cert_file);
    }
    return std::nullopt;
}

[[nodiscard]] auto certificate_common_name(const std::filesystem::path& certificate_path) -> std::string
{
    const auto certificate_path_string = certificate_path.string();
    BIO* bio = BIO_new_file(certificate_path_string.c_str(), R"(r)");
    if (!bio) {
        throw std::runtime_error("Could not open node certificate: '" + certificate_path_string + "'.");
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> bio_guard(bio, BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(
        PEM_read_bio_X509(bio_guard.get(), nullptr, nullptr, nullptr),
        X509_free);
    if (!certificate) {
        const std::string message = "Could not parse PEM certificate: '" + certificate_path_string + "'.";
        throw std::runtime_error{message};
    }

    X509_NAME* subject = X509_get_subject_name(certificate.get());
    if (!subject) {
        throw std::runtime_error("Certificate subject is missing in '" + certificate_path_string + "'.");
    }

    const int cn_index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
    if (cn_index < 0) {
        throw std::runtime_error("Certificate does not contain a common name: '" + certificate_path_string + "'.");
    }

    X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(subject, cn_index);
    if (!cn_entry) {
        throw std::runtime_error("Could not read common name from certificate: '" + certificate_path_string + "'.");
    }

    ASN1_STRING* cn_string = X509_NAME_ENTRY_get_data(cn_entry);
    if (!cn_string) {
        throw std::runtime_error("Common name entry is empty in certificate: '" + certificate_path_string + "'.");
    }

    unsigned char* utf8 = nullptr;
    const int len = ASN1_STRING_to_UTF8(&utf8, cn_string);
    if (len < 0 || !utf8) {
        throw std::runtime_error("Failed to convert certificate common name to UTF-8: '" + certificate_path_string + "'.");
    }

    const auto utf8_deleter = [](unsigned char* ptr) { OPENSSL_free(ptr); };
    std::unique_ptr<unsigned char, decltype(utf8_deleter)> utf8_guard(utf8, utf8_deleter);
    return std::string(reinterpret_cast<char*>(utf8_guard.get()), static_cast<std::size_t>(len));
}

// Extracts node_uuid from the SAN URI spiffe://s7/node/<node_uuid>. The certificate
// must carry exactly one such identity.
[[nodiscard]] auto certificate_node_uuid(const std::filesystem::path& certificate_path) -> std::string
{
    constexpr std::string_view prefix = "spiffe://s7/node/";
    const auto certificate_path_string = certificate_path.string();
    BIO* bio = BIO_new_file(certificate_path_string.c_str(), "r");
    if (!bio) {
        throw std::runtime_error("Could not open node certificate: '" + certificate_path_string + "'.");
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> bio_guard(bio, BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(
        PEM_read_bio_X509(bio_guard.get(), nullptr, nullptr, nullptr),
        X509_free);
    if (!certificate) {
        throw std::runtime_error("Could not parse PEM certificate: '" + certificate_path_string + "'.");
    }

    const auto names_deleter = [](GENERAL_NAMES* names) { GENERAL_NAMES_free(names); };
    std::unique_ptr<GENERAL_NAMES, decltype(names_deleter)> names(
        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate.get(), NID_subject_alt_name, nullptr, nullptr)),
        names_deleter);

    std::set<std::string> node_uuids;
    const int count = names ? sk_GENERAL_NAME_num(names.get()) : 0;
    for (int index = 0; index < count; ++index) {
        const GENERAL_NAME* name = sk_GENERAL_NAME_value(names.get(), index);
        if (name->type != GEN_URI) {
            continue;
        }
        const ASN1_IA5STRING* uri = name->d.uniformResourceIdentifier;
        const std::string_view text(reinterpret_cast<const char*>(ASN1_STRING_get0_data(uri)),
                                    static_cast<std::size_t>(ASN1_STRING_length(uri)));
        if (!text.starts_with(prefix)) {
            continue;
        }
        const auto node_uuid = canonical_uuid(std::string(text.substr(prefix.size())));
        if (!node_uuid) {
            throw std::runtime_error("Certificate '" + certificate_path_string + "' has an invalid S7 node URI: '" +
                                     std::string(text) + "'.");
        }
        node_uuids.insert(*node_uuid);
    }

    if (node_uuids.empty()) {
        throw std::runtime_error("Certificate '" + certificate_path_string +
                                 "' has no SAN URI of the form spiffe://s7/node/<node_uuid>.");
    }
    if (node_uuids.size() > 1) {
        throw std::runtime_error("Certificate '" + certificate_path_string + "' names more than one S7 node.");
    }
    return *node_uuids.begin();
}

[[nodiscard]] auto certificate_fingerprint_sha256(const std::filesystem::path& certificate_path) -> std::string
{
    const auto certificate_path_string = certificate_path.string();
    BIO* bio = BIO_new_file(certificate_path_string.c_str(), R"(r)");
    if (!bio) {
        throw std::runtime_error("Could not open node certificate: '" + certificate_path_string + "'.");
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> bio_guard(bio, BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(
        PEM_read_bio_X509(bio_guard.get(), nullptr, nullptr, nullptr),
        X509_free);
    if (!certificate) {
        throw std::runtime_error("Could not parse PEM certificate: '" + certificate_path_string + "'.");
    }

    unsigned int digest_len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    if (X509_digest(certificate.get(), EVP_sha256(), digest, &digest_len) != 1 || digest_len == 0) {
        throw std::runtime_error("Failed to compute certificate fingerprint: '" + certificate_path_string + "'.");
    }

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex << std::setw(2) << static_cast<int>(digest[i]);
    }
    return hex.str();
}

// Takes an exclusive lock on the state directory for the life of the process,
// so that two runtimes never share one node identity and peer cache.
[[nodiscard]] auto lock_state_dir(const std::filesystem::path& state_dir) -> int
{
    const auto path = state_dir / "runtime.lock";
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::runtime_error("Could not open state directory lock '" + path.string() + "'.");
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        throw std::runtime_error("Node state directory '" + state_dir.string() +
                                 "' is in use by another runtime; give each node its own --state-dir.");
    }
    return fd;
}

[[nodiscard]] auto build_control_descriptor(const std::string& node_uuid,
                                            const transport::RuntimeNetEndpointPtr& endpoint,
                                            const RuntimeSchemaDefinition& definition,
                                            const std::optional<std::filesystem::path>& identity_cert = std::nullopt) -> ControlNodeDescriptor
{
    ControlNodeDescriptor descriptor;
    descriptor.node_uuid = node_uuid;
    descriptor.node_name = "node-" + node_uuid.substr(0, 8);
    descriptor.display_name = descriptor.node_name;
    descriptor.instance_id = boost::uuids::to_string(boost::uuids::random_generator()());
    if (identity_cert) {
        descriptor.identity_certificate_file = identity_cert->string();
        descriptor.identity_certificate_common_name = certificate_common_name(*identity_cert);
        descriptor.identity_certificate_fingerprint_sha256 = certificate_fingerprint_sha256(*identity_cert);
    }
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
    descriptor.libraries.emplace_back(std::move(registry_library));

    if (endpoint && endpoint->backend_name() == "beast") {
        descriptor.hardware_capabilities.push_back("net:websocket");
    }
    return descriptor;
}
} // namespace

// Compiles deployed graph fragments and swaps the running graph on commit.
class RuntimeService::GraphHost final : public DeploymentHost {
public:
    explicit GraphHost(RuntimeService& service) : service_(service) {}

    struct StagedGraph final : StagedConfiguration {
        std::unique_ptr<NodeRuntime> runtime;
        std::vector<transport::DataPeer> data_peers;
    };

    auto stage(const NodePart& part) -> std::unique_ptr<StagedConfiguration> override
    {
        const auto& node_schema = part.schema;
        // A deployment carries a graph only; node settings stay with the node.
        for (const auto* key : {"websocket", "simulation", "execution", "control", "clock"}) {
            if (node_schema.contains(key)) {
                throw std::invalid_argument(std::string{"A deployed graph must not contain node settings ('"} + key + "').");
            }
        }
        const auto definition = JsonSchemaLoader::load_from_json(node_schema);
        const auto schema = build_schema_from_definition(definition);
        auto staged = std::make_unique<StagedGraph>();
        staged->runtime = std::make_unique<NodeRuntime>(
            Compiler::compile(*schema, service_.runtime_instance_uuid_, create_schema_instance_uuid()));
        staged->runtime->set_model_time_step_ns(part.model_time_step_ns);
        staged->data_peers = part.data_peers;
        return staged;
    }

    void activate(StagedConfiguration& staged) override
    {
        auto& graph = dynamic_cast<StagedGraph&>(staged);
        if (!graph.runtime) {
            throw std::logic_error("Staged graph was already activated.");
        }
        {
            // The previous graph is destroyed under the lock: its network
            // elements release their streams before the new graph's first
            // step opens them again (and continues them).
            std::lock_guard<std::mutex> lock(service_.graph_mutex_);
            auto previous = std::exchange(service_.graph_, std::move(graph.runtime));
            previous.reset();
        }
        if (const auto beast = std::dynamic_pointer_cast<transport::BeastRuntimeNetEndpoint>(service_.network_endpoint_)) {
            const auto& definition = service_.definition_;
            beast->set_data_peers(graph.data_peers, definition.client.tls.enabled ? definition.client.tls : definition.server.tls);
        }
    }

    auto execution_state() const -> std::string override
    {
        return service_.stepping_.load() ? "stepping" : "not_stepping";
    }

private:
    RuntimeService& service_;
};

RuntimeSchemaDefinition JsonSchemaLoader::load_from_json(const boost::json::value& json) {
    return parse_schema_definition(json);
}

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

RuntimeService::RuntimeService(RuntimeSchemaDefinition definition, std::filesystem::path state_dir)
    : definition_(std::move(definition))
    , state_dir_(std::move(state_dir))
    , runtime_instance_uuid_()
    , control_plane_(nullptr)
    , network_endpoint_(nullptr)
{
    if (state_dir_.empty()) {
        throw std::invalid_argument("RuntimeService requires a node state directory.");
    }
    std::filesystem::create_directories(state_dir_);
    state_dir_lock_fd_ = lock_state_dir(state_dir_);

    try {
        // Producer sequence reservations live in the node state directory.
        if (definition_.server.enabled || definition_.client.enabled) {
            network_endpoint_ = std::make_shared<s7::transport::BeastRuntimeNetEndpoint>(transport::TransportLimits{}, state_dir_.string());
        } else {
            network_endpoint_ = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        }

        const auto identity_cert = select_identity_certificate(definition_);
        runtime_instance_uuid_ = load_or_create_node_uuid(
            state_dir_,
            identity_cert ? std::optional<std::string>{certificate_node_uuid(*identity_cert)} : std::nullopt);

        if (const auto beast = std::dynamic_pointer_cast<transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
            beast->set_node_uuid(runtime_instance_uuid_); // the identity plain WebSocket peers know this node by
        }
        graph_host_ = std::make_shared<GraphHost>(*this);
        control_plane_ = std::make_shared<ControlPlaneMvp>(
            build_control_descriptor(runtime_instance_uuid_, network_endpoint_, definition_, identity_cert),
            ControlPlaneOptions{
                .storage_dir = state_dir_,
                .authorization = definition_.control,
                .deployment_host = graph_host_,
                .log = [](const std::string& line) { std::cerr << line << '\n'; },
                .network = network_endpoint_,
                .on_stream_recovered = [this] { ++stream_recoveries_; },
                .clock_sync = [this] { return clock_sync(); },
                .max_clock_error_ns = static_cast<std::int64_t>(definition_.clock.max_error_ms * 1e6),
            });
    } catch (...) {
        ::close(state_dir_lock_fd_);
        throw;
    }

    if (const auto beast_endpoint = std::dynamic_pointer_cast<s7::transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
        std::weak_ptr<ControlPlaneMvp> weak_control = control_plane_;
        beast_endpoint->set_control_request_handler([weak_control](const transport::ControlPeer& peer,
                                                                   const std::string& request_json) {
            const auto locked = weak_control.lock();
            if (!locked) {
                return std::string{"{\"type\":\"ERROR\",\"protocol_version\":1,\"code\":\"INTERNAL_ERROR\",\"message\":\"Control service unavailable\"}"};
            }
            return locked->handle_request_json(peer, request_json);
        });
    }
    RuntimeNetContext::set_endpoint(network_endpoint_);
}

RuntimeService::~RuntimeService()
{
    {
        // The graph's network elements close their streams before the endpoint goes.
        std::lock_guard<std::mutex> lock(graph_mutex_);
        graph_.reset();
    }

    // Only clear global runtime endpoint context if it still points to this service's endpoint.
    if (RuntimeNetContext::endpoint() == network_endpoint_) {
        RuntimeNetContext::set_endpoint(nullptr);
    }

    if (state_dir_lock_fd_ >= 0) {
        ::close(state_dir_lock_fd_);
    }
}

void RuntimeService::initialize_graph() {
    // A deployment committed before the node restarted takes precedence over
    // the startup schema.
    if (control_plane_->restore_deployment() == ControlPlaneMvp::RestoreOutcome::Restored) {
        return;
    }
    const auto schema = build_schema_from_definition(definition_);
    auto runtime = std::make_unique<NodeRuntime>(
        Compiler::compile(*schema, runtime_instance_uuid_, create_schema_instance_uuid()));
    // A simulation block defines the model-time base; otherwise steps are untimed.
    if (definition_.simulation.enabled && definition_.simulation.sample_rate_hz > 0) {
        runtime->set_model_time_step_ns(static_cast<std::int64_t>(
            std::llround(1e9 / static_cast<double>(definition_.simulation.sample_rate_hz))));
    }
    std::lock_guard<std::mutex> lock(graph_mutex_);
    graph_ = std::move(runtime);
}

void RuntimeService::step() {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    if (!graph_) {
        throw std::logic_error("RuntimeService::step() called before initialize_graph().");
    }
    graph_->step();
}

std::uint64_t RuntimeService::step_count() const {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    return graph_ ? graph_->step_count() : 0;
}

double RuntimeService::active_real_output(const std::string& element_id, const std::size_t port) const {
    std::lock_guard<std::mutex> lock(graph_mutex_);
    if (!graph_) {
        throw std::logic_error("No active graph.");
    }
    return graph_->real_output_of(element_id, port);
}

void RuntimeService::set_stepping(const bool stepping) {
    stepping_.store(stepping);
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
    const auto identity_cert = select_identity_certificate(definition_);
    out << "runtime.schema=" << definition_.name << '\n';
    out << "runtime.instance_uuid=" << runtime_instance_uuid_ << '\n';
    out << "runtime.state_dir=" << state_dir_.string() << '\n';
    out << "runtime.node_identity.source=" << (identity_cert ? "certificate" : "persistent_storage") << '\n';
    if (identity_cert) {
        out << "runtime.node_identity.certificate_file=" << identity_cert->string() << '\n';
        out << "runtime.node_identity.certificate_common_name=" << certificate_common_name(*identity_cert) << '\n';
        out << "runtime.node_identity.certificate_fingerprint_sha256=" << certificate_fingerprint_sha256(*identity_cert) << '\n';
    }
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
    out << "runtime.execution.step_rate_hz=" << definition_.execution.step_rate_hz << '\n';
}

void RuntimeService::set_clock_source(std::function<clock::SyncStatus()> source)
{
    clock_source_ = std::move(source);
}

auto RuntimeService::clock_sync() const -> clock::SyncStatus
{
    if (clock_source_) {
        return clock_source_();
    }
    if (definition_.clock.assume_synchronized) {
        return clock::SyncStatus{.synchronized = true, .max_error_ns = 0, .estimated_error_ns = 0, .source = "assumed"};
    }
    return clock::read_kernel_sync();
}

bool RuntimeService::check_clock(std::ostream& out, const bool force)
{
    const auto now = std::chrono::steady_clock::now();
    if (!force && clock_usable_ && now - clock_checked_ < std::chrono::seconds(1)) {
        return *clock_usable_;
    }
    clock_checked_ = now;
    const auto sync = clock_sync();
    const auto bound_ns = static_cast<std::int64_t>(definition_.clock.max_error_ms * 1e6);
    const bool usable = clock::usable(sync, bound_ns);
    if (clock_usable_ != usable) {
        const auto detail = "synchronized=" + std::string(sync.synchronized ? "true" : "false") +
                            " max_error_ns=" + std::to_string(sync.max_error_ns) + " bound_ns=" + std::to_string(bound_ns) +
                            " source=" + sync.source;
        out << "runtime.clock.usable=" << (usable ? "true " : "false ") << detail << '\n';
        if (clock_usable_ && !usable && network_endpoint_) {
            network_endpoint_->note_on_all_streams(stream::FaultCode::ClockUnsynchronized, detail);
        }
        clock_usable_ = usable;
    }
    return usable;
}

void RuntimeService::report_transport_errors(std::ostream& out) const {
    if (!network_endpoint_) {
        return;
    }
    for (const auto& error : network_endpoint_->take_transport_errors()) {
        out << "runtime.transport.error=" << error << '\n';
    }
}

void RuntimeService::report_transport_stats(std::ostream& out) const {
    if (!network_endpoint_) {
        return;
    }
    const auto stats = network_endpoint_->stats();
    out << "runtime.transport.data_connections=" << stats.data_connections << '\n'
        << "runtime.transport.control_connections=" << stats.control_connections << '\n'
        << "runtime.transport.connections_rejected=" << stats.connections_rejected << '\n'
        << "runtime.transport.messages_sent=" << stats.messages_sent << '\n'
        << "runtime.transport.messages_received=" << stats.messages_received << '\n'
        << "runtime.transport.malformed_messages=" << stats.malformed_messages << '\n';
    for (const auto& status : network_endpoint_->stream_statuses()) {
        out << "runtime.stream." << wire::to_string(status.stream_id) << '='
            << (status.role == transport::StreamRole::Producer ? "producer" : "consumer") << ' ' << signal_type_name(status.type)
            << " state=" << status.state << " fault=" << stream::to_string(status.fault) << " packets=" << status.packets
            << " next_sequence=" << status.next_sequence << '\n';
    }
}

const std::string& RuntimeService::runtime_instance_uuid() const {
    return runtime_instance_uuid_;
}

std::string RuntimeService::create_schema_instance_uuid() {
    return boost::uuids::to_string(boost::uuids::random_generator()());
}

auto RuntimeService::network_endpoint() const -> transport::RuntimeNetEndpointPtr {
    return network_endpoint_;
}

auto RuntimeService::handle_control_request_json(const transport::ControlPeer& peer, const std::string& request_json)
    -> std::string
{
    if (!control_plane_) {
        throw std::runtime_error("Control plane is not initialized.");
    }
    return control_plane_->handle_request_json(peer, request_json);
}

void RuntimeService::add_seed_peer(const std::string& endpoint)
{
    if (!control_plane_) {
        return;
    }
    control_plane_->add_seed_peer(endpoint);
}

std::uint16_t RuntimeService::start_websocket_server() const {
    if (!definition_.server.enabled) {
        return 0;
    }
    if (const auto beast_endpoint = std::dynamic_pointer_cast<s7::transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
        return beast_endpoint->configure_server(definition_.server.port, definition_.server.tls, definition_.server.bind_address);
    }
    return 0;
}

void RuntimeService::connect_websocket_client() const {
    if (!definition_.client.enabled) {
        return;
    }
    if (definition_.client.url.empty()) {
        throw std::invalid_argument("WebSocket client is enabled but URL is empty.");
    }
    if (const auto beast_endpoint = std::dynamic_pointer_cast<s7::transport::BeastRuntimeNetEndpoint>(network_endpoint_)) {
        beast_endpoint->configure_client(definition_.client.url, definition_.client.tls);
    }
}

StepLoopStats run_step_loop(const std::function<void()>& step,
                            const double step_rate_hz,
                            const std::atomic<bool>& stop_requested,
                            const std::optional<std::chrono::steady_clock::time_point> deadline,
                            const std::function<void()>& housekeeping)
{
    if (!std::isfinite(step_rate_hz) || step_rate_hz <= 0.0) {
        throw std::invalid_argument("Step loop requires a positive step rate.");
    }

    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / step_rate_hz));
    // Longest single sleep, so that a stop request is noticed promptly at low rates.
    constexpr auto max_sleep = std::chrono::milliseconds(50);
    // When the loop falls further behind than this, it drops the backlog instead of bursting.
    const auto max_lag = std::max<clock::duration>(period, std::chrono::milliseconds(100));

    constexpr auto housekeeping_interval = std::chrono::milliseconds(100);

    StepLoopStats stats;
    const auto started = clock::now();
    auto next_step = started;
    auto next_housekeeping = started + housekeeping_interval;
    while (true) {
        if (housekeeping && clock::now() >= next_housekeeping) {
            housekeeping();
            next_housekeeping = clock::now() + housekeeping_interval;
        }
        if (stop_requested.load()) {
            stats.stop_reason = StepLoopStats::StopReason::StopRequested;
            break;
        }
        const auto now = clock::now();
        if (deadline && now >= *deadline) {
            stats.stop_reason = StepLoopStats::StopReason::Deadline;
            break;
        }
        if (now < next_step) {
            auto wake = std::min(next_step, now + max_sleep);
            if (deadline) {
                wake = std::min(wake, *deadline);
            }
            std::this_thread::sleep_until(wake);
            continue;
        }

        step();
        ++stats.steps;
        next_step += period;
        if (clock::now() - next_step > max_lag) {
            next_step = clock::now();
        }
    }

    stats.elapsed_seconds = std::chrono::duration<double>(clock::now() - started).count();
    return stats;
}

} // namespace s7
