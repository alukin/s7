#pragma once

#include <s7/core.hpp>
#include <s7/observe.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace s7studio::runtime {

namespace detail {

class ScopeElement final : public s7::Element
{
public:
    explicit ScopeElement(std::size_t channelCount)
        : s7::Element("Scope")
        , outputs_(std::max<std::size_t>(1, channelCount), 0.0)
    {
        const std::size_t effectiveChannelCount = outputs_.size();
        for (std::size_t channel = 0; channel < effectiveChannelCount; ++channel) {
            const std::string portName = "ch" + std::to_string(channel);
            register_input(channel, portName, s7::SignalType::DOUBLE, nullptr);
            register_output(channel, portName, s7::SignalType::DOUBLE, &outputs_[channel]);
        }
    }

    void step() override
    {
        for (std::size_t channel = 0; channel < outputs_.size(); ++channel) {
            outputs_[channel] = read_input<s7::RealValue>(channel);
        }
    }

private:
    std::vector<s7::RealValue> outputs_;
};

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

inline void addElementFromJson(s7::Schema& schema, const boost::json::object& elementObj)
{
    const auto id = expectString(elementObj, "id");
    const auto type = expectString(elementObj, "type");
    const auto paramsIt = elementObj.find("params");

    if (type == "Constant") {
        const double value = paramsIt != elementObj.end() && paramsIt->value().is_object()
            ? getDouble(paramsIt->value().as_object(), "value", 0.0)
            : 0.0;
        schema.add_element(id, [value] { return std::make_unique<s7::Constant>(value); });
        return;
    }

    if (type == "Adder") {
        schema.add_element(id, [] { return std::make_unique<s7::Adder>(); });
        return;
    }

    if (type == "Multiply") {
        schema.add_element(id, [] { return std::make_unique<s7::Multiply>(); });
        return;
    }

    if (type == "Delay") {
        const double initial_value = paramsIt != elementObj.end() && paramsIt->value().is_object()
            ? getDouble(paramsIt->value().as_object(), "initial_value", 0.0)
            : 0.0;
        schema.add_element(id, [initial_value] { return std::make_unique<s7::Delay>(initial_value); });
        return;
    }

    if (type == "Integrator") {
        const double initial_value = paramsIt != elementObj.end() && paramsIt->value().is_object()
            ? getDouble(paramsIt->value().as_object(), "initial_value", 0.0)
            : 0.0;
        schema.add_element(id, [initial_value] { return std::make_unique<s7::Integrator>(initial_value); });
        return;
    }

    if (type == "AnalogOscillator") {
        const auto& params = paramsIt != elementObj.end() && paramsIt->value().is_object()
            ? paramsIt->value().as_object()
            : boost::json::object{};
        const double amplitude = getDouble(params, "amplitude", 1.0);
        const double frequency_hz = getDouble(params, "frequency_hz", 1.0);
        const double sample_rate_hz = getDouble(params, "sample_rate_hz", 1.0);
        const double phase = getDouble(params, "phase", 0.0);
        schema.add_element(id, [amplitude, frequency_hz, sample_rate_hz, phase] {
            return std::make_unique<s7::AnalogOscillator>(amplitude, frequency_hz, sample_rate_hz, phase);
        });
        return;
    }

    if (type == "Scope") {
        const auto& params = paramsIt != elementObj.end() && paramsIt->value().is_object()
            ? paramsIt->value().as_object()
            : boost::json::object{};
        const std::size_t channelCount = std::max<std::size_t>(1, getSizeT(params, "channel_count", 1));
        schema.add_element(id, [channelCount] { return std::make_unique<ScopeElement>(channelCount); });
        return;
    }

    throw std::invalid_argument("Unsupported runtime element type: '" + type + "'.");
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
            out << sample.as_complex();
        } else {
            out << sample.as_real();
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

} // namespace s7studio::runtime
