#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../include/s7/runtime_service.hpp"

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
        s7::RuntimeService service{definition};

        std::cout << "s7runtime starter\n";
        std::cout << "schema.file=" << schema_path << '\n';
        service.print_summary(std::cout);

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