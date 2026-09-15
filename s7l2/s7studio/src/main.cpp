#include "local_runtime_v2.hpp"
#include "main_window.hpp"

#include <QApplication>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cerrno>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <cstring>
#include <utility>

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

namespace {

bool hasArg(int argc, char** argv, std::string_view needle)
{
    for (int index = 1; index < argc; ++index) {
        if (needle == argv[index]) {
            return true;
        }
    }

    return false;
}

auto executableName(char** argv) -> std::string
{
    if (!argv || !argv[0]) {
        return "s7studio";
    }

    return argv[0];
}

#if defined(__linux__)
auto readSingleLine(const char* path) -> std::string
{
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    return line;
}

void enableCrashDumps()
{
    rlimit limits{};
    limits.rlim_cur = RLIM_INFINITY;
    limits.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &limits) != 0) {
        std::cerr << "s7studio: warning: failed to raise RLIMIT_CORE: " << std::strerror(errno) << '\n';
    }

    if (prctl(PR_SET_DUMPABLE, 1) != 0) {
        std::cerr << "s7studio: warning: failed to mark process dumpable: " << std::strerror(errno) << '\n';
    }
}

void printCrashDumpHelp(const std::string& program)
{
    const std::string corePattern = readSingleLine("/proc/sys/kernel/core_pattern");
    std::cout << "s7studio crash-dump help\n";
    std::cout << "program=" << program << '\n';
    std::cout << "core_pattern=" << corePattern << '\n';
    std::cout << "RLIMIT_CORE will be raised when --enable-core-dumps is passed.\n";

    if (!corePattern.empty() && corePattern.front() == '|') {
        std::cout << "This system pipes crashes to systemd-coredump, so you usually will not see a local core file appear next to the executable.\n";
        std::cout << "After a crash, export a real core file with:\n";
        std::cout << "  coredumpctl list s7studio\n";
        std::cout << "  coredumpctl dump s7studio --output ./core.s7studio\n";
        std::cout << "You can also inspect it directly with:\n";
        std::cout << "  coredumpctl gdb s7studio\n";
    } else {
        std::cout << "This system should write a core file directly according to core_pattern.\n";
    }
}
#else
void enableCrashDumps()
{
}

void printCrashDumpHelp(const std::string& program)
{
    std::cout << "s7studio crash-dump help\n";
    std::cout << "program=" << program << '\n';
    std::cout << "Automatic Linux core-dump configuration is not available on this platform.\n";
}
#endif

int runSmoke()
{
    try {
        static constexpr std::string_view smokeSchema = R"json({
            "name": "s7studio_smoke",
            "elements": [
                { "id": "osc", "type": "AnalogOscillator", "params": { "amplitude": 1.0, "frequency_hz": 1.0, "sample_rate_hz": 1000.0, "phase": 0.0 } },
                { "id": "scope_main", "type": "Scope", "params": { "channel_count": 1 } }
            ],
            "connections": [
                { "from_element": "osc", "from_port": 0, "to_element": "scope_main", "to_port": 0 }
            ]
        })json";
        boost::system::error_code ec;
        const auto schemaJson = boost::json::parse(smokeSchema, ec);
        if (ec) {
            throw std::runtime_error("Failed to parse smoke schema: " + ec.message());
        }
        const auto schema = s7studio::runtime::buildSchema(schemaJson);
        s7studio::runtime::validateSchema(schema);

        auto graph = s7::Compiler::compile(schema);
        s7::NodeRuntime runtime{std::move(graph)};

        runtime.step();
        runtime.step();

        std::cout << "s7studio smoke\n";
        std::cout << "schema=" << schema.name() << '\n';
        std::cout << "steps=" << runtime.step_count() << '\n';
        for (const auto& line : s7studio::runtime::collectSamples(schema, runtime)) {
            std::cout << line << '\n';
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7studio smoke error: " << ex.what() << '\n';
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) try
{
    const auto program = executableName(argv);

    if (hasArg(argc, argv, "--crash-dump-help")) {
        printCrashDumpHelp(program);
        return 0;
    }

    if (hasArg(argc, argv, "--enable-core-dumps")) {
        enableCrashDumps();
    }

    if (hasArg(argc, argv, "--smoke")) {
        return runSmoke();
    }

    QApplication app(argc, argv);
    s7studio::MainWindow window;
    window.show();
    return QApplication::exec();
}
catch (const std::exception& ex) {
    std::cerr << "s7studio: fatal error: " << ex.what() << '\n';
    return 1;
}
catch (...) {
    std::cerr << "s7studio: unknown fatal error\n";
    return 1;
}
