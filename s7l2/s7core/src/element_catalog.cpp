#include "s7/element_catalog.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace s7 {

ElementCatalogService::ElementCatalogService(const ElementRegistry& registry)
    : registry_(registry)
{
}

void ElementCatalogService::build_entries() const
{
    entries_.clear();

    for (const auto& descriptor : registry_.all_descriptors()) {
        const auto& metadata = descriptor.metadata();

        ElementCatalogEntry entry{
            .type_name = metadata.type_name,
            .display_name = metadata.display_name,
            .description = metadata.description,
            .category = metadata.category,
            .version = metadata.version,
            .input_count = static_cast<int>(metadata.input_ports.size()),
            .output_count = static_cast<int>(metadata.output_ports.size())
        };

        entries_.push_back(entry);
    }

    std::sort(entries_.begin(), entries_.end(), [](const ElementCatalogEntry& a, const ElementCatalogEntry& b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.display_name < b.display_name;
    });

    entries_cached_ = true;
}

std::vector<ElementCatalogEntry> ElementCatalogService::all_elements() const
{
    if (!entries_cached_) {
        build_entries();
    }
    return entries_;
}

std::vector<ElementCatalogEntry> ElementCatalogService::elements_in_category(const std::string& category) const
{
    auto entries = all_elements();

    std::vector<ElementCatalogEntry> result;
    for (const auto& entry : entries) {
        if (entry.category == category) {
            result.push_back(entry);
        }
    }

    return result;
}

std::vector<std::string> ElementCatalogService::categories() const
{
    auto entries = all_elements();
    std::set<std::string> cats;

    for (const auto& entry : entries) {
        cats.insert(entry.category);
    }

    return {cats.begin(), cats.end()};
}

const ElementCatalogEntry* ElementCatalogService::find_element(const std::string& type_name) const
{
    if (!entries_cached_) {
        build_entries();
    }

    for (const auto& entry : entries_) {
        if (entry.type_name == type_name) {
            return &entry;
        }
    }

    return nullptr;
}

boost::json::value ElementCatalogService::to_json() const
{
    boost::json::object result;

    for (const auto& descriptor : registry_.all_descriptors()) {
        const auto& metadata = descriptor.metadata();

        boost::json::object element;
        element["type_name"] = metadata.type_name;
        element["display_name"] = metadata.display_name;
        element["description"] = metadata.description;
        element["version"] = metadata.version;
        element["input_count"] = static_cast<int>(metadata.input_ports.size());
        element["output_count"] = static_cast<int>(metadata.output_ports.size());
        element["schema_based"] = metadata.schema_definition.has_value();
        if (metadata.schema_definition.has_value()) {
            element["schema"] = *metadata.schema_definition;
        }

        boost::json::array inputPorts;
        for (const auto& port : metadata.input_ports) {
            boost::json::object portJson;
            portJson["name"] = port.name;
            portJson["description"] = port.description;
            portJson["type"] = std::string(signal_type_name(port.type));
            portJson["optional"] = port.optional;
            inputPorts.push_back(portJson);
        }
        element["input_ports"] = inputPorts;

        boost::json::array outputPorts;
        for (const auto& port : metadata.output_ports) {
            boost::json::object portJson;
            portJson["name"] = port.name;
            portJson["description"] = port.description;
            portJson["type"] = std::string(signal_type_name(port.type));
            portJson["optional"] = port.optional;
            outputPorts.push_back(portJson);
        }
        element["output_ports"] = outputPorts;

        boost::json::array parameters;
        for (const auto& parameter : metadata.parameters) {
            boost::json::object parameterJson;
            parameterJson["name"] = parameter.name;
            parameterJson["type"] = parameter.type;
            parameterJson["description"] = parameter.description;
            parameterJson["default"] = parameter.default_value;
            parameterJson["required"] = parameter.required;
            if (parameter.min_value.has_value()) {
                parameterJson["min"] = *parameter.min_value;
            }
            if (parameter.max_value.has_value()) {
                parameterJson["max"] = *parameter.max_value;
            }
            if (!parameter.enum_values.empty()) {
                boost::json::array enumValues;
                for (const auto& value : parameter.enum_values) {
                    enumValues.push_back(boost::json::value(value));
                }
                parameterJson["enum_values"] = enumValues;
            }
            parameters.push_back(parameterJson);
        }
        element["parameters"] = parameters;

        if (metadata.variadic_inputs.enabled) {
            boost::json::object variadic;
            variadic["enabled"] = true;
            variadic["min_count"] = static_cast<std::uint64_t>(metadata.variadic_inputs.min_count);
            variadic["max_count"] = static_cast<std::uint64_t>(metadata.variadic_inputs.max_count);
            variadic["port_name_template"] = metadata.variadic_inputs.port_name_template;
            variadic["signal_type"] = std::string(signal_type_name(metadata.variadic_inputs.signal_type));
            element["variadic_inputs"] = variadic;
        }

        auto& categoryElements = result[metadata.category];
        if (!categoryElements.is_array()) {
            categoryElements = boost::json::array{};
        }
        categoryElements.as_array().push_back(element);
    }

    return result;
}

} // namespace s7

