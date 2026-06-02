#include "node_factory.hpp"

#include "elements/basic_node_item.hpp"
#include "elements/sink_node_item.hpp"
#include "scene_roles.hpp"
#include "visual_catalog.hpp"

#include <s7factory.hpp>

#include <QColor>
#include <qschematic/items/node.hpp>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>

namespace s7ui::ui {
namespace {
constexpr auto kComplexElementColorHex = "#8f5ab3";

auto isComplexType(const QString& typeName) -> bool
{
    return typeName.contains("complex", Qt::CaseInsensitive);
}

auto defaultScenePosForIndex(const int index) -> QPointF
{
    return {static_cast<qreal>(20.0 + (index % 6) * 70.0),
            static_cast<qreal>(20.0 + (index / 6) * 50.0)};
}

/// Convert visual spec shape string to BasicNodeItem::Shape enum
auto shapeFromString(const QString& shapeStr) -> s7ui::elements::BasicNodeItem::Shape
{
    const QString normalized = shapeStr.toLower().trimmed();
    if (normalized == "rect" || normalized == "rectangle") {
        return s7ui::elements::BasicNodeItem::Shape::Rectangle;
    } else if (normalized == "round_rect" || normalized == "roundedrect") {
        return s7ui::elements::BasicNodeItem::Shape::RoundedRect;
    } else if (normalized == "diamond") {
        return s7ui::elements::BasicNodeItem::Shape::Diamond;
    } else if (normalized == "hex" || normalized == "hexagon") {
        return s7ui::elements::BasicNodeItem::Shape::Hexagon;
    } else if (normalized == "circle" || normalized == "ellipse") {
        return s7ui::elements::BasicNodeItem::Shape::Circle;
    } else if (normalized == "custom") {
        return s7ui::elements::BasicNodeItem::Shape::Custom;
    }
    // Default to rounded rectangle
    return s7ui::elements::BasicNodeItem::Shape::RoundedRect;
}

void applyVisualStyleToBasicNode(s7ui::elements::BasicNodeItem& node,
                                 const NodeTemplate& nodeTemplate,
                                 const s7ui::visual::Catalog* visualCatalog)
{
    if (!visualCatalog) {
        if (isComplexType(nodeTemplate.typeName)) {
            node.setFillColor(QColor(kComplexElementColorHex));
        }
        return;
    }

    if (const auto visualSpec = visualCatalog->specForType(nodeTemplate.typeName)) {
        node.setVisualStyle(QColor(visualSpec->color), shapeFromString(visualSpec->shape));
        if (isComplexType(nodeTemplate.typeName)) {
            node.setFillColor(QColor(kComplexElementColorHex));
        }
        return;
    }

    if (isComplexType(nodeTemplate.typeName)) {
        node.setFillColor(QColor(kComplexElementColorHex));
    }
}

void applyVisualLayoutToSink(s7ui::elements::SinkNodeItem& sink,
                             const std::optional<s7ui::visual::VisualSpec>& visualSpec)
{
    if (!visualSpec) {
        return;
    }

    if (visualSpec->size) {
        sink.setVisualSize(*visualSpec->size);
    }
    if (visualSpec->previewArea) {
        sink.setPreviewArea(*visualSpec->previewArea);
    }
}

auto combinedItemRect(const QSchematic::Items::Item& item) -> QRectF
{
    return item.boundingRect().united(item.childrenBoundingRect());
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
    const bool useDefaultPosition = std::isnan(position.x()) || std::isnan(position.y());
    const QPointF resolvedPosition = useDefaultPosition ? defaultScenePosForIndex(index) : position;

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
        applyVisualLayoutToSink(*sink, visualSpecForType(nodeTemplate.typeName));
        applyVisualStyleToBasicNode(*sink, nodeTemplate, visualCatalog_);
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

    applyVisualStyleToBasicNode(*node, nodeTemplate, visualCatalog_);
    return node;
}

std::shared_ptr<QSchematic::Items::Item> NodeFactory::createPrototypeItem(
    const NodeTemplate& nodeTemplate) const
{
    auto item = createItemFromTemplate(nodeTemplate, 0, QPointF(0.0, 0.0));
    if (!item) {
        return item;
    }

    if (const auto basicNode = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(item)) {
        basicNode->setNodeId(QString());
        basicNode->setNodeName(nodeTemplate.typeName);
        basicNode->setData(kRoleNodeId, QString());
        basicNode->setData(kRoleNodeName, nodeTemplate.typeName);
    }

    return item;
}

QPointF NodeFactory::preferredAnchorInItem(const QSchematic::Items::Item& item) const
{
    if (const auto* node = dynamic_cast<const QSchematic::Items::Node*>(&item)) {
        const QPointF origin = node->mapToParent(QPointF(0.0, 0.0));
        const QPointF center = node->mapToParent(node->sizeRect().center());
        return center - origin;
    }

    const QRectF rect = combinedItemRect(item);
    const QPointF origin = item.mapToParent(QPointF(0.0, 0.0));
    const QPointF center = item.mapToParent(rect.center());
    return center - origin;
}

QPoint NodeFactory::pixmapHotSpotForItem(const QSchematic::Items::Item& item) const
{
    const QRectF rect = combinedItemRect(item);
    QPointF localAnchor = rect.center();
    if (const auto* node = dynamic_cast<const QSchematic::Items::Node*>(&item)) {
        localAnchor = node->sizeRect().center();
    }
    return (localAnchor - rect.topLeft()).toPoint();
}

QRectF NodeFactory::preferredPreviewRectInItem(const QSchematic::Items::Item& item) const
{
    if (dynamic_cast<const s7ui::elements::SinkNodeItem*>(&item)) {
        if (const auto* node = dynamic_cast<const QSchematic::Items::Node*>(&item)) {
            return node->sizeRect();
        }
    }

    return combinedItemRect(item);
}

std::optional<s7ui::visual::VisualSpec> NodeFactory::visualSpecForType(const QString& typeName) const
{
    return visualCatalog_ ? visualCatalog_->specForType(typeName) : std::nullopt;
}

} // namespace s7ui::ui

