#include "wire_type_labels.hpp"

#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "local_runtime_v2.hpp"

#include <map>
#include <unordered_map>

namespace s7studio::diagram {

auto updateWireTypeLabels(DiagramScene& scene, QString* error) -> std::vector<WireTypeLabel>
{
    const NetModel model(scene);

    // element id -> output port types, from the resolved elements
    std::unordered_map<std::string, std::vector<s7::SignalType>> outputTypes;
    QString firstError;
    try {
        const auto schema = runtime::buildSchema(model.schemaJson("wire_types"));
        auto resolved = s7::TypeResolver::resolve(schema);
        if (!resolved.ok()) {
            firstError = QString::fromStdString(resolved.errors.front());
        } else {
            for (std::size_t index = 0; index < schema.elements().size(); ++index) {
                const auto element = resolved.factories[index]();
                auto& types = outputTypes[schema.elements()[index].id];
                for (const auto& port : element->output_ports()) {
                    types.push_back(port.type);
                }
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
            return QString::fromUtf8(s7::signal_type_name(it->second[static_cast<std::size_t>(port.port)]));
        }
        return QStringLiteral("?");
    };

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
