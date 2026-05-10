#include <folly/io/async/ScopedEventBaseThread.h>

#include <nlohmann/json.hpp>

#include <rsocket/RSocket.h>
#include <rsocket/transports/tcp/TcpConnectionFactory.h>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    int port = 5577;
    std::string command = "hello";
    std::string schema_path;
    std::string schema_json;
    std::string schema_handle;
    std::string run_id;
    long ticks = 1;
    std::string raw_method;
    std::string raw_params = "{}";
};

void printUsage(const char* exe)
{
    std::cout
        << "s7ctl - S7 runtime control CLI\n\n"
        << "Usage:\n"
        << "  " << exe << " [--host <host>] [--port <port>] hello\n"
        << "  " << exe << " [--host <host>] [--port <port>] types\n"
        << "  " << exe << " [--host <host>] [--port <port>] status\n"
        << "  " << exe << " [--host <host>] [--port <port>] load-file --schema-path <path>\n"
        << "  " << exe << " [--host <host>] [--port <port>] load-inline --schema-json <json>\n"
        << "  " << exe << " [--host <host>] [--port <port>] validate --schema-handle <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] run --schema-handle <id> [--ticks <n>]\n"
        << "  " << exe << " [--host <host>] [--port <port>] pause --run-id <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] resume --run-id <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] stop --run-id <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] reset --schema-handle <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] unload --schema-handle <id>\n"
        << "  " << exe << " [--host <host>] [--port <port>] raw --method <name> [--params <json>]\n"
        << "\n"
        << "Examples:\n"
        << "  " << exe << " hello\n"
        << "  " << exe << " status\n"
        << "  " << exe << " load-file --schema-path ./s7main/examples/io_file_real_sink.json\n"
        << "  " << exe << " run --schema-handle schema-1 --ticks 10\n"
        << "  " << exe << " raw --method schema.run --params '{\"schema_handle\":\"schema-1\",\"mode\":{\"ticks\":10}}'\n";
}

int parsePort(const std::string& v)
{
    try {
        const int port = std::stoi(v);
        if (port < 1 || port > 65535) {
            throw std::runtime_error("port out of range");
        }
        return port;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid --port value: " + v);
    }
}

Options parseArgs(int argc, char* argv[])
{
    Options opt;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (arg == "--host") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --host");
            opt.host = argv[++i];
        } else if (arg == "--port") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --port");
            opt.port = parsePort(argv[++i]);
        } else if (arg == "--method") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --method");
            opt.raw_method = argv[++i];
        } else if (arg == "--params") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --params");
            opt.raw_params = argv[++i];
        } else if (arg == "--schema-path") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema-path");
            opt.schema_path = argv[++i];
        } else if (arg == "--schema-json") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema-json");
            opt.schema_json = argv[++i];
        } else if (arg == "--schema-handle") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema-handle");
            opt.schema_handle = argv[++i];
        } else if (arg == "--run-id") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --run-id");
            opt.run_id = argv[++i];
        } else if (arg == "--ticks") {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --ticks");
            opt.ticks = std::stol(argv[++i]);
            if (opt.ticks <= 0) throw std::runtime_error("--ticks must be > 0");
        } else if (arg == "hello" || arg == "types" || arg == "status" ||
                   arg == "load-file" || arg == "load-inline" || arg == "validate" ||
                   arg == "run" || arg == "pause" || arg == "resume" || arg == "stop" ||
                   arg == "reset" || arg == "unload" || arg == "raw") {
            opt.command = std::string(arg);
        } else {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (opt.command == "raw" && opt.raw_method.empty()) {
        throw std::runtime_error("raw command requires --method <name>");
    }
    if (opt.command == "load-file" && opt.schema_path.empty()) {
        throw std::runtime_error("load-file requires --schema-path <path>");
    }
    if (opt.command == "load-inline" && opt.schema_json.empty()) {
        throw std::runtime_error("load-inline requires --schema-json <json>");
    }
    if ((opt.command == "validate" || opt.command == "run" ||
         opt.command == "reset" || opt.command == "unload") &&
        opt.schema_handle.empty()) {
        throw std::runtime_error(opt.command + " requires --schema-handle <id>");
    }
    if ((opt.command == "pause" || opt.command == "resume" || opt.command == "stop") &&
        opt.run_id.empty()) {
        throw std::runtime_error(opt.command + " requires --run-id <id>");
    }

    return opt;
}

nlohmann::json makeRequest(const Options& opt)
{
    static long next_id = 1;
    const long id = next_id++;

    if (opt.command == "hello") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "runtime.hello"},
                              {"params", {"client_name", "s7ctl"}}};
    }
    if (opt.command == "types") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "runtime.types.list"},
                              {"params", nlohmann::json::object()}};
    }
    if (opt.command == "status") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "runtime.status"},
                              {"params", nlohmann::json::object()}};
    }
    if (opt.command == "load-file") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.load"},
                              {"params", {
                                  {"source", {
                                      {"kind", "filesystem"},
                                      {"path", opt.schema_path}
                                  }}
                              }}};
    }
    if (opt.command == "load-inline") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.load"},
                              {"params", {
                                  {"source", {
                                      {"kind", "inline"},
                                      {"schema_json", opt.schema_json}
                                  }}
                              }}};
    }
    if (opt.command == "validate") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.validate"},
                              {"params", {{"schema_handle", opt.schema_handle}}}};
    }
    if (opt.command == "run") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.run"},
                              {"params", {
                                  {"schema_handle", opt.schema_handle},
                                  {"mode", {{"ticks", opt.ticks}}}
                              }}};
    }
    if (opt.command == "pause") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.pause"},
                              {"params", {{"run_id", opt.run_id}}}};
    }
    if (opt.command == "resume") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.resume"},
                              {"params", {{"run_id", opt.run_id}}}};
    }
    if (opt.command == "stop") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.stop"},
                              {"params", {{"run_id", opt.run_id}}}};
    }
    if (opt.command == "reset") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.reset"},
                              {"params", {{"schema_handle", opt.schema_handle}}}};
    }
    if (opt.command == "unload") {
        return nlohmann::json{{"jsonrpc", "2.0"},
                              {"id", id},
                              {"method", "schema.unload"},
                              {"params", {{"schema_handle", opt.schema_handle}}}};
    }

    return nlohmann::json{{"jsonrpc", "2.0"},
                          {"id", id},
                          {"method", opt.raw_method},
                          {"params", nlohmann::json::parse(opt.raw_params)}};
}

std::string requestResponse(const Options& opt, const nlohmann::json& request)
{
    folly::SocketAddress address;
    address.setFromHostPort(opt.host, opt.port);

    folly::ScopedEventBaseThread worker;
    auto client = rsocket::RSocket::createConnectedClient(
                      std::make_unique<rsocket::TcpConnectionFactory>(
                          *worker.getEventBase(), std::move(address)))
                      .get();

    std::promise<std::string> result_promise;
    auto result_future = result_promise.get_future();

    client->getRequester()
        ->requestResponse(rsocket::Payload(request.dump()))
        ->subscribe(
            [&result_promise](rsocket::Payload p) {
                result_promise.set_value(p.moveDataToString());
            },
            [&result_promise](folly::exception_wrapper ex) {
                try {
                    ex.throw_exception();
                } catch (const std::exception& e) {
                    result_promise.set_exception(std::make_exception_ptr(
                        std::runtime_error(std::string("request failed: ") + e.what())));
                }
            });

    if (result_future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        throw std::runtime_error("request timed out");
    }

    return result_future.get();
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const Options opt = parseArgs(argc, argv);
        const nlohmann::json request = makeRequest(opt);
        const std::string response_raw = requestResponse(opt, request);

        const auto response = nlohmann::json::parse(response_raw);
        std::cout << response.dump(2) << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7ctl error: " << ex.what() << '\n';
        return 1;
    }
}



