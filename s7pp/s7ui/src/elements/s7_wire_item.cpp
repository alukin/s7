#include "s7_wire_item.hpp"
#include "ui_constants.hpp"

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

auto S7WireItem::isBroken() const -> bool
{
    return broken_;
}

void S7WireItem::paint(QPainter* painter,
                       const QStyleOptionGraphicsItem* option,
                       QWidget* widget)
{
    if (!broken_) {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        const QColor wireColor(0, 0, 0);
        const QColor wireColorHighlighted(220, 36, 121);
        const QColor wireColorSelected(15, 22, 175);

        painter->save();

        QPen penLine;
        penLine.setStyle(Qt::SolidLine);
        penLine.setCapStyle(Qt::RoundCap);
        const QColor penColor = isSelected()
                                    ? wireColorSelected
                                    : (isHighlighted() ? wireColorHighlighted : wireColor);
        penLine.setColor(penColor);
        penLine.setWidthF(style::kWirePenWidth);

        painter->setPen(penLine);
        painter->setBrush(Qt::NoBrush);

        const auto& pts = pointsRelative();
        if (!pts.isEmpty()) {
            painter->drawPolyline(pts.constData(), static_cast<int>(pts.count()));
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(isHighlighted() ? wireColorHighlighted : wireColor);
        for (const auto& wirePoint : wirePointsRelative()) {
            if (wirePoint.is_junction()) {
                painter->drawEllipse(wirePoint.toPointF(),
                                     style::kWireJunctionRadius,
                                     style::kWireJunctionRadius);
            }
        }

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
    if (pts.isEmpty()) {
        painter->restore();
        return;
    }
    painter->drawPolyline(pts.constData(), static_cast<int>(pts.count()));

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

