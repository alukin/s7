#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <boost/json.hpp>

namespace s7 {

struct JsonElementDefinition {
    std::string id;
    std::string type;
    boost::json::object params;
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

struct RuntimeSchemaDefinition {
    std::string name;
    std::vector<JsonElementDefinition> elements;
    std::vector<JsonConnectionDefinition> connections;
    WebSocketServerConfig server;
    WebSocketClientConfig client;
};

class JsonSchemaLoader {
public:
    static RuntimeSchemaDefinition load_from_file(const std::string& file_path);
};

class RuntimeService {
public:
    explicit RuntimeService(RuntimeSchemaDefinition definition);

    [[nodiscard]] const RuntimeSchemaDefinition& schema() const;
    void print_summary(std::ostream& out) const;

    void start_websocket_server();
    void connect_websocket_client();

private:
    RuntimeSchemaDefinition definition_;
};

} // namespace s7

