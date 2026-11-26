// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "s7/metadata_json.hpp"
#include "s7/builtin_element_registry.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_catalog.hpp"
#include "s7/compiler.hpp"
#include "s7/elements.hpp"
#include "s7/schema_library.hpp"
#include "core_metadata.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
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

void check_array_metadata(s7::ElementRegistry& registry, const boost::json::value& baseline)
{
    namespace json = boost::json;
    auto document = baseline;
    auto& elements = document.at("elements").as_array();
    auto fixture = elements.front().as_object();
    elements.clear();
    fixture["type_name"] = "ArrayMetadataFixture";
    fixture["category"] = "array-metadata-test";
    fixture["output_ports"] = json::array{
        json::object{{"name", "field"}, {"description", ""}, {"type", "F64"}, {"optional", false},
                     {"type_rule", ""}, {"shape", json::array{2, 3, 4, 3}}}};
    elements.emplace_back(fixture);
    const s7::ElementMetadataLibrary metadata(json::serialize(document));
    const auto& entry = metadata.at("ArrayMetadataFixture");
    const auto& array = entry.output_ports.front().array;
    require(array && array->rank() == 4 && array->element_count() == 72, "metadata preserves a fixed F64 field shape");

    bool created = false;
    registry.register_element(s7::ElementDescriptor(entry.type_name, [&](const s7::ElementCreationContext&) {
        created = true;
        return std::make_unique<s7::Constant>(1.0);
    }, entry));
    const auto* descriptor = registry.find(entry.type_name);
    require(descriptor && descriptor->metadata().output_ports.front().array == array, "registry preserves the array descriptor");
    const auto catalog = s7::ElementCatalogService(registry).to_json();
    const auto& port = catalog.at("array-metadata-test").as_array().front().at("output_ports").as_array().front().as_object();
    require(s7::parse_port_array(port, s7::SignalType::F64, "catalog") == array, "catalog shape round-trips through JSON");
    const json::object params;
    rejects([&] { static_cast<void>(descriptor->create({descriptor->metadata(), params})); }, "Array execution");
    require(!created, "the scalar factory must not run for a declaration-only array element");
    registry.unregister({entry.type_name});

    auto& output = elements.front().at("output_ports").as_array().front().as_object();
    for (const auto& shape : {json::value(nullptr), json::value(json::object{{"rank", 1}}), json::value(json::array{}),
                              json::value(json::array{0}), json::value(json::array{-1}), json::value(json::array{2.0}),
                              json::value(json::array{"N"}), json::value(json::array{std::numeric_limits<std::uint64_t>::max()}),
                              json::value(json::array{std::numeric_limits<std::int64_t>::max(), 3})}) {
        output["shape"] = shape;
        rejects([&] { s7::ElementMetadataLibrary invalid(json::serialize(document)); }, "'shape'");
    }
    output["shape"] = json::array{2};
    output["type_rule"] = "T";
    rejects([&] { s7::ElementMetadataLibrary invalid(json::serialize(document)); }, "requires a fixed numeric type");
}

void check_array_schema(s7::ElementRegistry& registry)
{
    namespace json = boost::json;
    auto source = json::parse(R"({
        "document_kind":"s7.element_library", "name":"array-schema-test",
        "elements":[{"type_name":"ArraySchemaFixture","category":"array-schema-test","schema":{
            "name":"field_fixture", "dt":0.01, "elements":[], "connections":[],
            "exposed_inputs":[{"name":"state","type":"F64","element":"field","port":0,"shape":[2,4]}],
            "exposed_outputs":[{"name":"iq","type":"C32","element":"generator","port":0,"shape":[128]}]
        }}]
    })");
    const auto definition = s7::SchemaLibraryLoader::load_from_json(source);
    const auto reloaded = s7::SchemaLibraryLoader::load_from_json(json::parse(json::serialize(definition.to_json())));
    const auto& schema_json = reloaded.elements.front().schema;
    require(schema_json == definition.elements.front().schema, "array declarations survive schema serialization");
    s7::SchemaLibraryLoader::register_library(reloaded, registry);
    const auto* descriptor = registry.find("ArraySchemaFixture");
    require(descriptor, "shaped schema library element is registered");
    auto schema = s7::SchemaLibraryLoader::build_schema(schema_json, registry);
    require(schema.exposed_inputs().front().array == descriptor->metadata().input_ports.front().array &&
            schema.exposed_outputs().front().array == descriptor->metadata().output_ports.front().array &&
            schema.has_array_ports(), "registered schema metadata and loaded ports describe the same arrays");
    require(schema.output_ports().front().value_ptr == nullptr, "array metadata cannot expose scalar placeholder bytes");
    rejects([&] { schema.step(); }, "Array execution");
    rejects([&] { static_cast<void>(s7::Compiler::compile(schema)); }, "Array execution");
    s7::Schema parent("parent");
    parent.add_element("nested", [&] {
        return std::make_unique<s7::Schema>(s7::SchemaLibraryLoader::build_schema(schema_json, registry));
    });
    rejects([&] { static_cast<void>(s7::Compiler::compile(parent)); }, "Schema 'nested' declares array ports");
    registry.unregister({"ArraySchemaFixture"});

    auto malformed = schema_json;
    auto& port = malformed.at("exposed_inputs").as_array().front().as_object();
    port.erase("type");
    rejects([&] { static_cast<void>(s7::SchemaLibraryLoader::build_schema(malformed, registry)); }, "fixed numeric type");
    port["type"] = "F64";
    port["format"] = "invalid";
    rejects([&] { static_cast<void>(s7::SchemaLibraryLoader::build_schema(malformed, registry)); }, "Unknown exposed port field");

    auto invalid_library = reloaded;
    invalid_library.elements.push_back({"MalformedArraySchema", "", "", "", "", malformed});
    rejects([&] { s7::SchemaLibraryLoader::register_library(invalid_library, registry); }, "Unknown exposed port field");
    require(!registry.find("ArraySchemaFixture"), "a malformed shape declaration leaves the registry unchanged");
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
        require(!adder.output_ports.front().array, "omitted shapes keep the scalar default");
        require(adder.stateless && library.at("Constant").stateless && !library.at("Delay").stateless,
                "Stateless visual hints do not match core element behavior");
        require(adder.type_variables.size() == 2 && adder.type_variables[1].defaults_to == "T", "Accumulator type default changed");
        require(adder.output_ports[0].type_rule == "A" && adder.variadic_inputs.count_parameter == "weights", "Port rules changed");
        const auto params = s7::checked_params("test", library.at("EulerIntegrator"), {});
        require(!params.contains("dt") && params.at("initial_value") == 0, "Integrator defaults changed");
        rejects([&] { static_cast<void>(s7::checked_params("test", library.at("GaussLegendreIntegrator"), {{"dt", 0.001}})); },
                "unknown parameter 'dt'");
        rejects([&] { static_cast<void>(s7::checked_params("test", library.at("Convert"), {})); }, "required");
        rejects([&] { static_cast<void>(s7::checked_params("test", library.at("MinMax"), {{"input_count",1}})); }, "at least 2");
        rejects([&] { static_cast<void>(library.at("MissingFactory")); }, "MissingFactory");
        const auto baseline = boost::json::parse(s7::resources::core_metadata);
        check_array_metadata(registry, baseline);
        check_array_schema(registry);
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
        {
            auto document = baseline;
            auto& entry = document.at("elements").as_array().front().as_object();
            entry["variadic_outputs"] = entry.at("variadic_inputs");
            const s7::ElementMetadataLibrary extended(boost::json::serialize(document));
            require(extended.at(std::string(entry.at("type_name").as_string())).variadic_outputs.count_parameter ==
                entry.at("variadic_inputs").at("count_parameter").as_string(), "Output channel sizing metadata was lost");
        }
        mutation([](auto& root) {
            auto& entry = root["elements"].as_array().front().as_object();
            entry["variadic_outputs"] = entry["variadic_inputs"];
            entry["variadic_outputs"].as_object()["enabled"] = true;
            entry["variadic_outputs"].as_object()["count_parameter"] = "missing";
        }, "count parameter");
        std::cout << "Metadata JSON: factories, defaults, constraints, type rules and malformed resources verified\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
