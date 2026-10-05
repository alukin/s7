#include "local_runtime_v2.hpp"
#include "local_schema_runner.hpp"
#include "scope_sample_mailbox.hpp"
#include "main_window.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/transport.hpp>

#include <QApplication>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cmath>
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
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        s7::register_net_elements(registry);
        assert(registry.find("NetSink") != nullptr);
        assert(registry.find("NetSource") != nullptr);

        static constexpr std::string_view smokeSchema = R"json({
            "name": "s7studio_smoke",
            "elements": [
                { "id": "source", "type": "Constant", "params": { "value": 3.0 } },
                {
                    "id": "nested",
                    "type": "Schema",
                    "schema": {
                        "name": "nested_gain_chain",
                        "elements": [
                            { "id": "g1", "type": "Gain", "params": { "factor": 2.0 } },
                            { "id": "g2", "type": "Gain", "params": { "factor": 5.0 } }
                        ],
                        "connections": [
                            { "from_element": "g1", "from_port": 0, "to_element": "g2", "to_port": 0 }
                        ],
                        "exposed_inputs": [
                            { "name": "in", "type": "F64", "element": "g1", "port": 0 }
                        ],
                        "exposed_outputs": [
                            { "name": "out", "type": "F64", "element": "g2", "port": 0 }
                        ]
                    }
                },
                { "id": "scope_main", "type": "Scope", "params": { "channel_count": 1 } }
            ],
            "connections": [
                { "from_element": "source", "from_port": 0, "to_element": "nested", "to_port": 0 },
                { "from_element": "nested", "from_port": 0, "to_element": "scope_main", "to_port": 0 }
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

        {
            // The Scope mailbox stays bounded, drops oldest first, keeps order,
            // and asks for one UI notification at a time.
            s7studio::ScopeSampleMailbox mailbox{4};
            bool notified = false;
            int notifications = 0;
            for (long step = 0; step < 10; ++step) {
                notified = mailbox.push(s7studio::ScopeSample{"scope", 0, step, 0.001 * step, static_cast<double>(step)});
                notifications += notified ? 1 : 0;
            }
            assert(notifications == 1);
            auto batch = mailbox.take();
            assert(batch.samples.size() == 4 && batch.dropped == 6);
            for (std::size_t index = 0; index < batch.samples.size(); ++index) {
                assert(batch.samples[index].step == static_cast<long>(6 + index));
            }
            assert(mailbox.push(s7studio::ScopeSample{"scope", 0, 10, 0.01, 10.0}));
            batch = mailbox.take();
            assert(batch.samples.size() == 1 && batch.dropped == 0);
        }

        const s7studio::runtime::detail::ScopeElement scope3{3};
        assert(scope3.input_ports().size() == 3 && scope3.output_ports().size() == 3);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::string expected = "ch" + std::to_string(channel);
            assert(scope3.input_ports()[channel].name == expected);
            assert(scope3.output_ports()[channel].name == expected);
        }

        std::cout << "s7studio smoke\n";
        std::cout << "schema=" << schema.name() << '\n';
        assert(std::abs(runtime.real_output_of("nested") - 30.0) < 1e-12);
        std::cout << "steps=" << runtime.step_count() << '\n';
        for (const auto& line : s7studio::runtime::collectSamples(schema, runtime)) {
            std::cout << line << '\n';
        }

        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);
        const auto tx = endpoint->open_endpoint({
            .endpoint_name = "studio-smoke-tx",
            .stream_name = "studio.demo.in",
            .allow_publish = true,
            .allow_receive = false,
        });
        assert(tx.ok);

        s7::transport::DataPacket packet;
        packet.stream_name = "studio.demo.in";
        packet.sequence = 1;
        packet.data_type = s7::transport::kPrototypeDataTypeF64;
        packet.elem_count = 1;
        packet.t_model_ns = 1;
        packet.payload = s7::transport::encode_f64_payload(8.5);
        const auto send = endpoint->publish(tx.handle, packet);
        assert(send.ok);

        static constexpr std::string_view netSchema = R"json({
            "name": "s7studio_net_smoke",
            "elements": [
                { "id": "net_in", "type": "NetSource", "params": { "topic": "studio.demo.in" } }
            ],
            "connections": []
        })json";
        const auto netSchemaJson = boost::json::parse(netSchema, ec);
        if (ec) {
            throw std::runtime_error("Failed to parse studio net smoke schema: " + ec.message());
        }
        const auto netGraphSchema = s7studio::runtime::buildSchema(netSchemaJson);
        s7studio::runtime::validateSchema(netGraphSchema);
        auto netGraph = s7::Compiler::compile(netGraphSchema);
        s7::NodeRuntime netRuntime{std::move(netGraph)};
        netRuntime.step();
        assert(std::abs(netRuntime.real_output_of("net_in") - 8.5) < 1e-12);
        s7::RuntimeNetContext::set_endpoint(nullptr);

        {
            // A run that produces samples while the UI takes none keeps at most
            // the mailbox capacity; the rest is counted as dropped.
            const auto scopeSchema = boost::json::parse(R"json({
                "name": "scope_backlog",
                "elements": [
                    { "id": "source", "type": "Constant", "params": { "value": 1.0 } },
                    { "id": "scope", "type": "Scope", "params": { "channel_count": 1 } }
                ],
                "connections": [ { "from_element": "source", "from_port": 0, "to_element": "scope", "to_port": 0 } ]
            })json");
            s7studio::LocalSchemaRunner runner;
            constexpr long ticks = 300'000;
            runner.run(scopeSchema, 0.001, ticks);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (runner.isRunning() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            assert(!runner.isRunning());
            const auto batch = runner.takeScopeSamples();
            assert(batch.samples.size() == 200'000);
            assert(batch.dropped == static_cast<std::uint64_t>(ticks) - 200'000);
            assert(batch.samples.back().step == ticks);
            assert(std::abs(batch.samples.back().timeSeconds - static_cast<double>(ticks) * 0.001) < 1e-9);
            runner.reset();
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
