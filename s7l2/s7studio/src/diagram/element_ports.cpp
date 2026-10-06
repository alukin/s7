#include "diagram/element_ports.hpp"

#include "json_utils.hpp"

#include <s7/element_registry.hpp>

#include <algorithm>
#include <string>

namespace s7studio::diagram {

namespace {

auto exposedNames(const boost::json::object& schema, const char* key, const char* prefix) -> std::vector<QString>
{
    std::vector<QString> names;
    if (const auto* ports = json_utils::findObjectValue(schema, key); ports && ports->is_array()) {
        for (const auto& port : ports->as_array()) {
            const auto name = port.is_object() ? json_utils::stringValue(port.as_object(), "name", "") : std::string{};
            names.push_back(name.empty() ? QStringLiteral("%1%2").arg(prefix).arg(names.size()) : QString::fromStdString(name));
        }
    }
    return names;
}

auto numbered(const char* prefix, const int count) -> std::vector<QString>
{
    std::vector<QString> names;
    for (int index = 0; index < count; ++index) {
        names.push_back(QStringLiteral("%1%2").arg(prefix).arg(index));
    }
    return names;
}

// Ports of the registry metadata, `count` (if larger) extended with numbered names.
auto metadataNames(const std::vector<s7::PortSpec>& specs, const char* prefix, const int count) -> std::vector<QString>
{
    std::vector<QString> names;
    for (const auto& spec : specs) {
        names.push_back(QString::fromStdString(spec.name));
    }
    while (static_cast<int>(names.size()) < count) {
        names.push_back(QStringLiteral("%1%2").arg(prefix).arg(names.size()));
    }
    return names;
}

auto arraySize(const boost::json::object& params, const char* key) -> int
{
    if (const auto* value = json_utils::findObjectValue(params, key); value && value->is_array()) {
        return static_cast<int>(value->as_array().size());
    }
    return 0;
}

} // namespace

auto elementPorts(const std::string_view type, const boost::json::object& params, const boost::json::value& nestedSchema)
    -> ElementPorts
{
    if (nestedSchema.is_object()) {
        return {exposedNames(nestedSchema.as_object(), "exposed_inputs", "in"),
                exposedNames(nestedSchema.as_object(), "exposed_outputs", "out")};
    }
    if (type == "Scope") {
        return {numbered("ch", std::max(1, json_utils::intValue(params, "channel_count", 1))), {}};
    }

    int variadicInputs = 0;
    if (type == "Adder") {
        variadicInputs = std::max(2, arraySize(params, "weights"));
    } else if (type == "Multiply") {
        variadicInputs = std::max(2, arraySize(params, "factors"));
    }

    if (const auto* descriptor = s7::ElementRegistry::instance().find(std::string(type))) {
        const auto& metadata = descriptor->metadata();
        return {metadataNames(metadata.input_ports, "in", variadicInputs), metadataNames(metadata.output_ports, "out", 0)};
    }

    // Built-ins described here for diagrams opened before (or without) the
    // registry: the ports their schemas use.
    if (type == "Constant" || type == "AnalogOscillator") {
        return {{}, numbered("out", 1)};
    }
    if (type == "Gain" || type == "Delay" || type == "Accumulator" || type == "FirFilter" || type == "IirFilter" ||
        type == "PreviousValue" || type == "Convert") {
        return {numbered("in", 1), numbered("out", 1)};
    }
    if (variadicInputs > 0) {
        return {numbered("in", variadicInputs), numbered("out", 1)};
    }
    if (type == "ReImToComplex") {
        return {numbered("in", 2), numbered("out", 1)};
    }
    if (type == "ComplexToReIm" || type == "ComplexToMagnitudePhase") {
        return {numbered("in", 1), numbered("out", 2)};
    }
    return {};
}

} // namespace s7studio::diagram
