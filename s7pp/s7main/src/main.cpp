#include "s7factory.hpp"
#include "s7control_server.hpp"
#include "s7netlist.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
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
        << "  " << exe << " --server [--port <n>]\n"
        << "  " << exe << " --help\n\n"
        << "Options:\n"
        << "  --schema <path>      Path to netlist JSON file\n"
        << "  --ticks <n>          Run schema for n ticks\n"
        << "  --seconds <s>        Run schema for s seconds (uses dt from netlist)\n"
        << "  --validate-only      Load and validate only (no execution)\n"
        << "  --list-types         List registered element types and exit\n"
        << "  --server             Start websocket control server\n"
        << "  --port <n>           Control server port (default: 5577)\n"
        << "  --help, -h           Show this help\n";
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

auto parseArgs(std::span<char* const> argv) -> CliOptions
{
    CliOptions options;

    for (std::size_t i = 1; i < argv.size(); ++i)
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
        if (arg == "--server")
        {
            options.server = true;
            continue;
        }
        if (arg == "--schema")
        {
            if (i + 1 >= argv.size()) throw std::runtime_error("Missing value for --schema");
            options.schema_path = argv[++i];
            continue;
        }
        if (arg == "--ticks")
        {
            if (i + 1 >= argv.size()) throw std::runtime_error("Missing value for --ticks");
            options.ticks = parseLong(argv[++i], "--ticks");
            continue;
        }
        if (arg == "--seconds")
        {
            if (i + 1 >= argv.size()) throw std::runtime_error("Missing value for --seconds");
            options.seconds = parseDouble(argv[++i], "--seconds");
            continue;
        }
        if (arg == "--port")
        {
            if (i + 1 >= argv.size()) throw std::runtime_error("Missing value for --port");
            options.server_port = static_cast<int>(parseLong(argv[++i], "--port"));
            continue;
        }
        auto message = std::string{"Unknown argument: "};
        message += arg;
        throw std::runtime_error(message);
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
              << ", nodes=" << doc.nodes.size()
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

auto runControlServerMode(const int port) -> int
{
    registerNodeLibraries();
    auto runtime = NodeRuntimeService::create();
    return runControlServer(runtime, port);
}

} // namespace

auto main(int argc, char** argv) -> int
{
    try
    {
        const CliOptions options = parseArgs({argv, static_cast<std::size_t>(argc)});

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

