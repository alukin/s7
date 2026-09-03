#include "diagram_ruler.hpp"

#include <QGraphicsView>
#include <QPainter>
#include <QPaintEvent>
#include <QPoint>
#include <QRect>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>

namespace s7studio::diagram {

namespace {

constexpr int kRulerThicknessPx = 22;
constexpr double kSceneUnitsPerMm = 96.0 / 25.4;

[[nodiscard]] auto positiveScaleFrom(const QTransform& transform, const DiagramRuler::Axis axis) -> double
{
    if (axis == DiagramRuler::Axis::Horizontal) {
        return std::max(std::abs(transform.m11()), 1e-6);
    }
    return std::max(std::abs(transform.m22()), 1e-6);
}

[[nodiscard]] auto pixelsPerMillimeter(QGraphicsView* view, const DiagramRuler::Axis axis) -> double
{
    if (!view) {
        return 0.0;
    }
    return positiveScaleFrom(view->transform(), axis) * kSceneUnitsPerMm;
}

[[nodiscard]] auto isMajorTick(const int index, const int majorEvery) -> bool
{
    return majorEvery > 0 && index % majorEvery == 0;
}

} // namespace

DiagramRuler::DiagramRuler(QGraphicsView* view, const Axis axis, QWidget* parent)
    : QWidget(parent)
    , view_(view)
    , axis_(axis)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);
    if (axis_ == Axis::Horizontal) {
        setFixedHeight(kRulerThicknessPx);
    } else {
        setFixedWidth(kRulerThicknessPx);
    }
}

auto DiagramRuler::sizeHint() const -> QSize
{
    if (axis_ == Axis::Horizontal) {
        return {160, kRulerThicknessPx};
    }
    return {kRulerThicknessPx, 160};
}

auto DiagramRuler::minimumSizeHint() const -> QSize
{
    if (axis_ == Axis::Horizontal) {
        return {32, kRulerThicknessPx};
    }
    return {kRulerThicknessPx, 32};
}

void DiagramRuler::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#f5f5f5"));
    painter.setPen(QPen(QColor("#b8b8b8"), 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (!view_ || !view_->scene()) {
        return;
    }

    painter.setPen(QPen(QColor("#505050"), 1.0));
    if (axis_ == Axis::Horizontal) {
        drawHorizontal(painter);
    } else {
        drawVertical(painter);
    }

    event->accept();
}

auto DiagramRuler::majorTickStepMm() const -> int
{
    const double pxPerMm = pixelsPerMillimeter(view_, axis_);
    constexpr std::array<int, 9> candidates{1, 2, 5, 10, 20, 50, 100, 200, 500};
    for (const int candidate : candidates) {
        if (candidate * pxPerMm >= 40.0) {
            return candidate;
        }
    }
    return candidates.back();
}

auto DiagramRuler::minorTickStepMm(const int majorTickStep) const -> int
{
    if (majorTickStep <= 1) {
        return 1;
    }

    const double pxPerMm = pixelsPerMillimeter(view_, axis_);
    for (const int divisor : {10, 5, 2}) {
        if (majorTickStep % divisor != 0) {
            continue;
        }

        const int step = majorTickStep / divisor;
        if (step * pxPerMm >= 6.0) {
            return step;
        }
    }

    return majorTickStep;
}

auto DiagramRuler::mapSceneMillimetersToLocal(const double millimeters) const -> int
{
    if (!view_) {
        return 0;
    }

    const QPoint widgetPoint = axis_ == Axis::Horizontal
        ? view_->mapFromScene(QPointF(millimeters * kSceneUnitsPerMm, 0.0))
        : view_->mapFromScene(QPointF(0.0, millimeters * kSceneUnitsPerMm));
    const QPoint offset = geometry().topLeft();
    return axis_ == Axis::Horizontal ? widgetPoint.x() - offset.x() : widgetPoint.y() - offset.y();
}

void DiagramRuler::drawHorizontal(QPainter& painter)
{
    if (!view_) {
        return;
    }

    const int majorStep = majorTickStepMm();
    const int minorStep = minorTickStepMm(majorStep);
    const int majorEvery = std::max(1, majorStep / minorStep);

    const QRect viewportRect = view_->viewport()->geometry();
    const double leftMm = view_->mapToScene(QPoint(viewportRect.left(), viewportRect.top())).x() / kSceneUnitsPerMm;
    const double rightMm = view_->mapToScene(QPoint(viewportRect.right(), viewportRect.top())).x() / kSceneUnitsPerMm;

    const int firstIndex = static_cast<int>(std::floor(std::min(leftMm, rightMm) / minorStep));
    const int lastIndex = static_cast<int>(std::ceil(std::max(leftMm, rightMm) / minorStep));

    for (int index = firstIndex; index <= lastIndex; ++index) {
        const double tickMm = static_cast<double>(index * minorStep);
        const int x = mapSceneMillimetersToLocal(tickMm);
        if (x < 0 || x > width()) {
            continue;
        }

        const bool major = isMajorTick(index, majorEvery);
        const int tickTop = major ? 4 : 11;
        painter.drawLine(x, tickTop, x, height() - 1);

        if (major) {
            painter.drawText(x + 2, 11, QString::number(static_cast<int>(std::llround(tickMm))));
        }
    }
}

void DiagramRuler::drawVertical(QPainter& painter)
{
    if (!view_) {
        return;
    }

    const int majorStep = majorTickStepMm();
    const int minorStep = minorTickStepMm(majorStep);
    const int majorEvery = std::max(1, majorStep / minorStep);

    const QRect viewportRect = view_->viewport()->geometry();
    const double topMm = view_->mapToScene(QPoint(viewportRect.left(), viewportRect.top())).y() / kSceneUnitsPerMm;
    const double bottomMm = view_->mapToScene(QPoint(viewportRect.left(), viewportRect.bottom())).y() / kSceneUnitsPerMm;

    const int firstIndex = static_cast<int>(std::floor(std::min(topMm, bottomMm) / minorStep));
    const int lastIndex = static_cast<int>(std::ceil(std::max(topMm, bottomMm) / minorStep));

    for (int index = firstIndex; index <= lastIndex; ++index) {
        const double tickMm = static_cast<double>(index * minorStep);
        const int y = mapSceneMillimetersToLocal(tickMm);
        if (y < 0 || y > height()) {
            continue;
        }

        const bool major = isMajorTick(index, majorEvery);
        const int tickLeft = major ? 4 : 11;
        painter.drawLine(tickLeft, y, width() - 1, y);

        if (major) {
            painter.save();
            painter.translate(9, y - 2);
            painter.rotate(-90.0);
            painter.drawText(QPoint(0, 0), QString::number(static_cast<int>(std::llround(tickMm))));
            painter.restore();
        }
    }
}

} // namespace s7studio::diagram

