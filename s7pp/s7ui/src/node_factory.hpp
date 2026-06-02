#pragma once

#include "node_template.hpp"

#include <qschematic/items/item.hpp>

#include <QPoint>
#include <QPointF>
#include <QRectF>

#include <memory>

class QTreeWidget;
class QTreeWidgetItem;

namespace s7ui::visual {
class Catalog;
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

private:
    const s7ui::visual::Catalog* visualCatalog_ = nullptr;
};

} // namespace s7ui::ui

