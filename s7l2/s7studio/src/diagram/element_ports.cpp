// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

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

bool isAxisObserverElementType(const std::string_view type)
{
    return type == "PhasePortrait" || type == "PhaseTrajectory3D" || type == "Surface3D" || type == "ComplexSurface3D" || type == "FrequencyResponse" || type == "TimeFrequency";
}

bool isObserverElementType(const std::string_view type)
{
    return type == "Scope" || type == "LiveOscilloscope" || isAxisObserverElementType(type);
}

auto elementPorts(const std::string_view type, const boost::json::object& params, const boost::json::value& nestedSchema)
    -> ElementPorts
{
    if (type == "Scope" || type == "LiveOscilloscope") {
        const int count = std::max(0, json_utils::intValue(params, "channel_count", 0));
        auto inputs = numbered("signal", count);
        inputs.push_back(QStringLiteral("+")); // connection target that grows the observer
        return {std::move(inputs), {}};
    }
    if (type == "TimeFrequency") return {{QStringLiteral("in")}, {}};
    if (type == "FrequencyResponse") return {{QStringLiteral("input"), QStringLiteral("output")}, {}};
    if (type == "PhasePortrait") {
        return {{QStringLiteral("x"), QStringLiteral("y")}, {}};
    }
    if (type == "PhaseTrajectory3D") {
        return {{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}, {}};
    }
    if (type == "Surface3D" || type == "ComplexSurface3D") {
        std::vector<QString> names{"x", "y", "index", "columns", "rows", "active"};
        names.push_back(type == "Surface3D" ? "z" : "re");
        if (type == "ComplexSurface3D") names.push_back("im");
        return {std::move(names), {}};
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
        auto names = [&](const s7::VariadicInputSpec& ports, const std::vector<s7::PortSpec>& fixed, const char* prefix) {
            if (!ports.enabled || ports.count_parameter.empty()) return metadataNames(fixed, prefix, 0);
            const auto* sizing = json_utils::findObjectValue(params, ports.count_parameter.c_str());
            if (!sizing) {
                const auto spec = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
                    [&](const s7::ParameterSpec& item) { return item.name == ports.count_parameter; });
                if (spec != metadata.parameters.end()) sizing = &spec->default_value;
            }
            int count = static_cast<int>(fixed.size());
            if (sizing && sizing->is_array()) {
                count = static_cast<int>(std::min<std::size_t>(sizing->as_array().size(), ports.max_count));
                if (ports.port_name_template == "{channel}") {
                    std::vector<QString> result;
                    for (int index = 0; index < count; ++index) {
                        const auto& name = sizing->as_array()[index];
                        result.push_back(name.is_string() ? QString::fromStdString(std::string(name.as_string()))
                            : QStringLiteral("%1%2").arg(prefix).arg(index));
                    }
                    return result;
                }
            } else if (sizing && sizing->is_number()) {
                count = static_cast<int>(std::clamp(json_utils::numberValue(*sizing), 0.0, double(ports.max_count)));
            }
            return metadataNames(fixed, prefix, count);
        };
        return {names(metadata.variadic_inputs, metadata.input_ports, "in"),
                names(metadata.variadic_outputs, metadata.output_ports, "out")};
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
