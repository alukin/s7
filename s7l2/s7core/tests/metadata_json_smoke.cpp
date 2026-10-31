// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "s7/metadata_json.hpp"
#include "s7/builtin_element_registry.hpp"
#include "s7/element_registry.hpp"
#include "core_metadata.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class Function>
void rejects(Function function, const char* expected)
{
    try { function(); }
    catch (const std::exception& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
        return;
    }
    throw std::runtime_error("Expected metadata error was not raised");
}
}
int main()
{
    try {
        const s7::ElementMetadataLibrary library(s7::resources::core_metadata);
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        // Every JSON entry has an executable factory and every non-schema
        // built-in has an entry. Missing/stray definitions fail the audit.
        for (const auto& [type, metadata] : library.elements()) {
            const auto* descriptor = registry.find(type);
            require(descriptor && descriptor->type_name() == metadata.type_name, "Metadata has no matching factory");
            require(metadata.library == "s7core" && descriptor->metadata().library == metadata.library,
                    "Owning module provenance missing from registered metadata");
        }
        for (const auto& descriptor : registry.all_descriptors())
            if (!descriptor.metadata().schema_definition) static_cast<void>(library.at(descriptor.type_name()));
        const auto& adder = library.at("Adder");
        require(adder.stateless && library.at("Constant").stateless && !library.at("Delay").stateless,
                "Stateless visual hints do not match core element behavior");
        require(adder.type_variables.size() == 2 && adder.type_variables[1].defaults_to == "T", "Accumulator type default changed");
        require(adder.output_ports[0].type_rule == "A" && adder.variadic_inputs.count_parameter == "weights", "Port rules changed");
        const auto params = s7::checked_params("test", library.at("EulerIntegrator"), {});
        // dt is a legacy field with no default: the integrator follows its domain's rate.
        require(!params.contains("dt") && params.at("initial_value") == 0, "Integrator defaults changed");
        const auto& gauss_params = library.at("GaussLegendreIntegrator").parameters;
        const auto gauss_dt = std::ranges::find_if(gauss_params, [](const auto& parameter) {
            return parameter.name == "dt";
        });
        require(gauss_dt != gauss_params.end() && gauss_dt->legacy && gauss_dt->type == "value",
                "Gauss dt must be hidden as a legacy value in Studio");
        rejects([&] { static_cast<void>(s7::checked_params("test", library.at("Convert"), {})); }, "required");
        rejects([&] { static_cast<void>(s7::checked_params("test", library.at("MinMax"), {{"input_count",1}})); }, "at least 2");
        rejects([&] { static_cast<void>(library.at("MissingFactory")); }, "MissingFactory");
        const auto baseline = boost::json::parse(s7::resources::core_metadata);
        const auto mutation = [&](auto change, const char* expected) {
            auto document = baseline;
            change(document.as_object());
            rejects([&] { s7::ElementMetadataLibrary invalid(boost::json::serialize(document)); }, expected);
        };
        mutation([](auto& root) { root["format_version"] = 2; }, "unsupported");
        mutation([](auto& root) { root["library"] = 42; }, "library");
        mutation([](auto& root) { root["elemnts"] = root["elements"]; }, "unknown field");
        mutation([](auto& root) { root["elements"].as_array().front().as_object()["stateless"] = "yes"; }, "stateless");
        mutation([](auto& root) { root["elements"].as_array().emplace_back(root["elements"].as_array().front()); }, "duplicate type");
        mutation([](auto& root) { root["elements"].as_array().front().as_object().erase("parameters"); }, "parameters");
        mutation([](auto& root) {
            auto& element = root["elements"].as_array().front().as_object();
            element["parameters"].as_array().emplace_back(element["parameters"].as_array().front());
        }, "duplicate name");
        mutation([](auto& root) { root["elements"].as_array().front().at("parameters").as_array().front().as_object()["type"] = "integer_typo"; }, "unknown parameter type");
        mutation([](auto& root) { root["elements"].as_array().front().at("type_variables").as_array().front().as_object()["pin_parameter"] = "missing"; }, "pin");
        mutation([](auto& root) { root["elements"].as_array().front().at("output_ports").as_array().front().as_object()["type_rule"] = "missing"; }, "type rule");
        mutation([](auto& root) {
            auto& parameter = root["elements"].as_array().front().at("parameters").as_array().front().as_object();
            parameter["min"] = 4; parameter["max"] = 2;
        }, "minimum exceeds maximum");
        mutation([](auto& root) { root["elements"].as_array().front().at("parameters").as_array().front().as_object()["default"] = 123; }, "must be a type name");
        std::cout << "Metadata JSON: factories, defaults, constraints, type rules and malformed resources verified\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
