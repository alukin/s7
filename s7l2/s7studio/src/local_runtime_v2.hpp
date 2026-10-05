#pragma once

#include <s7/core.hpp>
#include <s7/observe.hpp>
#include <s7/element_registry.hpp>
#include <s7/element_catalog.hpp>
#include <s7/net.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <span>
#include <cstring>
#include <array>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace s7studio::runtime {

inline s7::Schema buildSchema(const boost::json::value& schemaJson);

namespace detail {

// Studio observer: one channel per input, any real type (displayed with
// ObservationSample::to_display_double()). Each output mirrors its input so
// the runner can sample it.
class ScopeElement final : public s7::Element
{
public:
    explicit ScopeElement(std::vector<s7::SignalType> channelTypes)
        : s7::Element("Scope")
        , channels_(channelTypes.size())
    {
        for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
            channels_[channel].bytes = s7::signal_type_info(channelTypes[channel]).storage_bytes;
            const std::string portName = "ch" + std::to_string(channel);
            register_input(channel, portName, channelTypes[channel], nullptr);
            register_output(channel, portName, channelTypes[channel], channels_[channel].value.data());
        }
    }

    explicit ScopeElement(std::size_t channelCount)
        : ScopeElement(std::vector<s7::SignalType>(std::max<std::size_t>(1, channelCount), s7::SignalType::F64))
    {
    }

    void step() override
    {
        for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
            std::memcpy(channels_[channel].value.data(), input_binding(channel), channels_[channel].bytes);
        }
    }

private:
    struct Channel {
        alignas(16) std::array<std::byte, 32> value{};
        std::size_t bytes{};
    };
    std::vector<Channel> channels_;
};

// A Scope whose channel types are inferred: one type variable per channel.
inline s7::GenericElement makeScopeElement(const std::size_t channelCount)
{
    s7::GenericElement scope;
    for (std::size_t channel = 0; channel < std::max<std::size_t>(1, channelCount); ++channel) {
        scope.variables.push_back({"ch" + std::to_string(channel), s7::signal_type_sets::real, std::nullopt});
        scope.input_rules.push_back(s7::PortTypeRule::of_variable(channel));
        scope.output_rules.push_back(s7::PortTypeRule::of_variable(channel));
    }
    scope.create = [](std::span<const s7::SignalType> types) {
        return std::make_unique<ScopeElement>(std::vector<s7::SignalType>(types.begin(), types.end()));
    };
    return scope;
}

inline const boost::json::object& expectObject(const boost::json::value& value, std::string_view name)
{
    if (!value.is_object()) {
        throw std::invalid_argument("Expected object for '" + std::string(name) + "'.");
    }
    return value.as_object();
}

inline const boost::json::array& expectArray(const boost::json::object& object, std::string_view key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_array()) {
        throw std::invalid_argument("Expected array field '" + std::string(key) + "'.");
    }
    return it->value().as_array();
}

inline std::string expectString(const boost::json::object& object, std::string_view key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_string()) {
        throw std::invalid_argument("Expected string field '" + std::string(key) + "'.");
    }
    return it->value().as_string().c_str();
}

inline double getDouble(const boost::json::object& object, std::string_view key, double default_value)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return default_value;
    }
    if (it->value().is_double()) {
        return it->value().as_double();
    }
    if (it->value().is_int64()) {
        return static_cast<double>(it->value().as_int64());
    }
    if (it->value().is_uint64()) {
        return static_cast<double>(it->value().as_uint64());
    }
    throw std::invalid_argument("Expected numeric field '" + std::string(key) + "'.");
}

inline std::size_t getSizeT(const boost::json::object& object, std::string_view key, std::size_t default_value)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return default_value;
    }
    if (it->value().is_uint64()) {
        return static_cast<std::size_t>(it->value().as_uint64());
    }
    if (it->value().is_int64()) {
        const auto value = it->value().as_int64();
        if (value < 0) {
            throw std::invalid_argument("Field '" + std::string(key) + "' must be non-negative.");
        }
        return static_cast<std::size_t>(value);
    }
    throw std::invalid_argument("Expected integer field '" + std::string(key) + "'.");
}

inline s7::SignalType signalTypeFromString(std::string_view value)
{
    return s7::parse_signal_type(value);
}

struct SchemaPortBinding {
    std::string name;
    std::string type;
    std::string element;
    std::size_t port{};
};

inline std::vector<SchemaPortBinding> parseSchemaPortBindings(const boost::json::object& root,
                                                              std::string_view field_name)
{
    std::vector<SchemaPortBinding> bindings;
    const auto it = root.find(field_name);
    if (it == root.end()) {
        return bindings;
    }
    if (!it->value().is_array()) {
        throw std::invalid_argument("Expected array field '" + std::string(field_name) + "'.");
    }

    for (const auto& binding_value : it->value().as_array()) {
        if (!binding_value.is_object()) {
            throw std::invalid_argument("Expected object in field '" + std::string(field_name) + "'.");
        }

        const auto& binding_object = binding_value.as_object();
        SchemaPortBinding binding;
        binding.name = expectString(binding_object, "name");
        binding.type = expectString(binding_object, "type");
        binding.element = expectString(binding_object, "element");
        if (const auto port_it = binding_object.find("port"); port_it != binding_object.end()) {
            if (port_it->value().is_uint64()) {
                binding.port = port_it->value().as_uint64();
            } else if (port_it->value().is_int64() && port_it->value().as_int64() >= 0) {
                binding.port = static_cast<std::size_t>(port_it->value().as_int64());
            } else {
                throw std::invalid_argument("Expected non-negative integer 'port' in schema port binding.");
            }
        }
        bindings.push_back(std::move(binding));
    }

    return bindings;
}

// Create element from registry
inline void addElementFromRegistry(s7::Schema& schema,
                                   const std::string& id,
                                   const std::string& type,
                                   const boost::json::object& params)
{
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);

    const auto* descriptor = registry.find(type);
    if (!descriptor) {
        throw std::invalid_argument("Unknown element type: '" + type + "'");
    }

    schema.add_element(s7::make_element_spec(id, *descriptor, params));
}

inline void addNestedSchemaElement(s7::Schema& schema,
                                   const std::string& id,
                                   const boost::json::value& nested_schema)
{
    const auto nested_definition = nested_schema;
    schema.add_element(id, [nested_definition] {
        auto nested = buildSchema(nested_definition);
        return std::make_unique<s7::Schema>(std::move(nested));
    });
}

// Fallback for Scope element (special s7studio element)
inline void addElementFromJson(s7::Schema& schema, const boost::json::object& elementObj)
{
    const auto id = expectString(elementObj, "id");
    const auto type = expectString(elementObj, "type");
    const auto paramsIt = elementObj.find("params");
    const auto& params = paramsIt != elementObj.end() && paramsIt->value().is_object()
        ? paramsIt->value().as_object()
        : boost::json::object{};

    const auto schemaIt = elementObj.find("schema");
    const auto hasNestedSchema = schemaIt != elementObj.end() && schemaIt->value().is_object();

    // Handle Scope element (special case for s7studio)
    if (type == "Scope") {
        const std::size_t channelCount = std::max<std::size_t>(1, getSizeT(params, "channel_count", 1));
        schema.add_element(id, makeScopeElement(channelCount));
        return;
    }

    if (hasNestedSchema) {
        addNestedSchemaElement(schema, id, schemaIt->value());
        return;
    }

    // Try to create from registry
    try {
        addElementFromRegistry(schema, id, type, params);
    } catch (const std::exception& e) {
        throw std::invalid_argument("Failed to add element '" + id + "' of type '" + type + "': " + e.what());
    }
}

} // namespace detail

inline s7::Schema buildSchema(const boost::json::value& schemaJson)
{
    if (!schemaJson.is_object()) {
        throw std::invalid_argument("Schema root must be a JSON object.");
    }

    const auto& root = detail::expectObject(schemaJson, "root");
    const auto name = root.if_contains("name") && root.at("name").is_string()
        ? root.at("name").as_string().c_str()
        : std::string{"s7studio_demo"};

    s7::Schema schema{name};

    const auto elementsIt = root.find("elements");
    if (elementsIt == root.end()) {
        throw std::invalid_argument("Schema must contain an 'elements' array.");
    }

    for (const auto& elementValue : detail::expectArray(root, "elements")) {
        detail::addElementFromJson(schema, detail::expectObject(elementValue, "element"));
    }

    const auto connectionsIt = root.find("connections");
    if (connectionsIt != root.end()) {
        for (const auto& connectionValue : detail::expectArray(root, "connections")) {
            const auto& connectionObj = detail::expectObject(connectionValue, "connection");
            schema.connect(detail::expectString(connectionObj, "from_element"),
                           detail::getSizeT(connectionObj, "from_port", 0),
                           detail::expectString(connectionObj, "to_element"),
                           detail::getSizeT(connectionObj, "to_port", 0));
        }
    }

    for (const auto& binding : detail::parseSchemaPortBindings(root, "exposed_inputs")) {
        schema.expose_input(binding.name,
                            detail::signalTypeFromString(binding.type),
                            binding.element,
                            binding.port);
    }

    for (const auto& binding : detail::parseSchemaPortBindings(root, "exposed_outputs")) {
        schema.expose_output(binding.name,
                              detail::signalTypeFromString(binding.type),
                              binding.element,
                              binding.port);
    }

    return schema;
}

inline std::vector<std::string> collectSamples(const s7::Schema& schema, const s7::NodeRuntime& runtime)
{
    s7::ObservationService observation(runtime);
    std::vector<std::string> lines;
    lines.reserve(schema.elements().size());

    for (const auto& element : schema.elements()) {
        const auto sample = observation.sample_output(element.id, 0);
        std::ostringstream out;
        out << element.id << '[' << sample.port << "]=";
        if (sample.is_complex()) {
            out << '<' << s7::signal_type_name(sample.type) << '>';
        } else {
            out << sample.to_display_double();
        }
        lines.push_back(out.str());
    }

    return lines;
}

inline void validateSchema(const s7::Schema& schema)
{
    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        std::ostringstream out;
        for (const auto& error : validation.errors) {
            out << error << '\n';
        }
        throw std::runtime_error(out.str());
    }
}

// Get available element types for UI
inline std::vector<std::string> getAvailableElementTypes()
{
    auto& registry = s7::ElementRegistry::instance();
    return registry.element_types();
}

// Get element catalog for UI
inline s7::ElementCatalogService getElementCatalog()
{
    auto& registry = s7::ElementRegistry::instance();
    return s7::ElementCatalogService(registry);
}

} // namespace s7studio::runtime

