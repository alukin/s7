#include "palette_tree_widget.hpp"

#include "node_factory.hpp"
#include "scene_roles.hpp"

#include <qschematic/items/itemmimedata.hpp>

#include <QDrag>

#include <cmath>
#include <memory>

namespace s7ui::ui {

PaletteTreeWidget::PaletteTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
}

void PaletteTreeWidget::setNodeFactory(const NodeFactory* nodeFactory)
{
    nodeFactory_ = nodeFactory;
}

void PaletteTreeWidget::setViewScaleProvider(std::function<qreal()> provider)
{
    viewScaleProvider_ = std::move(provider);
}

void PaletteTreeWidget::setSceneSettings(const QSchematic::Settings& settings)
{
    sceneSettings_ = settings;
}

void PaletteTreeWidget::startDrag(Qt::DropActions supportedActions)
{
    auto* item = currentItem();
    if (!item || item->childCount() > 0 || !nodeFactory_) {
        return;
    }

    const QString typeName = item->data(0, kRoleTypeName).toString();
    if (typeName.isEmpty()) {
        return;
    }

    const auto prototype = nodeFactory_->createPrototypeItem(nodeFactory_->templateForType(typeName));
    if (!prototype) {
        return;
    }

    // Apply scene settings to the prototype so connectors compute their
    // symbol rect using the correct gridSize (default is 20; we use 5).
    // BasicNodeItem::settingsChanged propagates this to child connectors.
    prototype->setSettings(sceneSettings_);

    // -----------------------------------------------------------------------
    // Compute drag-preview scale so the pixmap matches the apparent node size
    // on the canvas at the current zoom level.
    //
    //   toPixmap(hotspot, scale) renders in item-LOCAL coordinates × scale.
    //   The item's own setScale() (kNodeVisualScale = 0.5) is NOT applied by
    //   toPixmap — it is only active when the item sits in a QGraphicsScene.
    //
    //   Apparent node size in view pixels =
    //       item_local_size × item_own_scale × view_pixels_per_scene_unit
    //
    //   So: pixmapScale = item->scale() × viewTransform.m11()
    //
    // Fallback: if no view scale is available, use item->scale() alone which
    // produces a small but correctly-shaped thumbnail.
    // -----------------------------------------------------------------------
    const qreal itemOwnScale  = std::max<qreal>(0.01, std::abs(prototype->scale()));
    const qreal viewScale     = viewScaleProvider_ ? viewScaleProvider_() : 1.0;
    const qreal kPreviewScale = itemOwnScale * viewScale;

    const QRectF previewRect = nodeFactory_->preferredPreviewRectInItem(*prototype);
    if (previewRect.isNull() || !previewRect.isValid()) {
        return;
    }

    QPointF unusedOriginHotSpot;
    const QPixmap fullPreview = prototype->toPixmap(unusedOriginHotSpot, kPreviewScale);
    if (fullPreview.isNull()) {
        return;
    }

    QRectF fullRect = prototype->boundingRect().united(prototype->childrenBoundingRect());
    fullRect.setWidth(fullRect.width() - fullRect.x());
    fullRect.setHeight(fullRect.height() - fullRect.y());

    const QRect cropRectPx(
        static_cast<int>(std::lround((previewRect.left() - fullRect.left()) * kPreviewScale)),
        static_cast<int>(std::lround((previewRect.top() - fullRect.top()) * kPreviewScale)),
        std::max(1, static_cast<int>(std::lround(previewRect.width() * kPreviewScale))),
        std::max(1, static_cast<int>(std::lround(previewRect.height() * kPreviewScale))));

    const QRect boundedCrop = cropRectPx.intersected(fullPreview.rect());
    const QPixmap preview = boundedCrop.isValid() ? fullPreview.copy(boundedCrop) : fullPreview;

    if (preview.isNull()) {
        return;
    }

    auto drag = std::make_unique<QDrag>(this);
    drag->setMimeData(new QSchematic::Items::MimeData(prototype));
    drag->setPixmap(preview);

    const QPoint baseHotspot = nodeFactory_->pixmapHotSpotForItem(*prototype);
    const QPointF localHotspot = QPointF(baseHotspot) - previewRect.topLeft();
    drag->setHotSpot(
        QPoint(static_cast<int>(std::lround(localHotspot.x() * kPreviewScale)),
               static_cast<int>(std::lround(localHotspot.y() * kPreviewScale))));

    drag->exec(supportedActions, Qt::CopyAction);
}

} // namespace s7ui::ui
