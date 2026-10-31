// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "s7/metadata_json.hpp"
#include "s7/element_registry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace s7 {
namespace {
using boost::json::object;
using boost::json::value;
[[noreturn]] void invalid(const std::string& context, const std::string& message)
{
    throw std::invalid_argument("Element metadata " + context + ": " + message);
}
void keys(const object& object, std::initializer_list<std::string_view> allowed, const std::string& context)
{
    for (const auto& field : object)
        if (std::find(allowed.begin(), allowed.end(), std::string_view(field.key())) == allowed.end())
            invalid(context, "unknown field '" + std::string(field.key()) + "'");
}
const value& required(const object& object, std::string_view key, std::string_view context)
{
    const auto* field = object.if_contains(key);
    if (!field) invalid(std::string(context), "missing field '" + std::string(key) + "'");
    return *field;
}
std::string string(const object& object, std::string_view key, const std::string& context, bool optional = false)
{
    const auto* field = object.if_contains(key);
    if (!field && optional) return {};
    if (!field || !field->is_string()) invalid(context, "'" + std::string(key) + "' must be a string");
    return std::string(field->as_string());
}
bool boolean(const object& object, std::string_view key, const std::string& context)
{
    const auto& field = required(object, key, context);
    if (!field.is_bool()) invalid(context, "'" + std::string(key) + "' must be a boolean");
    return field.as_bool();
}
std::uint64_t count(const object& object, std::string_view key, const std::string& context)
{
    const auto& field = required(object, key, context);
    if (field.is_uint64()) return field.as_uint64();
    if (field.is_int64() && field.as_int64() >= 0) return static_cast<std::uint64_t>(field.as_int64());
    invalid(context, "'" + std::string(key) + "' must be a nonnegative integer");
}
const boost::json::array& array(const object& object, std::string_view key, std::string_view context)
{
    const auto& field = required(object, key, context);
    if (!field.is_array()) invalid(std::string(context), "'" + std::string(key) + "' must be an array");
    return field.as_array();
}
const object& as_object(const value& field, std::string_view context)
{
    if (!field.is_object()) invalid(std::string(context), "expected object");
    return field.as_object();
}
std::vector<std::string> strings(const object& object, std::string_view key, const std::string& context)
{
    std::vector<std::string> result;
    for (const auto& field : array(object, key, context)) {
        if (!field.is_string()) invalid(context, "'" + std::string(key) + "' must contain strings");
        result.emplace_back(field.as_string());
    }
    return result;
}
void unique(std::set<std::string>& names, const std::string& name, const std::string& context)
{
    if (name.empty() || !names.insert(name).second) invalid(context, "empty or duplicate name '" + name + "'");
}
std::vector<PortSpec> ports(const object& source, const char* key, const std::string& context)
{
    std::vector<PortSpec> result;
    std::set<std::string> names;
    for (const auto& field : array(source, key, context)) {
        const auto& p = as_object(field, context + "." + key);
        keys(p, {"name", "description", "type", "optional", "type_rule"}, context);
        PortSpec port;
        port.name = string(p, "name", context);
        unique(names, port.name, context + "." + key);
        port.description = string(p, "description", context);
        port.type = parse_signal_type(string(p, "type", context));
        port.optional = boolean(p, "optional", context);
        port.type_rule = string(p, "type_rule", context);
        result.push_back(std::move(port));
    }
    return result;
}
ElementMetadata decode(const object& source)
{
    ElementMetadata metadata;
    metadata.type_name = string(source, "type_name", "element");
    const auto& context = metadata.type_name;
    keys(source, {"type_name", "display_name", "description", "category", "version", "tags", "input_ports", "output_ports",
                  "parameters", "type_variables", "visual", "variadic_inputs"}, context);
    metadata.display_name = string(source, "display_name", context);
    metadata.description = string(source, "description", context);
    metadata.category = string(source, "category", context);
    metadata.version = string(source, "version", context);
    metadata.tags = strings(source, "tags", context);
    metadata.input_ports = ports(source, "input_ports", context);
    metadata.output_ports = ports(source, "output_ports", context);
    std::set<std::string> parameter_names;
    for (const auto& field : array(source, "parameters", context)) {
        const auto& p = as_object(field, context + ".parameters");
        ParameterSpec parameter;
        parameter.name = string(p, "name", context);
        const auto label = context + "." + parameter.name;
        keys(p, {"name", "type", "description", "default", "required", "min", "max", "enum_values", "legacy"}, label);
        unique(parameter_names, parameter.name, label);
        parameter.type = string(p, "type", label);
        if (parameter.type != "real" && parameter.type != "int" && parameter.type != "bool" && parameter.type != "string" &&
            parameter.type != "enum" && parameter.type != "type" && parameter.type != "array" && parameter.type != "object" && parameter.type != "value")
            invalid(label, "unknown parameter type '" + parameter.type + "'");
        parameter.description = string(p, "description", label);
        parameter.default_value = required(p, "default", label);
        parameter.required = boolean(p, "required", label);
        parameter.legacy = p.contains("legacy") && boolean(p, "legacy", label);
        parameter.enum_values = strings(p, "enum_values", label);
        for (const auto* key : {"min", "max"}) if (const auto* bound = p.if_contains(key)) {
            if (!bound->is_number() || !std::isfinite(bound->to_number<double>())) invalid(label, "bounds must be finite numbers");
            (std::string_view(key) == "min" ? parameter.min_value : parameter.max_value) = bound->to_number<double>();
        }
        if (parameter.min_value && parameter.max_value && *parameter.min_value > *parameter.max_value)
            invalid(label, "minimum exceeds maximum");
        if (!parameter.default_value.is_null()) {
            if (parameter.type == "object" && !parameter.default_value.is_object()) invalid(label, "default must be an object");
            ElementMetadata check;
            check.parameters.push_back(parameter);
            static_cast<void>(checked_params(label, check, {{parameter.name, parameter.default_value}}));
        }
        metadata.parameters.push_back(std::move(parameter));
    }
    std::set<std::string> variable_names;
    for (const auto& field : array(source, "type_variables", context)) {
        const auto& v = as_object(field, context + ".type_variables");
        keys(v, {"name", "allowed", "pin_parameter", "defaults_to"}, context);
        TypeVariableSpec variable;
        variable.name = string(v, "name", context);
        unique(variable_names, variable.name, context + ".type_variables");
        std::set<SignalType> allowed;
        for (const auto& type : strings(v, "allowed", context)) {
            const auto parsed = parse_signal_type(type);
            if (!allowed.insert(parsed).second) invalid(context, "duplicate allowed signal type");
            variable.allowed.push_back(parsed);
        }
        if (variable.allowed.empty()) invalid(context, "type variable has no allowed types");
        variable.pin_parameter = string(v, "pin_parameter", context);
        variable.defaults_to = string(v, "defaults_to", context);
        const auto pin = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
            [&](const auto& p) { return p.name == variable.pin_parameter && p.type == "type"; });
        if (pin == metadata.parameters.end()) invalid(context, "type variable pin is not a declared type parameter");
        metadata.type_variables.push_back(std::move(variable));
    }
    for (const auto& variable : metadata.type_variables)
        if (!variable.defaults_to.empty() && !variable_names.contains(variable.defaults_to)) invalid(context, "unknown type-variable default");
    const auto check_rule = [&](const std::string& rule) {
        if (rule.empty()) return;
        auto name = rule;
        for (const auto* prefix : {"component(", "complex("})
            if (name.starts_with(prefix) && name.ends_with(')')) { name = name.substr(std::string_view(prefix).size(), name.size()-std::string_view(prefix).size()-1); break; }
        if (!variable_names.contains(name)) invalid(context, "unknown port type rule '" + rule + "'");
    };
    for (const auto& port : metadata.input_ports) check_rule(port.type_rule);
    for (const auto& port : metadata.output_ports) check_rule(port.type_rule);
    const auto& visual = as_object(required(source, "visual", context), context + ".visual");
    keys(visual, {"shape", "color", "max_width", "max_height", "icon_name"}, context);
    metadata.visual.shape = string(visual, "shape", context);
    metadata.visual.color = string(visual, "color", context);
    metadata.visual.icon_name = string(visual, "icon_name", context);
    const auto width = count(visual, "max_width", context), height = count(visual, "max_height", context);
    if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) invalid(context, "visual dimensions exceed integer range");
    metadata.visual.max_width = static_cast<int>(width); metadata.visual.max_height = static_cast<int>(height);
    const auto& v = as_object(required(source, "variadic_inputs", context), context + ".variadic_inputs");
    keys(v, {"enabled", "min_count", "max_count", "port_name_template", "signal_type", "type_rule", "count_parameter"}, context);
    auto& inputs = metadata.variadic_inputs;
    inputs.enabled = boolean(v, "enabled", context);
    inputs.min_count = count(v, "min_count", context); inputs.max_count = count(v, "max_count", context);
    inputs.port_name_template = string(v, "port_name_template", context);
    inputs.signal_type = parse_signal_type(string(v, "signal_type", context));
    inputs.type_rule = string(v, "type_rule", context);
    inputs.count_parameter = string(v, "count_parameter", context);
    check_rule(inputs.type_rule);
    if (inputs.enabled && (inputs.min_count > inputs.max_count || !parameter_names.contains(inputs.count_parameter)))
        invalid(context, "invalid variadic range or undeclared count parameter");
    return metadata;
}
}
ElementMetadataLibrary::ElementMetadataLibrary(std::string_view json)
{
    const auto document = boost::json::parse(json);
    const auto& source = as_object(document, "document");
    keys(source, {"document_kind", "format_version", "library", "elements"}, "document");
    if (string(source, "document_kind", "document") != "s7.element_metadata" || count(source, "format_version", "document") != 1)
        invalid("document", "unsupported document kind or format version");
    const auto library = source.contains("library") ? string(source, "library", "document") : std::string{};
    for (const auto& field : array(source, "elements", "document")) {
        auto metadata = decode(as_object(field, "element"));
        metadata.library = library;
        const auto name = metadata.type_name;
        if (name.empty() || !elements_.emplace(name, std::move(metadata)).second) invalid("document", "empty or duplicate type '" + name + "'");
    }
}
const ElementMetadata& ElementMetadataLibrary::at(std::string_view type) const
{
    const auto found = elements_.find(type);
    if (found == elements_.end()) invalid(std::string(type), "no matching definition in resource");
    return found->second;
}
}
