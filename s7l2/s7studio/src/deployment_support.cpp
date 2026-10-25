// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "deployment_support.hpp"

#include "diagram/diagram_scene.hpp"
#include "local_runtime_v2.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace s7studio::diagram {

auto deploymentDiagram(const DiagramDocument& document, const QMap<QString, NodeAddress>& known) -> boost::json::object
{
    auto diagram = document.toJson().as_object();
    if (auto* layout = diagram.if_contains("diagram"); layout && layout->is_object()) { layout->as_object().erase("observers_panel"); layout->as_object().erase("observers_panel_manual"); }
    std::unordered_map<std::string, boost::json::object> previous;
    for (const auto& value : document.runtimeNodes) {
        if (value.is_object()) {
            if (const auto* uuid = value.as_object().if_contains("node_uuid"); uuid && uuid->is_string()) {
                previous[std::string(uuid->as_string())] = value.as_object();
            }
        }
    }
    boost::json::array nodes;
    for (const auto& uuid : s7::deployment::placed_nodes(diagram)) {
        auto entry = previous.contains(uuid) ? previous.at(uuid) : boost::json::object{{"node_uuid", uuid}};
        if (const auto it = known.find(QString::fromStdString(uuid)); it != known.end()) {
            entry["name"] = it->name.toStdString();
            entry["endpoint"] = it->endpoint.toStdString();
            if (!it->dataEndpoint.isEmpty()) {
                entry["data_endpoint"] = it->dataEndpoint.toStdString();
                entry["data_security"] = it->dataSecurity.toStdString();
            }
        }
        nodes.push_back(std::move(entry));
    }
    diagram["runtime_nodes"] = std::move(nodes);
    diagram["streams"] = document.streams;
    return diagram;
}

auto studioOutputTypes(const boost::json::object& diagram) -> s7::deployment::OutputTypeLookup
{
    const auto schema = runtime::buildSchema(s7::deployment::diagram_schema(diagram));
    auto resolved = s7::TypeResolver::resolve(schema);
    if (!resolved.ok()) {
        throw std::invalid_argument(resolved.errors.front());
    }
    auto types = std::make_shared<std::unordered_map<std::string, std::vector<s7::SignalType>>>();
    for (std::size_t index = 0; index < schema.elements().size(); ++index) {
        auto& ports = (*types)[schema.elements()[index].id];
        for (const auto& port : resolved.factories[index]()->output_ports()) {
            ports.push_back(port.type);
        }
    }
    return [types](const std::string& element, const std::size_t port) -> std::optional<s7::SignalType> {
        const auto it = types->find(element);
        if (it == types->end() || port >= it->second.size()) {
            return std::nullopt;
        }
        return it->second[port];
    };
}

void updatePlacementLabels(DiagramScene& scene, const QMap<QString, QString>& nodeNames)
{
    for (const auto& node : scene.elementNodes()) {
        const auto placement = QString::fromStdString(node->element().placement);
        if (placement.isEmpty()) {
            node->setPlacementLabel({});
            continue;
        }
        const auto name = nodeNames.value(placement);
        node->setPlacementLabel(QStringLiteral("@ ") + (name.isEmpty() ? placement.left(8) : name));
    }
}

} // namespace s7studio::diagram
