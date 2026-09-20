#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../include/s7/runtime_service.hpp"
#include "../include/s7/plugin_loader.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
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
        bool start_network = false;
        bool serve_forever = false;
        std::uint64_t serve_seconds = 0;
        std::vector<std::string> library_paths;
        std::vector<std::filesystem::path> plugin_dirs;
        std::vector<std::string> seed_peers;

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

            if (arg == "--help") {
                std::cout << "Usage: s7runtime [--schema <file>] [--library <file>]... [--plugin-dir <dir>]... [--seed-peer <url>]... [--start-net] [--serve-seconds <n>] [--serve-forever]\n";
                return 0;
            }

            throw std::invalid_argument("Unknown argument: '" + arg + "'.");
        }

        const auto definition = s7::JsonSchemaLoader::load_from_file(schema_path);

        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        s7::register_net_elements(registry);

        s7::PluginLoader plugin_loader(std::move(plugin_dirs));
        plugin_loader.load_plugins(registry);

        for (const auto& library_path : library_paths) {
            s7::SchemaLibraryLoader::register_library_file(library_path, registry);
        }

        s7::RuntimeService service{definition};
        for (const auto& seed : seed_peers) {
            service.add_seed_peer(seed);
        }
        const auto schema = service.build_schema();
        const auto schema_instance_uuid = service.create_schema_instance_uuid();
        auto executable_graph = s7::Compiler::compile(schema, service.runtime_instance_uuid(), schema_instance_uuid);
        s7::NodeRuntime runtime{std::move(executable_graph)};

        std::cout << "s7runtime starter\n";
        std::cout << "schema.file=" << schema_path << '\n';
        std::cout << "runtime.library_count=" << library_paths.size() << '\n';
        std::cout << "runtime.plugin_count=" << plugin_loader.loaded_count() << '\n';
        service.print_summary(std::cout);

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
                std::cout << "websocket.server.listening at ws://127.0.0.1:" << definition.server.port << "\n";
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
            for (std::uint64_t step = 0; step < step_count; ++step) {
                runtime.step();
            }
            std::cout << "runtime.executed_steps=" << step_count << '\n';
            for (const auto& file : service.output_files()) {
                std::cout << "runtime.output_file=" << file.string() << '\n';
            }
        }

        if (serve_forever || serve_seconds > 0) {
            std::cout << "runtime.serve=" << (serve_forever ? "forever" : std::to_string(serve_seconds) + "s") << '\n';
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(serve_seconds);
            while (!stop_requested.load()) {
                if (!serve_forever && std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "runtime.serve.stopped=" << (stop_requested.load() ? "signal" : "timeout") << '\n';
        }


        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7runtime error: " << ex.what() << '\n';
        return 1;
    }
}