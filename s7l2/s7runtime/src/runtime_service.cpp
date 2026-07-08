#include "s7/runtime_service.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/system/error_code.hpp>

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

    return definition;
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

RuntimeService::RuntimeService(RuntimeSchemaDefinition definition) : definition_(std::move(definition)) {}

const RuntimeSchemaDefinition& RuntimeService::schema() const {
    return definition_;
}

void RuntimeService::print_summary(std::ostream& out) const {
    out << "runtime.schema=" << definition_.name << '\n';
    out << "runtime.elements=" << definition_.elements.size() << '\n';
    out << "runtime.connections=" << definition_.connections.size() << '\n';
    out << "runtime.websocket.server.enabled=" << (definition_.server.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.server.port=" << definition_.server.port << '\n';
    out << "runtime.websocket.client.enabled=" << (definition_.client.enabled ? "true" : "false") << '\n';
    out << "runtime.websocket.client.url=" << definition_.client.url << '\n';
}

void RuntimeService::start_websocket_server() {
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

void RuntimeService::connect_websocket_client() {
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


