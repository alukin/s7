#pragma once

#include <s7factory.hpp>

#include <qschematic/items/node.hpp>

#include <QColor>

#include <string>
#include <vector>

class QGraphicsSceneMouseEvent;
class QPainter;
class QPainterPath;

namespace s7ui::elements {

class BasicNodeItem : public QSchematic::Items::Node
{
    Q_OBJECT

public:
    /// Shape types matching S7UI spec
    enum class Shape {
        Rectangle,      // Plain rectangle
        RoundedRect,    // Rectangle with rounded corners
        Diamond,        // 4-point diamond shape
        Hexagon,        // 6-point hexagon
        Circle,         // Circle/ellipse
        Custom          // Custom shape (fallback to rectangle)
    };

    explicit BasicNodeItem(const QString& typeName, QGraphicsItem* parent = nullptr);

    [[nodiscard]] std::shared_ptr<QSchematic::Items::Item> deepCopy() const override;

    void setNodeId(const QString& nodeId);
    [[nodiscard]] QString nodeId() const;

    void setNodeName(const QString& nodeName);
    [[nodiscard]] QString nodeName() const;

    void setTypeName(const QString& typeName);
    [[nodiscard]] QString typeName() const;

    void setParamsJson(const QString& paramsJson);
    [[nodiscard]] QString paramsJson() const;

    /// Set port layout using explicit connector counts.
    /// Port labels come from the optional descriptor ports list.
    void setPortLayout(int inputPorts, int outputPorts,
                       const std::vector<s7::PortDescriptor>& portDescs = {});

    /// Reconfigure ports from a type descriptor and a JSON params string.
    /// Computes the new port count via descriptor.compute_port_layout if available.
    /// Detects connectors that disappeared and marks connected wires as broken.
    void applyParamsAndUpdatePorts(const QString& newParamsJson);

    /// Set visual style including colors and shape
    void setVisualStyle(const QColor& baseColor, Shape shape = Shape::RoundedRect);
    void setShape(Shape shape);

    /// Set border and fill colors
    void setBorderColor(const QColor& color);
    void setFillColor(const QColor& color);
    [[nodiscard]] QColor borderColor() const;
    [[nodiscard]] QColor fillColor() const;

    /// Get node shape (renamed from shape() to avoid conflict with QGraphicsItem::shape())
    [[nodiscard]] Shape nodeShape() const;


protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    [[nodiscard]] QPainterPath shape() const override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    virtual void openPropertiesDialog();

    void sizeChangedEvent(QSizeF oldSize, QSizeF newSize) override;
    void copyAttributes(BasicNodeItem& dest) const;

private:
    [[nodiscard]] QPainterPath bodyPath(const QRectF& rect) const;
    void relayoutConnectors();
    void refreshToolTip();


    /// Serialise the type descriptor's default_params to a JSON object string.
    [[nodiscard]] static QString defaultParamsJsonFor(const std::string& typeName);

    /// Mark wires attached to connectors whose names are in `removedNames` as broken.
    void markBrokenWires(const std::vector<std::string>& removedConnectorNames);

    QString typeName_;
    QString nodeName_;
    QString nodeId_;
    QString paramsJson_ = QStringLiteral("{}");
    int inputPorts_  = 1;
    int outputPorts_ = 1;
    Shape shape_ = Shape::RoundedRect;
    QColor borderColor_ = QColor(Qt::black);
    QColor fillColor_ = QColor(79, 79, 79, 120);
};

} // namespace s7ui::elements
