#ifndef S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP
#define S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP

#include <cstdint>
#include <optional>
#include <ostream>
#include <filesystem>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "s7/core.hpp"

namespace s7 {

struct JsonElementDefinition {
    std::string id;
    std::string type;
    boost::json::object params;
    boost::json::value nested_schema;
};

struct SchemaPortBinding {
    std::string name;
    std::string type;
    std::string element;
    std::uint64_t port{};
};

struct JsonConnectionDefinition {
    std::string from_element;
    std::uint64_t from_port{};
    std::string to_element;
    std::uint64_t to_port{};
};

struct WebSocketServerConfig {
    bool enabled{};
    std::uint16_t port{};
};

struct WebSocketClientConfig {
    bool enabled{};
    std::string url;
};

struct SimulationConfig {
    bool enabled{};
    std::uint64_t periods{};
    std::uint64_t sample_rate_hz{};
    double frequency_hz{};
};

struct RuntimeSchemaDefinition {
    std::string name;
    std::vector<JsonElementDefinition> elements;
    std::vector<JsonConnectionDefinition> connections;
    std::vector<SchemaPortBinding> exposed_inputs;
    std::vector<SchemaPortBinding> exposed_outputs;
    WebSocketServerConfig server;
    WebSocketClientConfig client;
    SimulationConfig simulation;
};

class JsonSchemaLoader {
public:
    static RuntimeSchemaDefinition load_from_file(const std::string& file_path);
};

class RuntimeService {
public:
    explicit RuntimeService(RuntimeSchemaDefinition definition);

    [[nodiscard]] const RuntimeSchemaDefinition& schema() const;
    [[nodiscard]] bool has_simulation() const;
    [[nodiscard]] std::uint64_t simulation_step_count() const;
    [[nodiscard]] std::vector<std::filesystem::path> output_files() const;
    [[nodiscard]] s7::Schema build_schema() const;
    [[nodiscard]] const std::string& runtime_instance_uuid() const;
    [[nodiscard]] std::string create_schema_instance_uuid() const;
    void print_summary(std::ostream& out) const;

    void start_websocket_server() const;
    void connect_websocket_client() const;

private:
    RuntimeSchemaDefinition definition_;
    std::string runtime_instance_uuid_;
};

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP
