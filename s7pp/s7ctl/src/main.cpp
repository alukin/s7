#include "s7ctl_json_dto.hpp"

#include <boost/json.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

struct Options {
    std::string host = "127.0.0.1";
    int port = 5577;
    std::string path = "/";
    std::string command = "hello";
    std::string schema_path;
    std::string schema_json;
    std::string schema_handle;
    std::string run_id;
    long ticks = 1;
    std::string raw_method;
    std::string raw_params = "{}";
    std::string topics_csv = "schema,run,runtime";
    long event_count = 1;
};

auto printUsage(const char* exe) -> void
{
    std::cout
        << "s7ctl - S7 websocket control CLI\n\n"
        << "Usage:\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] hello\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] types\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] status\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] load-file --schema-path <path>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] load-inline --schema-json <json>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] validate --schema-handle <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] run --schema-handle <id> [--ticks <n>]\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] pause --run-id <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] resume --run-id <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] stop --run-id <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] reset --schema-handle <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] unload --schema-handle <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] raw --method <name> [--params <json>]\n"
        << "  " << exe << " [--host <host>] [--port <port>] [--path <ws_path>] events [--topics <csv>] [--count <n>]\n";
}

auto parsePort(const std::string& v) -> int
{
    try {
        const int parsed = std::stoi(v);
        if (parsed < 1 || parsed > 65535) {
            throw std::runtime_error("port out of range");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid --port value: " + v);
    }
}

auto parseTicks(const std::string& v) -> long
{
    try {
        const long parsed = std::stol(v);
        if (parsed <= 0) {
            throw std::runtime_error("--ticks must be > 0");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid --ticks value: " + v);
    }
}

auto parseCount(const std::string& v) -> long
{
    try {
        const long parsed = std::stol(v);
        if (parsed <= 0) {
            throw std::runtime_error("--count must be > 0");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid --count value: " + v);
    }
}

auto splitCsv(const std::string& csv) -> std::vector<std::string>
{
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

auto parseArgs(int argc, char** argv) -> Options
{
    Options opt;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (arg == "--host") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --host");
            opt.host = argv[++i];
            continue;
        }
        if (arg == "--port") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --port");
            opt.port = parsePort(argv[++i]);
            continue;
        }
        if (arg == "--path") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --path");
            opt.path = argv[++i];
            if (opt.path.empty() || opt.path.front() != '/') {
                opt.path = "/" + opt.path;
            }
            continue;
        }
        if (arg == "--method") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --method");
            opt.raw_method = argv[++i];
            continue;
        }
        if (arg == "--params") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --params");
            opt.raw_params = argv[++i];
            continue;
        }
        if (arg == "--schema-path") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema-path");
            opt.schema_path = argv[++i];
            continue;
        }
        if (arg == "--schema-json") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema-json");
            opt.schema_json = argv[++i];
            continue;
        }
        if (arg == "--schema-handle") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema-handle");
            opt.schema_handle = argv[++i];
            continue;
        }
        if (arg == "--run-id") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --run-id");
            opt.run_id = argv[++i];
            continue;
        }
        if (arg == "--ticks") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --ticks");
            opt.ticks = parseTicks(argv[++i]);
            continue;
        }
        if (arg == "--topics") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --topics");
            opt.topics_csv = argv[++i];
            continue;
        }
        if (arg == "--count") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --count");
            opt.event_count = parseCount(argv[++i]);
            continue;
        }

        if (arg == "hello" || arg == "types" || arg == "status" ||
            arg == "load-file" || arg == "load-inline" || arg == "validate" ||
            arg == "run" || arg == "pause" || arg == "resume" || arg == "stop" ||
            arg == "reset" || arg == "unload" || arg == "raw" || arg == "events") {
            opt.command = std::string(arg);
            continue;
        }

        throw std::runtime_error("Unknown argument: " + std::string(arg));
    }

    if (opt.command == "raw" && opt.raw_method.empty()) {
        throw std::runtime_error("raw command requires --method <name>");
    }
    if (opt.command == "events" && splitCsv(opt.topics_csv).empty()) {
        throw std::runtime_error("events command requires non-empty --topics");
    }
    if (opt.command == "load-file" && opt.schema_path.empty()) {
        throw std::runtime_error("load-file requires --schema-path <path>");
    }
    if (opt.command == "load-inline" && opt.schema_json.empty()) {
        throw std::runtime_error("load-inline requires --schema-json <json>");
    }
    if ((opt.command == "validate" || opt.command == "run" || opt.command == "reset" || opt.command == "unload") &&
        opt.schema_handle.empty()) {
        throw std::runtime_error(opt.command + " requires --schema-handle <id>");
    }
    if ((opt.command == "pause" || opt.command == "resume" || opt.command == "stop") && opt.run_id.empty()) {
        throw std::runtime_error(opt.command + " requires --run-id <id>");
    }

    return opt;
}

auto makeRequest(const Options& opt) -> boost::json::value
{
    static long next_id = 1;
    const long id = next_id++;

    if (opt.command == "hello") {
        return s7::ctl::dto::RequestFactory::hello(id);
    }
    if (opt.command == "types") {
        return s7::ctl::dto::RequestFactory::types(id);
    }
    if (opt.command == "status") {
        return s7::ctl::dto::RequestFactory::status(id);
    }
    if (opt.command == "load-file") {
        return s7::ctl::dto::RequestFactory::loadFilesystem(id, opt.schema_path);
    }
    if (opt.command == "load-inline") {
        return s7::ctl::dto::RequestFactory::loadInline(id, opt.schema_json);
    }
    if (opt.command == "validate") {
        return s7::ctl::dto::RequestFactory::validate(id, opt.schema_handle);
    }
    if (opt.command == "run") {
        return s7::ctl::dto::RequestFactory::run(id, opt.schema_handle, opt.ticks);
    }
    if (opt.command == "pause") {
        return s7::ctl::dto::RequestFactory::pause(id, opt.run_id);
    }
    if (opt.command == "resume") {
        return s7::ctl::dto::RequestFactory::resume(id, opt.run_id);
    }
    if (opt.command == "stop") {
        return s7::ctl::dto::RequestFactory::stop(id, opt.run_id);
    }
    if (opt.command == "reset") {
        return s7::ctl::dto::RequestFactory::reset(id, opt.schema_handle);
    }
    if (opt.command == "unload") {
        return s7::ctl::dto::RequestFactory::unload(id, opt.schema_handle);
    }
    if (opt.command == "raw") {
        return s7::ctl::dto::RequestFactory::raw(id, opt.raw_method, opt.raw_params);
    }
    if (opt.command == "events") {
        return s7::ctl::dto::RequestFactory::eventsSubscribe(id, splitCsv(opt.topics_csv));
    }

    throw std::runtime_error("Unsupported command");
}

auto streamEvents(const Options& opt, const boost::json::value& subscribe_request) -> int
{
    asio::io_context ioc;
    tcp::resolver resolver(ioc);
    websocket::stream<tcp::socket> ws(ioc);

    const auto results = resolver.resolve(opt.host, std::to_string(opt.port));
    asio::connect(ws.next_layer(), results.begin(), results.end());
    ws.handshake(opt.host + ":" + std::to_string(opt.port), opt.path);

    ws.text(true);
    ws.write(asio::buffer(boost::json::serialize(subscribe_request)));

    beast::flat_buffer ack_buffer;
    ws.read(ack_buffer);
    const auto ack = boost::json::parse(beast::buffers_to_string(ack_buffer.data()));
    std::cout << boost::json::serialize(ack) << '\n';

    const auto ack_dto = s7::ctl::dto::SubscribeAck::fromJson(ack);
    uint64_t subscription_id = ack_dto.subscriptionId;

    for (long i = 0; i < opt.event_count; ++i) {
        beast::flat_buffer event_buffer;
        ws.read(event_buffer);
        const auto event_json = boost::json::parse(beast::buffers_to_string(event_buffer.data()));
        std::cout << boost::json::serialize(event_json) << '\n';
    }

    if (subscription_id != 0) {
        static long unsubscribe_id = 100000;
        const boost::json::value unsubscribe_request =
            s7::ctl::dto::RequestFactory::eventsUnsubscribe(++unsubscribe_id, subscription_id);
        ws.write(asio::buffer(boost::json::serialize(unsubscribe_request)));
        beast::flat_buffer unsub_buffer;
        ws.read(unsub_buffer);
        const auto unsub = boost::json::parse(beast::buffers_to_string(unsub_buffer.data()));
        std::cout << boost::json::serialize(unsub) << '\n';
    }

    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);
    return 0;
}

auto requestResponse(const Options& opt, const boost::json::value& request) -> std::string
{
    asio::io_context ioc;
    tcp::resolver resolver(ioc);
    websocket::stream<tcp::socket> ws(ioc);

    const auto results = resolver.resolve(opt.host, std::to_string(opt.port));
    asio::connect(ws.next_layer(), results.begin(), results.end());
    ws.handshake(opt.host + ":" + std::to_string(opt.port), opt.path);

    const auto payload = boost::json::serialize(request);
    ws.text(true);
    ws.write(asio::buffer(payload));

    beast::flat_buffer buffer;
    ws.read(buffer);

    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);

    return beast::buffers_to_string(buffer.data());
}

} // namespace

auto main(int argc, char** argv) -> int
{
    try {
        const Options opt = parseArgs(argc, argv);
        const boost::json::value request = makeRequest(opt);

        if (opt.command == "events") {
            return streamEvents(opt, request);
        }

        const std::string response_raw = requestResponse(opt, request);

        const auto response = boost::json::parse(response_raw);
        std::cout << boost::json::serialize(response) << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7ctl error: " << ex.what() << '\n';
        return 1;
    }
}



