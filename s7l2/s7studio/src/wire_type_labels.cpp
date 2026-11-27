// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "wire_type_labels.hpp"

#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "local_runtime_v2.hpp"

#include <s7/validation.hpp>

#include <QStringList>

#include <map>
#include <unordered_map>

namespace s7studio::diagram {
namespace {
QString label(s7::SignalType type, const s7::ArrayDescriptor* shape)
{
    auto result = QString::fromUtf8(s7::signal_type_name(type));
    if (!shape) return result;
    QStringList extents;
    for (const auto extent : shape->extents()) extents.push_back(QString::number(extent));
    return result + QStringLiteral("[") + extents.join(QLatin1Char(',')) + QStringLiteral("]");
}
} // namespace

auto updateWireTypeLabels(DiagramScene& scene, QString* error) -> std::vector<WireTypeLabel>
{
    const NetModel model(scene);

    // element id -> output port types, from the resolved elements
    std::unordered_map<std::string, std::vector<QString>> outputTypes;
    std::unordered_map<std::string, std::vector<QString>> inputTypes;
    QString firstError;
    try {
        const auto schema = runtime::buildSchema(model.schemaJson("wire_types"));
        auto flattened = s7::detail::flatten_schema(schema, true);
        if (!flattened.errors.empty()) throw std::invalid_argument(flattened.errors.front());
        const auto& flat = *flattened.schema;
        auto resolved = s7::TypeResolver::resolve(flat);
        if (!resolved.ok()) {
            firstError = QString::fromStdString(resolved.errors.front());
        } else {
            for (std::size_t index = 0; index < flat.elements().size(); ++index) {
                const auto& spec = flat.elements()[index];
                auto element = resolved.instances[index] ? std::move(resolved.instances[index])
                                                         : resolved.factories[index]();
                const auto portLabels = [&](std::span<const s7::Port> ports, bool output) {
                    std::vector<QString> labels;
                    labels.reserve(ports.size());
                    for (const auto& port : ports) {
                        const auto* shape = port.array_descriptor;
                        if (spec.array_metadata) {
                            const auto& metadata = *spec.array_metadata;
                            const auto& declared = (output ? metadata.output_ports : metadata.input_ports)[port.index].array;
                            if (declared) shape = &*declared;
                        }
                        labels.push_back(label(port.type, shape));
                    }
                    return labels;
                };
                outputTypes[spec.id] = portLabels(element->output_ports(), true);
                if (const auto* boundary = dynamic_cast<const s7::detail::SchemaPort*>(element.get())) {
                    if (boundary->input()) {
                        auto& labels = inputTypes[boundary->owner()];
                        if (labels.size() <= boundary->index()) labels.resize(boundary->index() + 1);
                        labels[boundary->index()] = outputTypes.at(spec.id).front();
                    }
                } else {
                    inputTypes[spec.id] = portLabels(element->input_ports(), false);
                }
            }
            for (const auto& [id, ports] : flattened.nested_outputs) {
                std::vector<QString> labels;
                labels.reserve(ports.size());
                for (const auto& port : ports) labels.push_back(outputTypes.at(port).front());
                outputTypes.emplace(id, std::move(labels));
            }
        }
    } catch (const std::exception& ex) {
        firstError = QString::fromUtf8(ex.what());
    }
    if (error) {
        *error = firstError;
    }

    auto typeOf = [&](const PortRef& port) {
        if (const auto it = outputTypes.find(port.element);
            it != outputTypes.end() && port.port >= 0 && static_cast<std::size_t>(port.port) < it->second.size()) {
            return it->second[static_cast<std::size_t>(port.port)];
        }
        return QStringLiteral("?");
    };

    for (const auto& node : scene.elementNodes()) {
        const auto setTooltips = [&](const auto& ports, const auto& types, const QString& direction) {
            const auto found = types.find(node->id());
            for (std::size_t index = 0; index < ports.size(); ++index) {
                const auto& port = ports[index];
                auto tooltip = QStringLiteral("%1 %2").arg(direction, port->name());
                if (found != types.end() && index < found->second.size() &&
                    found->second[index].contains(QLatin1Char('['))) {
                    tooltip += QStringLiteral(" (%1)").arg(found->second[index]);
                }
                port->setToolTip(tooltip);
            }
        };
        setTooltips(node->inputs(), inputTypes, QStringLiteral("input"));
        setTooltips(node->outputs(), outputTypes, QStringLiteral("output"));
    }

    std::vector<WireTypeLabel> labels;
    for (const auto& connection : model.connections()) {
        labels.push_back(WireTypeLabel{QString::fromStdString(connection.from.element), connection.from.port, typeOf(connection.from)});
    }

    std::map<const SignalWire*, QString> problems;
    for (const auto& [wire, problem] : model.problems()) {
        problems.emplace(wire, problem);
    }
    for (const auto& wire : scene.signalWires()) {
        const auto problem = problems.find(wire.get());
        wire->setProblem(problem == problems.end() ? QString() : problem->second);
        // One label per net, on its first wire.
        const auto driver = wire->upstream.isOutput() ? NetModel::driverOf(*wire) : std::nullopt;
        const auto type = driver ? typeOf(*driver) : QString();
        wire->setTypeLabel(type, type != QStringLiteral("?"));

        QString probeLabel;
        if (driver) {
            for (const auto& probe : scene.probes()) {
                if (probe.point == *driver) {
                    probeLabel = probe.name;
                    break;
                }
            }
        }
        wire->setProbeLabel(probeLabel);
    }
    return labels;
}

} // namespace s7studio::diagram
