// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/platform.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../include/s7/runtime_service.hpp"
#include "../include/s7/plugin_loader.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/io.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/platform.hpp>
#include <s7/schema_library.hpp>

#ifndef S7RUNTIME_DEFAULT_SCHEMA_PATH
#define S7RUNTIME_DEFAULT_SCHEMA_PATH "s7runtime/examples/schema_minimal.json"
#endif

int main(int argc, char** argv) {
    try {
        static std::atomic<bool> stop_requested{false};
        std::signal(SIGINT, [](int) { stop_requested.store(true); });
        std::signal(SIGTERM, [](int) { stop_requested.store(true); });

        std::string schema_path = S7RUNTIME_DEFAULT_SCHEMA_PATH;
        if (const auto directory = s7::platform::executable_directory()) {
            for (const auto& candidate : {*directory / "examples" / "schema_minimal.json",
                     *directory / ".." / "share" / "s7" / "runtime" / "examples" / "schema_minimal.json"}) {
                if (std::filesystem::is_regular_file(candidate)) {
                    schema_path = candidate.string();
                    break;
                }
            }
        }
        bool start_network = false;
        bool serve_forever = false;
        std::uint64_t serve_seconds = 0;
        std::vector<std::string> library_paths;
        std::vector<std::filesystem::path> plugin_dirs;
        std::vector<std::string> seed_peers;
        std::filesystem::path state_dir;
        std::optional<double> step_rate_hz;

        for (int index = 1; index < argc; ++index) {
            const std::string arg = argv[index];

            if (arg == "--schema") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --schema.");
                }
                schema_path = argv[++index];
                continue;
            }

            if (arg == "--start-net") {
                start_network = true;
                continue;
            }

            if (arg == "--serve-forever") {
                serve_forever = true;
                continue;
            }

            if (arg == "--serve-seconds") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --serve-seconds.");
                }
                serve_seconds = static_cast<std::uint64_t>(std::stoull(argv[++index]));
                continue;
            }

            if (arg == "--library") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --library.");
                }
                library_paths.emplace_back(argv[++index]);
                continue;
            }

            if (arg == "--plugin-dir") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --plugin-dir.");
                }
                plugin_dirs.emplace_back(argv[++index]);
                continue;
            }

            if (arg == "--seed-peer") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --seed-peer.");
                }
                seed_peers.emplace_back(argv[++index]);
                continue;
            }

            if (arg == "--state-dir") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --state-dir.");
                }
                state_dir = argv[++index];
                continue;
            }

            if (arg == "--step-rate") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("Missing value after --step-rate.");
                }
                step_rate_hz = std::stod(argv[++index]);
                if (!std::isfinite(*step_rate_hz) || *step_rate_hz < 0.0) {
                    throw std::invalid_argument("--step-rate must be a non-negative number.");
                }
                continue;
            }

            if (arg == "--help") {
                std::cout << "Usage: s7runtime [--schema <file>] [--state-dir <dir>] [--library <file>]... [--plugin-dir <dir>]... [--seed-peer <url>]... [--start-net] [--serve-seconds <n>] [--serve-forever] [--step-rate <hz>]\n"
                          << "  --state-dir <dir>  node identity and peer cache; each node needs its own\n"
                          << "                     (default: ~/.s7runtime/default-node, for single-node development)\n"
                          << "  --step-rate <hz>   step the graph at this rate while serving; overrides\n"
                          << "                     execution.step_rate_hz in the schema (0: do not step)\n";
                return 0;
            }

            throw std::invalid_argument("Unknown argument: '" + arg + "'.");
        }

        auto definition = s7::JsonSchemaLoader::load_from_file(schema_path);
        if (step_rate_hz) {
            definition.execution.step_rate_hz = *step_rate_hz;
        }

        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        s7::register_io_elements(registry);
        s7::register_net_elements(registry);

        s7::PluginLoader plugin_loader(std::move(plugin_dirs));
        plugin_loader.load_plugins(registry);

        for (const auto& library_path : library_paths) {
            s7::SchemaLibraryLoader::register_library_file(library_path, registry);
        }

        if (state_dir.empty()) {
            const auto home = s7::platform::home_directory();
            if (!home) {
                throw std::invalid_argument("No home directory (HOME is not set); pass --state-dir.");
            }
            state_dir = *home / ".s7runtime" / "default-node";
        }

        s7::RuntimeService service{definition, state_dir};
        for (const auto& seed : seed_peers) {
            service.add_seed_peer(seed);
        }
        service.initialize_graph();

        std::cout << "s7runtime starter\n";
        std::cout << "schema.file=" << schema_path << '\n';
        std::cout << "runtime.library_count=" << library_paths.size() << '\n';
        std::cout << "runtime.plugin_count=" << plugin_loader.loaded_count() << '\n';
        service.print_summary(std::cout);
        service.check_clock(std::cout);

        // Start WebSocket server if enabled in schema, or if --start-net flag is provided
        bool should_start_network = start_network || definition.server.enabled;
        if (should_start_network) {
            std::cout << "\n--- Starting Network Services ---\n";
            if (definition.server.enabled) {
                std::cout << "websocket.server.starting on port " << definition.server.port << "...\n";
                std::cout.flush();
            }
            service.start_websocket_server();
            if (definition.server.enabled) {
                const bool tls = definition.server.tls.enabled;
                const auto bind = !definition.server.bind_address.empty() ? definition.server.bind_address
                                  : tls                                  ? std::string("0.0.0.0")
                                                                         : std::string("127.0.0.1");
                std::cout << "websocket.server.listening at " << (tls ? "wss://" : "ws://") << bind << ':' << definition.server.port
                          << (tls || !definition.server.bind_address.empty() ? "" : " (loopback only: plain WebSocket)") << "\n";
                std::cout.flush();
            }
            
            if (definition.client.enabled) {
                std::cout << "websocket.client.connecting to " << definition.client.url << "...\n";
                std::cout.flush();
            }
            service.connect_websocket_client();
            if (definition.client.enabled) {
                std::cout << "websocket.client.connected\n";
            }
            std::cout << "runtime.network=started\n";
            std::cout << "--- Network Services Ready ---\n\n";
            std::cout.flush();
        } else {
            std::cout << "runtime.network=not-started (use --start-net or enable in schema)\n";
        }

        if (service.has_simulation()) {
            const auto step_count = service.simulation_step_count();
            std::uint64_t executed_steps = 0;
            try {
                for (; executed_steps < step_count; ++executed_steps) {
                    service.step();
                }
            } catch (const std::exception& ex) {
                std::cout << "runtime.executed_steps=" << executed_steps << '\n';
                for (const auto& file : service.output_files()) {
                    std::cout << "runtime.output_file=" << file.string() << '\n';
                }
                std::cerr << "runtime.execution.error=" << ex.what() << '\n';
                return 1;
            }
            std::cout << "runtime.executed_steps=" << step_count << '\n';
            for (const auto& file : service.output_files()) {
                std::cout << "runtime.output_file=" << file.string() << '\n';
            }
        }

        if (serve_forever || serve_seconds > 0) {
            std::cout << "runtime.serve=" << (serve_forever ? "forever" : std::to_string(serve_seconds) + "s") << '\n';
            std::optional<std::chrono::steady_clock::time_point> deadline;
            if (!serve_forever) {
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(serve_seconds);
            }
            if (definition.execution.step_rate_hz > 0.0) {
                std::cout << "runtime.execution=stepping step_rate_hz=" << definition.execution.step_rate_hz << '\n';
            } else {
                std::cout << "runtime.execution=idle (set execution.step_rate_hz or --step-rate to step the startup graph; "
                             "deployed graphs with a model-time step run in real time)\n";
            }
            std::cout.flush();
            const auto stats = service.serve(stop_requested, deadline, std::cout, std::cerr);
            std::cout << "runtime.execution.steps=" << service.step_count() << '\n';
            std::cout << "runtime.execution.achieved_rate_hz="
                      << (stats.elapsed_seconds > 0.0 ? static_cast<double>(stats.steps) / stats.elapsed_seconds : 0.0) << '\n';
            service.report_transport_errors(std::cerr);
            service.report_transport_stats(std::cout);
            std::cout << "runtime.serve.stopped=" << (stats.stopped_by_signal ? "signal" : "timeout") << '\n';
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7runtime error: " << ex.what() << '\n';
        return 1;
    }
}