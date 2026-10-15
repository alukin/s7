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
    if (type == "Scope") {
        const int count = std::max(0, json_utils::intValue(params, "channel_count", 0));
        auto inputs = numbered("signal", count);
        inputs.push_back(QStringLiteral("+")); // connection target that grows the observer
        return {std::move(inputs), {}};
    }
    if (type == "PhasePortrait") {
        return {{QStringLiteral("x"), QStringLiteral("y")}, {}};
    }
    if (type == "PhaseTrajectory3D") {
        return {{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}, {}};
    }
    if (nestedSchema.is_object()) {
        return {exposedNames(nestedSchema.as_object(), "exposed_inputs", "in"),
                exposedNames(nestedSchema.as_object(), "exposed_outputs", "out")};
    }
    int variadicInputs = 0;
    if (type == "Adder") {
        variadicInputs = std::max(2, arraySize(params, "weights"));
    } else if (type == "Multiply") {
        variadicInputs = std::max(2, arraySize(params, "factors"));
    }

    if (const auto* descriptor = s7::ElementRegistry::instance().find(std::string(type))) {
        const auto& metadata = descriptor->metadata();
        int inputCount = static_cast<int>(metadata.input_ports.size());
        if (metadata.variadic_inputs.enabled && !metadata.variadic_inputs.count_parameter.empty()) {
            const auto& parameter = metadata.variadic_inputs.count_parameter;
            const auto* sizing = json_utils::findObjectValue(params, parameter.c_str());
            if (sizing && sizing->is_array()) {
                inputCount = static_cast<int>(sizing->as_array().size());
            } else if (sizing && sizing->is_number()) {
                inputCount = json_utils::intValue(params, parameter.c_str(), inputCount);
            } else if (!sizing) {
                const auto spec = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
                                               [&](const s7::ParameterSpec& item) { return item.name == parameter; });
                if (spec != metadata.parameters.end()) {
                    if (spec->default_value.is_array()) {
                        inputCount = static_cast<int>(spec->default_value.as_array().size());
                    } else if (spec->default_value.is_number()) {
                        inputCount = static_cast<int>(json_utils::numberValue(spec->default_value));
                    }
                }
            }
        }
        return {metadataNames(metadata.input_ports, "in", inputCount), metadataNames(metadata.output_ports, "out", 0)};
    }

    // Built-ins described here for diagrams opened before (or without) the
    // registry: the ports their schemas use.
    if (type == "Constant" || type == "AnalogOscillator") {
        return {{}, numbered("out", 1)};
    }
    if (type == "MathFunction" || type == "Saturation" || type == "Sign" || type == "Gain" || type == "Delay" ||
        type == "Accumulator" || type == "FirFilter" || type == "IirFilter" || type == "PreviousValue" || type == "Convert") {
        return {numbered("in", 1), numbered("out", 1)};
    }
    if (type == "Divider" || type == "Power") {
        return {numbered("in", 2), numbered("out", 1)};
    }
    if (type == "MinMax") {
        return {numbered("in", std::max(2, json_utils::intValue(params, "input_count", 2))), numbered("out", 1)};
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
