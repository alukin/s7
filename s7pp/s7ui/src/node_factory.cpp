#include "node_factory.hpp"

#include "elements/basic_node_item.hpp"
#include "elements/sink_node_item.hpp"
#include "scene_roles.hpp"
#include "visual_catalog.hpp"

#include <s7factory.hpp>

#include <QColor>
#include <QTreeWidget>

#include <algorithm>

namespace s7ui::ui {
namespace {
auto defaultScenePosForIndex(const int index) -> QPointF
{
    return {static_cast<qreal>(20.0 + (index % 6) * 70.0),
            static_cast<qreal>(20.0 + (index / 6) * 50.0)};
}
} // namespace

NodeFactory::NodeFactory(const s7ui::visual::Catalog* visualCatalog)
    : visualCatalog_(visualCatalog)
{
}

NodeTemplate NodeFactory::selectedTemplate(const QTreeWidget* paletteTree) const
{
    NodeTemplate fallback{"Core", "Constant", "const", 0, 1, false};
    if (!paletteTree) return fallback;

    auto* item = paletteTree->currentItem();
    if (!item || item->childCount() > 0) return fallback;

    NodeTemplate nodeTemplate;
    nodeTemplate.typeName = item->data(0, kRoleTypeName).toString();
    nodeTemplate.defaultIdPrefix = item->data(0, kRoleCategory).toString();
    nodeTemplate.inputPorts = item->data(0, kRoleInputPorts).toInt();
    nodeTemplate.outputPorts = item->data(0, kRoleOutputPorts).toInt();
    nodeTemplate.visualSink = item->data(0, kRoleVisualSink).toBool();
    return nodeTemplate;
}

NodeTemplate NodeFactory::templateForType(const QString& typeName) const
{
    if (const auto desc = s7::ElementRegistry::instance().getDescriptor(typeName.toStdString())) {
        QString prefix = typeName.toLower();
        prefix.remove(' ');
        return NodeTemplate{QString::fromStdString(desc->category), typeName, prefix, desc->default_input_count, desc->default_output_count, false};
    }

    if (typeName == "ScopeSink") return NodeTemplate{"Visual", "ScopeSink", "scope", 2, 0, true};
    if (typeName == "ComplexScopeSink") return NodeTemplate{"Visual", "ComplexScopeSink", "complex_scope", 1, 0, true};

    QString fallbackPrefix = typeName.toLower();
    fallbackPrefix.remove(' ');
    if (fallbackPrefix.isEmpty()) fallbackPrefix = "node";

    QString fallbackCategory = "Imported";
    if (visualCatalog_) {
        if (const auto visualSpec = visualCatalog_->specForType(typeName); visualSpec && !visualSpec->category.trimmed().isEmpty()) {
            fallbackCategory = visualSpec->category;
        }
    }
    return NodeTemplate{fallbackCategory, typeName, fallbackPrefix, 1, 1, false};
}

std::shared_ptr<QSchematic::Items::Item> NodeFactory::createItemFromTemplate(
    const NodeTemplate& nodeTemplate,
    const int index,
    const QPointF& position) const
{
    const QPointF resolvedPosition = position.isNull() ? defaultScenePosForIndex(index) : position;

    if (nodeTemplate.visualSink) {
        auto sink = std::make_shared<s7ui::elements::SinkNodeItem>(nodeTemplate.typeName, std::max(1, nodeTemplate.inputPorts));
        const QString nodeId = QString("%1_%2").arg(nodeTemplate.defaultIdPrefix).arg(index + 1);
        const QString nodeName = QString("%1 %2").arg(nodeTemplate.typeName).arg(index + 1);
        sink->setNodeId(nodeId);
        sink->setNodeName(nodeName);
        sink->setParamsJson(QString("{\"inputs\": %1}").arg(std::max(1, nodeTemplate.inputPorts)));
        sink->setData(kRoleNodeId, nodeId);
        sink->setData(kRoleNodeName, nodeName);
        sink->setData(kRoleNodeType, nodeTemplate.typeName);
        sink->setData(kRoleVisualSink, true);
        sink->setScenePos(resolvedPosition);
        return sink;
    }

    auto node = std::make_shared<s7ui::elements::BasicNodeItem>(nodeTemplate.typeName);
    node->setScenePos(resolvedPosition);
    const QString nodeId = QString("%1_%2").arg(nodeTemplate.defaultIdPrefix).arg(index + 1);
    const QString nodeName = QString("%1 %2").arg(nodeTemplate.typeName).arg(index + 1);
    node->setNodeId(nodeId);
    node->setNodeName(nodeName);
    // paramsJson is already initialised to the descriptor defaults in BasicNodeItem constructor.
    node->setData(kRoleNodeId, nodeId);
    node->setData(kRoleNodeName, nodeName);
    node->setData(kRoleNodeType, nodeTemplate.typeName);
    node->setData(kRoleVisualSink, false);

    // Apply ports from descriptor (named labels + correct counts).
    const auto desc = s7::ElementRegistry::instance().getDescriptor(nodeTemplate.typeName.toStdString());
    const std::vector<s7::PortDescriptor> portDescs = desc ? desc->ports : std::vector<s7::PortDescriptor>{};
    node->setPortLayout(nodeTemplate.inputPorts, nodeTemplate.outputPorts, portDescs);

    if (visualCatalog_) {
        if (const auto visualSpec = visualCatalog_->specForType(nodeTemplate.typeName)) {
            node->setVisualStyle(QColor(visualSpec->color));
        }
    }
    return node;
}

} // namespace s7ui::ui

