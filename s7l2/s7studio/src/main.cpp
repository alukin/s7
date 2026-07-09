#include "demo_schema.hpp"
#include "local_runtime.hpp"
#include "main_window.hpp"

#include <QApplication>

#include <algorithm>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

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

int runSmoke()
{
    try {
        const auto schemaJson = s7studio::makeDemoSchemaJson();
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
    if (hasArg(argc, argv, "--smoke")) {
        return runSmoke();
    }

    QApplication app(argc, argv);
    s7studio::MainWindow window;
    window.show();
    return app.exec();
}
catch (const std::exception& ex) {
    std::cerr << "s7studio: fatal error: " << ex.what() << '\n';
    return 1;
}
catch (...) {
    std::cerr << "s7studio: unknown fatal error\n";
    return 1;
}
