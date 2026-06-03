#include "s7_view.hpp"

#include <QMouseEvent>
#include <cmath>

namespace s7ui::ui {

S7View::S7View(QWidget* parent)
    : QSchematic::View(parent)
{
}

void S7View::fitInView()
{
    // Sanity check
    auto* schematicScene = dynamic_cast<QSchematic::Scene*>(scene());
    if (!schematicScene)
        return;

    // Find the combined bounding rect of all the items
    QRectF rect;
    for (const auto& item : schematicScene->QGraphicsScene::items()) {
        // Ignore the background item
        if (schematicScene->isBackground(item))
            continue;

        QRectF boundingRect = item->boundingRect();
        boundingRect.moveTo(item->scenePos());
        rect = rect.united(boundingRect);
    }

    // Add some padding
    const auto adj = std::max(0.0, fitall_padding);
    rect.adjust(-adj, -adj, adj, adj);

    // CORRECTED: Use actual zoom value (via zoomValue()) instead of internal
    // interpolation _scaleFactor. This fixes a bug where fitInView could over-zoom
    // by applying frame ratios to the wrong scale space.
    const qreal currentZoomValue = zoomValue();
    qreal newZoomValue = currentZoomValue;
    QGraphicsView::fitInView(rect, Qt::KeepAspectRatio);
    newZoomValue = viewport()->geometry().width() / mapToScene(viewport()->geometry()).boundingRect().width();
    if (currentZoomValue < 1)
        newZoomValue = std::min(newZoomValue, 1.0);
    else
        newZoomValue = std::min(newZoomValue, currentZoomValue);
    setZoomValue(newZoomValue);
}

void S7View::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // If the user holds Shift or Ctrl and starts a rubber-band drag on an
        // empty area, remember the current selection so we can restore it after
        // the rubber-band completes (additive selection).
        const bool additiveRequested =
            (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
            && itemAt(event->pos()) == nullptr;

        if (additiveRequested && scene()) {
            _selectionBeforeRubberBand.clear();
            for (auto* selectedItem : scene()->selectedItems()) {
                if (selectedItem)
                    _selectionBeforeRubberBand.insert(selectedItem);
            }
            _additiveRubberBandDrag = true;
            Q_EMIT additiveRubberBandDragChanged(true);
        } else {
            _selectionBeforeRubberBand.clear();
            if (_additiveRubberBandDrag) {
                _additiveRubberBandDrag = false;
                Q_EMIT additiveRubberBandDragChanged(false);
            }
        }
    }

    QSchematic::View::mousePressEvent(event);
}

void S7View::mouseReleaseEvent(QMouseEvent* event)
{
    QSchematic::View::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton && _additiveRubberBandDrag) {
        // Merge the items that were selected before the drag back into the
        // current selection, giving true additive behaviour.
        for (auto* previouslySelected : _selectionBeforeRubberBand) {
            if (previouslySelected && previouslySelected->scene() == scene())
                previouslySelected->setSelected(true);
        }

        _selectionBeforeRubberBand.clear();
        _additiveRubberBandDrag = false;
        Q_EMIT additiveRubberBandDragChanged(false);
    }
}

} // namespace s7ui::ui

