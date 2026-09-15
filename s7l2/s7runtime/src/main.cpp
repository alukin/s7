#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "../include/s7/runtime_service.hpp"
#include "../include/s7/plugin_loader.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>

#ifndef S7RUNTIME_DEFAULT_SCHEMA_PATH
#define S7RUNTIME_DEFAULT_SCHEMA_PATH "s7runtime/examples/schema_minimal.json"
#endif

int main(int argc, char** argv) {
    try {
        std::string schema_path = S7RUNTIME_DEFAULT_SCHEMA_PATH;
        bool start_network = false;

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

            if (arg == "--help") {
                std::cout << "Usage: s7runtime [--schema <file>] [--start-net]\n";
                return 0;
            }

            throw std::invalid_argument("Unknown argument: '" + arg + "'.");
        }

        const auto definition = s7::JsonSchemaLoader::load_from_file(schema_path);

        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);

        s7::PluginLoader plugin_loader;
        plugin_loader.load_plugins(registry);

        s7::RuntimeService service{definition};
        const auto schema = service.build_schema();
        auto executable_graph = s7::Compiler::compile(schema);
        s7::NodeRuntime runtime{std::move(executable_graph)};

        std::cout << "s7runtime starter\n";
        std::cout << "schema.file=" << schema_path << '\n';
        std::cout << "runtime.plugin_count=" << plugin_loader.loaded_count() << '\n';
        service.print_summary(std::cout);

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

        if (start_network) {
            service.start_websocket_server();
            service.connect_websocket_client();
            std::cout << "runtime.network=started\n";
        } else {
            std::cout << "runtime.network=not-started (use --start-net)\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7runtime error: " << ex.what() << '\n';
        return 1;
    }
}