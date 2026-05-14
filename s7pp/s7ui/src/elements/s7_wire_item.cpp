#include "s7_wire_item.hpp"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace s7ui::elements {

const QColor S7WireItem::kColorBroken = QColor(220, 50, 50); // vivid red

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
    if (!broken_) {
        // Delegate to base class for normal rendering.
        Wire::paint(painter, option, widget);
        return;
    }

    // Custom rendering for the broken (orphaned) state.
    painter->save();

    QPen pen;
    pen.setStyle(Qt::DashLine);
    pen.setCapStyle(Qt::RoundCap);
    pen.setColor(kColorBroken);
    pen.setWidth(1);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const auto& pts = pointsRelative();
    painter->drawPolyline(pts.constData(), pts.count());

    // Draw a small "broken" marker (X) at each endpoint.
    const qreal markerSize = 3.0;
    pen.setStyle(Qt::SolidLine);
    pen.setWidth(1);
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

