#include "deployment_support.hpp"

#include "diagram_node_roles.hpp"
#include "local_runtime_v2.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/scene.hpp>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QGraphicsSimpleTextItem>

#include <memory>
#include <unordered_map>
#include <vector>

namespace s7studio::diagram {

namespace {

constexpr int kPlacementLabelRole = 0x5738; // marks the label item (cf. wire type labels)

auto findPlacementLabel(QGraphicsItem& node) -> QGraphicsSimpleTextItem*
{
    for (auto* child : node.childItems()) {
        if (child->data(kPlacementLabelRole).toBool()) {
            return dynamic_cast<QGraphicsSimpleTextItem*>(child);
        }
    }
    return nullptr;
}

} // namespace

auto deploymentDiagram(const DiagramDocument& document, const QMap<QString, NodeAddress>& known) -> boost::json::object
{
    auto diagram = document.toJson().as_object();
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

void updatePlacementLabels(QSchematic::Scene& scene, const QMap<QString, QString>& nodeNames)
{
    for (const auto& node : scene.items<QSchematic::Items::Node>()) {
        if (!node) {
            continue;
        }
        const auto placement = node->data(kNodePlacementRole).toString();
        auto* label = findPlacementLabel(*node);
        if (placement.isEmpty()) {
            delete label;
            continue;
        }
        if (!label) {
            label = new QGraphicsSimpleTextItem(node.get());
            label->setData(kPlacementLabelRole, true);
            QFont font = label->font();
            font.setPointSizeF(font.pointSizeF() * 0.85);
            label->setFont(font);
            label->setBrush(QBrush(QColor(0x20, 0x70, 0x40)));
        }
        const auto name = nodeNames.value(placement);
        label->setText(QStringLiteral("@ ") + (name.isEmpty() ? placement.left(8) : name));
        label->setPos(0.0, node->size().height() + 2.0);
    }
}

} // namespace s7studio::diagram
