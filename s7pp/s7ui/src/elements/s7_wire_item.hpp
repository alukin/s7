#pragma once

#include <qschematic/items/wire.hpp>

namespace s7ui::elements {

/// A wire item that can be marked as "broken" (orphaned endpoint after port removal).
/// When broken the wire is drawn in red so the user knows it must be re-connected.
class S7WireItem : public QSchematic::Items::Wire
{
    Q_OBJECT

public:
    explicit S7WireItem(QGraphicsItem* parent = nullptr);

    void setBroken(bool broken);
    [[nodiscard]] bool isBroken() const;

protected:
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bool broken_ = false;

    static const QColor kColorBroken;
};

} // namespace s7ui::elements

