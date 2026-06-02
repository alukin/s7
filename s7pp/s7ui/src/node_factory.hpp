#pragma once

#include "node_template.hpp"
#include "visual_catalog.hpp"

#include <QPoint>

#include <optional>

#include <memory>

class QTreeWidget;
class QTreeWidgetItem;
class QPointF;
class QRectF;

namespace QSchematic::Items {
class Item;
}

namespace s7ui::ui {

class NodeFactory
{
public:
    explicit NodeFactory(const s7ui::visual::Catalog* visualCatalog);

    [[nodiscard]] NodeTemplate selectedTemplate(const QTreeWidget* paletteTree) const;
    [[nodiscard]] NodeTemplate templateForType(const QString& typeName) const;

    [[nodiscard]] std::shared_ptr<QSchematic::Items::Item> createItemFromTemplate(
        const NodeTemplate& nodeTemplate,
        int index,
        const QPointF& position) const;

    [[nodiscard]] std::shared_ptr<QSchematic::Items::Item> createPrototypeItem(
        const NodeTemplate& nodeTemplate) const;
    [[nodiscard]] QPointF preferredAnchorInItem(const QSchematic::Items::Item& item) const;
    [[nodiscard]] QPoint pixmapHotSpotForItem(const QSchematic::Items::Item& item) const;
    [[nodiscard]] QRectF preferredPreviewRectInItem(const QSchematic::Items::Item& item) const;

    [[nodiscard]] std::optional<s7ui::visual::VisualSpec> visualSpecForType(const QString& typeName) const;

private:
    const s7ui::visual::Catalog* visualCatalog_ = nullptr;
};

} // namespace s7ui::ui

