#include "s7factory.hpp"
#include "s7netlist.hpp"
#include "s7control_server.hpp"
#include "s7json_rpc.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

using namespace s7;

namespace {

struct CliOptions {
    std::string schema_path;
    std::optional<long> ticks;
    std::optional<double> seconds;
    bool validate_only = false;
    bool list_types = false;
    bool control_stdio = false;
    bool server = false;
    int server_port = 5577;
    bool help = false;
};

auto printUsage(const char* exe) -> void
{
    std::cout
        << "S7 Computation Node (CLI v0)\n\n"
        << "Usage:\n"
        << "  " << exe << " --schema <path> [--ticks <n> | --seconds <s>] [--validate-only]\n"
        << "  " << exe << " --list-types\n"
        << "  " << exe << " --control-stdio\n"
        << "  " << exe << " --server [--port <n>]\n"
        << "  " << exe << " --help\n\n"
        << "Options:\n"
        << "  --schema <path>      Path to netlist JSON file\n"
        << "  --ticks <n>          Run schema for n ticks\n"
        << "  --seconds <s>        Run schema for s seconds (uses dt from netlist)\n"
        << "  --validate-only      Load and validate only (no execution)\n"
        << "  --list-types         List registered element types and exit\n"
        << "  --control-stdio      Start JSON control dispatcher on stdin/stdout (one JSON request per line)\n"
        << "  --server             Start RSocket control server\n"
        << "  --port <n>           Control server port (default: 5577)\n"
        << "  --help, -h           Show this help\n";
}

auto parsePort(const std::string& v) -> int
{
    try {
        const long port = std::stol(v);
        if (port < 1 || port > 65535) {
            throw std::runtime_error("--port must be in 1..65535");
        }
        return static_cast<int>(port);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid value for --port: " + v);
    }
}

auto parseLong(const std::string& v, const char* opt_name) -> long
{
    try {
        const long parsed = std::stol(v);
        if (parsed <= 0) {
            throw std::runtime_error(std::string(opt_name) + " must be > 0");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid value for ") + opt_name + ": " + v);
    }
}

auto parseDouble(const std::string& v, const char* opt_name) -> double
{
    try {
        const double parsed = std::stod(v);
        if (parsed <= 0.0) {
            throw std::runtime_error(std::string(opt_name) + " must be > 0");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid value for ") + opt_name + ": " + v);
    }
}

auto parseArgs(int argc, char* argv[]) -> CliOptions
{
    CliOptions options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            options.help = true;
            continue;
        }
        if (arg == "--list-types")
        {
            options.list_types = true;
            continue;
        }
        if (arg == "--validate-only")
        {
            options.validate_only = true;
            continue;
        }
        if (arg == "--control-stdio")
        {
            options.control_stdio = true;
            continue;
        }
        if (arg == "--server")
        {
            options.server = true;
            continue;
        }

        if (arg == "--schema")
        {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --schema");
            options.schema_path = argv[++i];
            continue;
        }
        if (arg == "--ticks")
        {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --ticks");
            options.ticks = parseLong(argv[++i], "--ticks");
            continue;
        }
        if (arg == "--seconds")
        {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --seconds");
            options.seconds = parseDouble(argv[++i], "--seconds");
            continue;
        }
        if (arg == "--port")
        {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for --port");
            options.server_port = parsePort(argv[++i]);
            continue;
        }

        throw std::runtime_error("Unknown argument: " + std::string(arg));
    }

    if (options.ticks.has_value() && options.seconds.has_value())
    {
        throw std::runtime_error("Use either --ticks or --seconds, not both");
    }

    return options;
}

auto registerNodeLibraries() -> void
{
    registerBuiltins();
    registerFileIO();
    registerMemIO();
    registerNetIO();
}

auto printRegisteredTypes() -> void
{
    registerNodeLibraries();

    auto types = ElementRegistry::instance().registeredTypes();
    std::cout << "Registered element types (" << types.size() << "):\n";
    for (const auto& t : types)
    {
        std::cout << "  - " << t << '\n';
    }
}

auto runSchemaFromFile(const CliOptions& options) -> int
{
    registerNodeLibraries();

    if (options.schema_path.empty())
    {
        throw std::runtime_error("--schema is required unless using --list-types");
    }

    const auto start = std::chrono::steady_clock::now();

    const auto doc = SchemaBuilder::parseJsonFile(options.schema_path);
    auto schema = SchemaBuilder::build(doc);

    std::cout << "Loaded schema: id='" << doc.schema_id << "', dt=" << doc.dt
              << ", elements=" << doc.elements.size()
              << ", connections=" << doc.connections.size() << '\n';

    if (options.validate_only)
    {
        std::cout << "Validation successful (no execution requested).\n";
        return EXIT_SUCCESS;
    }

    if (options.seconds.has_value())
    {
        schema->run_s(*options.seconds);
        std::cout << "Execution complete: ran for " << *options.seconds << " seconds\n";
    }
    else
    {
        const long ticks = options.ticks.value_or(1);
        schema->run(ticks);
        std::cout << "Execution complete: ran for " << ticks << " ticks\n";
    }

    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "Node run finished in " << elapsed_ms << " ms\n";

    return EXIT_SUCCESS;
}

auto runControlStdio() -> int
{
    registerNodeLibraries();
    auto runtime = NodeRuntimeService::create();
    JsonRpcDispatcher dispatcher(runtime);

    std::cerr << "S7 control stdio mode: waiting for one JSON-RPC request per line...\n";
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty()) {
            continue;
        }
        const auto response = dispatcher.dispatch(line);
        std::cout << response.dump() << '\n';
    }
    return EXIT_SUCCESS;
}

auto runControlServerMode(const int port) -> int
{
    registerNodeLibraries();
    auto runtime = NodeRuntimeService::create();
    return runControlServer(runtime, port);
}

} // namespace

auto main(int argc, char* argv[]) -> int
{
    try
    {
        const CliOptions options = parseArgs(argc, argv);

        if (options.help)
        {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        }

        if (options.list_types)
        {
            printRegisteredTypes();
            return EXIT_SUCCESS;
        }

        if (options.control_stdio)
        {
            return runControlStdio();
        }

        if (options.server)
        {
            return runControlServerMode(options.server_port);
        }

        return runSchemaFromFile(options);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        std::cerr << "Use --help for usage.\n";
        return EXIT_FAILURE;
    }
}

