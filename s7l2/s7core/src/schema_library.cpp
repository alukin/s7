// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/schema_library.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <string_view>

#include <zlib.h>

namespace s7 {
namespace {

struct SchemaPortBinding {
    std::string name;
    std::string type;
    std::string element;
    std::size_t port{};
};

auto expect_object(const boost::json::value& value, std::string_view field) -> const boost::json::object&
{
    if (!value.is_object()) {
        throw std::invalid_argument("Expected object in field '" + std::string(field) + "'.");
    }
    return value.as_object();
}

auto read_file_contents(const std::filesystem::path& file_path) -> std::string
{
    if (file_path.extension() != ".gz") {
        std::ifstream in(file_path);
        if (!in) {
            throw std::runtime_error("Could not open element library file: '" + file_path.string() + "'.");
        }

        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    const auto path_string = file_path.string();
    gzFile file = gzopen(path_string.c_str(), "rb");
    if (!file) {
        throw std::runtime_error("Could not open compressed element library file: '" + file_path.string() + "'.");
    }

    std::string output;
    char chunk[8192];
    while (true) {
        const int bytes_read = gzread(file, chunk, static_cast<unsigned int>(sizeof(chunk)));
        if (bytes_read > 0) {
            output.append(chunk, static_cast<std::size_t>(bytes_read));
            continue;
        }
        if (bytes_read == 0) {
            break;
        }

        int error_code = Z_OK;
        const char* error_message = gzerror(file, &error_code);
        gzclose(file);
        throw std::runtime_error(
            std::string{"Could not read compressed element library file '"} + file_path.string() + "': " +
            (error_message ? error_message : "unknown zlib error"));
    }

    gzclose(file);
    return output;
}

auto expect_array(const boost::json::object& object, std::string_view field) -> const boost::json::array&
{
    const auto it = object.find(field);
    if (it == object.end() || !it->value().is_array()) {
        throw std::invalid_argument("Expected array field '" + std::string(field) + "'.");
    }
    return it->value().as_array();
}

auto optional_object(const boost::json::object& object, std::string_view field) -> const boost::json::object*
{
    const auto it = object.find(field);
    if (it == object.end() || !it->value().is_object()) {
        return nullptr;
    }
    return &it->value().as_object();
}

auto string_value(const boost::json::object& object,
                  std::string_view field,
                  const std::string& fallback = {}) -> std::string
{
    const auto it = object.find(field);
    if (it == object.end()) {
        return fallback;
    }
    if (!it->value().is_string()) {
        throw std::invalid_argument("Expected string field '" + std::string(field) + "'.");
    }
    return std::string(it->value().as_string());
}

auto non_negative_index(const boost::json::object& object, std::string_view field) -> std::size_t
{
    const auto it = object.find(field);
    if (it == object.end()) {
        return 0;
    }
    if (it->value().is_uint64()) {
        return static_cast<std::size_t>(it->value().as_uint64());
    }
    if (it->value().is_int64() && it->value().as_int64() >= 0) {
        return static_cast<std::size_t>(it->value().as_int64());
    }
    throw std::invalid_argument("Expected non-negative integer field '" + std::string(field) + "'.");
}

auto signal_type_from_string(const std::string& type) -> SignalType
{
    return parse_signal_type(type);
}

auto parse_port_bindings(const boost::json::object& root, const std::string& field) -> std::vector<SchemaPortBinding>
{
    std::vector<SchemaPortBinding> bindings;
    const auto it = root.find(field);
    if (it == root.end()) {
        return bindings;
    }
    if (!it->value().is_array()) {
        throw std::invalid_argument("Expected array field '" + field + "'.");
    }

    for (const auto& item : it->value().as_array()) {
        const auto& binding_object = expect_object(item, field);
        SchemaPortBinding binding;
        binding.name = string_value(binding_object, "name");
        binding.type = string_value(binding_object, "type", "F64");
        binding.element = string_value(binding_object, "element");
        binding.port = non_negative_index(binding_object, "port");
        bindings.push_back(std::move(binding));
    }

    return bindings;
}

auto build_port_specs(const std::vector<SchemaPortBinding>& bindings, const bool output) -> std::vector<PortSpec>
{
    std::vector<PortSpec> ports;
    ports.reserve(bindings.size());

    for (const auto& binding : bindings) {
        PortSpec port;
        port.name = binding.name;
        port.description = output
            ? "Exposed output"
            : "Exposed input";
        port.type = signal_type_from_string(binding.type);
        port.optional = false;
        ports.push_back(std::move(port));
    }

    return ports;
}

auto build_schema_from_object(const boost::json::object& root, const ElementRegistry& registry) -> Schema
{
    const auto name = string_value(root, "name", "schema");
    Schema schema{name};
    // "dt": 0.001 seeds every element with no rate of its own
    // (doc/S7_Sample_Rate_Model.md), the same field name/meaning as the
    // runtime's own schema JSON (s7runtime/src/runtime_service.cpp).
    if (const auto* dt = root.if_contains("dt")) {
        if (!dt->is_number()) {
            throw std::invalid_argument("'dt' must be a positive number of seconds.");
        }
        schema.set_rate(Rate::from_seconds(dt->to_number<double>()));
    }

    const auto& elements = expect_array(root, "elements");
    for (const auto& element_value : elements) {
        const auto& element = expect_object(element_value, "element");
        const auto id = string_value(element, "id");
        const auto type = string_value(element, "type", "Element");
        const auto params_it = element.find("params");
        if (params_it != element.end() && !params_it->value().is_object()) {
            throw std::invalid_argument("Expected object field 'params' in schema library element '" + id + "'.");
        }
        const auto& params = params_it != element.end()
            ? params_it->value().as_object()
            : boost::json::object{};
        const auto* nested_schema = optional_object(element, "schema");

        if (nested_schema) {
            const auto nested_definition = boost::json::value(*nested_schema);
            schema.add_element(id, [nested_definition, &registry] {
                auto nested_schema_value = SchemaLibraryLoader::build_schema(nested_definition, registry);
                return std::make_unique<Schema>(std::move(nested_schema_value));
            });
            continue;
        }

        const auto* descriptor = registry.find(type);
        if (!descriptor) {
            throw std::invalid_argument("Unsupported element type in schema library payload: '" + type + "'.");
        }

        schema.add_element(make_element_spec(id, *descriptor, params));
    }

    const auto& connections = expect_array(root, "connections");
    for (const auto& connection_value : connections) {
        const auto& connection = expect_object(connection_value, "connection");
        schema.connect(string_value(connection, "from_element"),
                       non_negative_index(connection, "from_port"),
                       string_value(connection, "to_element"),
                       non_negative_index(connection, "to_port"));
    }

    for (const auto& binding : parse_port_bindings(root, "exposed_inputs")) {
        schema.expose_input(binding.name, signal_type_from_string(binding.type), binding.element, binding.port);
    }
    for (const auto& binding : parse_port_bindings(root, "exposed_outputs")) {
        schema.expose_output(binding.name, signal_type_from_string(binding.type), binding.element, binding.port);
    }

    return schema;
}

auto count_ports(const boost::json::object& root, const std::string& field) -> std::vector<SchemaPortBinding>
{
    return parse_port_bindings(root, field);
}

auto library_element_metadata(const SchemaLibraryDefinition& library,
                              const SchemaLibraryElementDefinition& element) -> ElementMetadata
{
    const auto& schema_root = expect_object(element.schema, "schema");
    const auto inputs = count_ports(schema_root, "exposed_inputs");
    const auto outputs = count_ports(schema_root, "exposed_outputs");

    ElementMetadata metadata;
    metadata.type_name = element.type_name;
    metadata.display_name = element.display_name.empty() ? element.type_name : element.display_name;
    metadata.description = element.description;
    metadata.category = element.category.empty() ? library.name : element.category;
    metadata.version = element.version.empty() ? "1.0.0" : element.version;
    metadata.visual = {"box", "#7E57C2", 120, 60, ""};
    metadata.schema_definition = element.schema;
    metadata.input_ports = build_port_specs(inputs, false);
    metadata.output_ports = build_port_specs(outputs, true);
    metadata.tags = {"schema-library"};
    return metadata;
}

} // namespace

bool configure_schema_discretization(boost::json::value& schema, const double dt, const ElementRegistry& registry)
{
    (void)Rate::from_seconds(dt);
    if (!schema.is_object()) throw std::invalid_argument("Expected a schema object for discretization.");
    auto& root=schema.as_object();
    root["dt"]=dt;
    auto* elements=root.if_contains("elements");
    if (!elements || !elements->is_array()) throw std::invalid_argument("Expected schema elements array for discretization.");
    for (auto& value : elements->as_array()) {
        auto& element=value.as_object();
        const auto* type=element.if_contains("type");
        const auto* descriptor=type && type->is_string() ? registry.find(std::string(type->as_string())) : nullptr;
        if (!element.if_contains("schema") && descriptor && descriptor->metadata().schema_definition)
            element["schema"]=*descriptor->metadata().schema_definition;
        if (auto* nested=element.if_contains("schema"); nested && nested->is_object())
            configure_schema_discretization(*nested,dt,registry);
        if (!descriptor) continue;
        for (const auto& parameter : descriptor->metadata().parameters) {
            if (parameter.name!="dt" && parameter.name!="sample_rate_hz") continue;
            if (!element.if_contains("params")) element["params"]=boost::json::object{};
            element["params"].as_object()[parameter.name]=parameter.name=="dt" ? dt : 1/dt;
        }
    }
    return true;
}

auto SchemaLibraryDefinition::to_json() const -> boost::json::value
{
    boost::json::object root;
    root["document_kind"] = "s7.element_library";
    root["name"] = name;
    root["elements"] = boost::json::array{};

    auto& elements_array = root["elements"].as_array();
    for (const auto& element : elements) {
        boost::json::object element_object;
        element_object["type_name"] = element.type_name;
        element_object["display_name"] = element.display_name;
        element_object["description"] = element.description;
        element_object["category"] = element.category;
        element_object["version"] = element.version;
        element_object["schema"] = element.schema;
        elements_array.push_back(std::move(element_object));
    }

    return root;
}

auto SchemaLibraryLoader::load_from_file(const std::filesystem::path& file_path) -> SchemaLibraryDefinition
{
    const auto contents = read_file_contents(file_path);
    boost::system::error_code ec;
    const auto parsed = boost::json::parse(contents, ec);
    if (ec) {
        throw std::runtime_error("JSON parsing failed for element library '" + file_path.string() + "': " + ec.message());
    }

    return load_from_json(parsed);
}

auto SchemaLibraryLoader::load_from_json(const boost::json::value& json_value) -> SchemaLibraryDefinition
{
    const auto& root = expect_object(json_value, "root");
    const auto kind = string_value(root, "document_kind");
    if (kind != "s7.element_library") {
        throw std::invalid_argument("JSON document is not an element library.");
    }

    SchemaLibraryDefinition library;
    library.name = string_value(root, "name", "element_library");

    const auto& elements = expect_array(root, "elements");
    int fallback_index = 0;
    for (const auto& element_value : elements) {
        const auto& element = expect_object(element_value, "library element");
        SchemaLibraryElementDefinition definition;
        definition.type_name = string_value(element, "type_name");
        if (definition.type_name.empty()) {
            definition.type_name = string_value(element, "id");
        }
        if (definition.type_name.empty()) {
            definition.type_name = string_value(element, "name");
        }
        if (definition.type_name.empty()) {
            definition.type_name = "library_element_" + std::to_string(fallback_index++);
        }
        definition.display_name = string_value(element, "display_name", definition.type_name);
        definition.description = string_value(element, "description");
        definition.category = string_value(element, "category", library.name);
        definition.version = string_value(element, "version", "1.0.0");

        const auto* schema = optional_object(element, "schema");
        if (!schema) {
            throw std::invalid_argument("Library element '" + definition.type_name + "' is missing a schema definition.");
        }
        definition.schema = boost::json::value(*schema);
        library.elements.push_back(std::move(definition));
    }

    return library;
}

void SchemaLibraryLoader::register_library(const SchemaLibraryDefinition& definition, ElementRegistry& registry)
{
    std::unordered_set<std::string> seen;
    seen.reserve(definition.elements.size());

    for (const auto& element : definition.elements) {
        if (!seen.insert(element.type_name).second) {
            throw std::invalid_argument("Duplicate schema library element type: '" + element.type_name + "'.");
        }
        if (registry.find(element.type_name)) {
            throw std::invalid_argument("Element type already registered: '" + element.type_name + "'.");
        }
    }

    for (const auto& element : definition.elements) {
        const auto metadata = library_element_metadata(definition, element);
        const auto schema_definition = element.schema;
        registry.register_element(ElementDescriptor(
            element.type_name,
            [schema_definition, &registry](const ElementCreationContext&) {
                auto schema = SchemaLibraryLoader::build_schema(schema_definition, registry);
                return std::make_unique<Schema>(std::move(schema));
            },
            metadata));
    }
}

void SchemaLibraryLoader::register_library_file(const std::filesystem::path& file_path, ElementRegistry& registry)
{
    register_library(load_from_file(file_path), registry);
}

auto SchemaLibraryLoader::build_schema(const boost::json::value& schema_json, const ElementRegistry& registry) -> Schema
{
    if (!schema_json.is_object()) {
        throw std::invalid_argument("Schema root must be a JSON object.");
    }

    return build_schema_from_object(schema_json.as_object(), registry);
}

} // namespace s7





