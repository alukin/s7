#include "s7_wire_item.hpp"
#include "ui_constants.hpp"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace s7ui::elements {

const QColor S7WireItem::kColorBroken = QColor(220, 50, 50); // vivid red

namespace {
const QColor kWireColor = QColor("#000000");
const QColor kWireColorHighlighted = QColor("#dc2479");
const QColor kWireColorSelected = QColor("#0f16af");
}

S7WireItem::S7WireItem(QGraphicsItem* parent)
    : QSchematic::Items::Wire(QSchematic::Items::Item::WireType, parent)
{
}

void S7WireItem::setBroken(bool broken)
{
    if (broken_ == broken) return;
    broken_ = broken;
    update();
}

bool S7WireItem::isBroken() const
{
    return broken_;
}

void S7WireItem::paint(QPainter* painter,
                       const QStyleOptionGraphicsItem* option,
                       QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!broken_) {
        painter->save();

        QPen penLine;
        penLine.setStyle(Qt::SolidLine);
        penLine.setCapStyle(Qt::RoundCap);
        const QColor penColor = isSelected()
                                    ? kWireColorSelected
                                    : (isHighlighted() ? kWireColorHighlighted : kWireColor);
        penLine.setColor(penColor);
        penLine.setWidthF(style::kWirePenWidth);

        painter->setPen(penLine);
        painter->setBrush(Qt::NoBrush);

        const auto& pts = pointsRelative();
        painter->drawPolyline(pts.constData(), pts.count());

        // Draw wire junction points with reduced radius.
        painter->setPen(Qt::NoPen);
        painter->setBrush(isHighlighted() ? kWireColorHighlighted : kWireColor);
        for (const auto& wirePoint : wirePointsRelative()) {
            if (wirePoint.is_junction()) {
                painter->drawEllipse(wirePoint.toPointF(),
                                     style::kWireJunctionRadius,
                                     style::kWireJunctionRadius);
            }
        }

        // Keep selected handles visible but smaller.
        if (isSelected()) {
            painter->setOpacity(0.5);
            painter->setPen(QPen(Qt::black));
            painter->setBrush(QBrush(Qt::black));
            for (const QPointF& pt : pts) {
                painter->drawRect(QRectF(pt.x() - style::kWireHandleSize,
                                         pt.y() - style::kWireHandleSize,
                                         2.0 * style::kWireHandleSize,
                                         2.0 * style::kWireHandleSize));
            }
        }

        painter->restore();
        return;
    }

    // Custom rendering for the broken (orphaned) state.
    painter->save();

    QPen pen;
    pen.setStyle(Qt::DashLine);
    pen.setCapStyle(Qt::RoundCap);
    pen.setColor(kColorBroken);
    pen.setWidthF(style::kWirePenWidth);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const auto& pts = pointsRelative();
    painter->drawPolyline(pts.constData(), pts.count());

    // Draw a small "broken" marker (X) at each endpoint.
    const qreal markerSize = style::kBrokenWireMarkerSize;
    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(style::kWirePenWidth);
    painter->setPen(pen);
    for (const QPointF& pt : {pts.constFirst(), pts.constLast()}) {
        painter->drawLine(pt + QPointF(-markerSize, -markerSize),
                          pt + QPointF( markerSize,  markerSize));
        painter->drawLine(pt + QPointF( markerSize, -markerSize),
                          pt + QPointF(-markerSize,  markerSize));
    }

    painter->restore();
}

} // namespace s7ui::elements

