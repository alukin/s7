#pragma once

#include <qschematic/view.hpp>

#include <QSet>

class QGraphicsItem;

namespace s7ui::ui {

/**
 * @brief Our own QSchematic::View subclass.
 *
 * Adds features we need without touching the external QSchematic library:
 *  - Additive rubber-band drag: holding Shift or Ctrl while rubber-band
 *    dragging adds to the current selection instead of replacing it.
 *  - additiveRubberBandDragChanged(bool) signal, used by MainWindow to
 *    update the status-bar interaction hint.
 *  - fitInView() override: corrects zoom-cap computation to use actual
 *    zoom values (via zoomValue()) instead of internal interpolation factor.
 *
 * All other behavior is inherited unchanged from QSchematic::View.
 */
class S7View : public QSchematic::View
{
    Q_OBJECT

public:
    explicit S7View(QWidget* parent = nullptr);
    ~S7View() override = default;

Q_SIGNALS:
    /// Emitted when an additive rubber-band drag starts (true) or ends (false).
    void additiveRubberBandDragChanged(bool active);

public Q_SLOTS:
    /// Override fitInView to fix zoom-cap calculation.
    /// This corrects an issue in QSchematic::View where zoom limits are
    /// applied incorrectly during fitInView operations.
    void fitInView();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool                    _additiveRubberBandDrag         = false;
    QSet<QGraphicsItem*>    _selectionBeforeRubberBand;
};

} // namespace s7ui::ui

