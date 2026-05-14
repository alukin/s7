#pragma once

#include <s7factory.hpp>

#include <qschematic/items/node.hpp>

#include <QColor>

#include <string>
#include <vector>

class QGraphicsSceneMouseEvent;
class QPainter;

namespace s7ui::elements {

class BasicNodeItem : public QSchematic::Items::Node
{
    Q_OBJECT

public:
    explicit BasicNodeItem(const QString& typeName, QGraphicsItem* parent = nullptr);

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

    void setVisualStyle(const QColor& baseColor);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    virtual void openPropertiesDialog();

    void sizeChangedEvent(QSizeF oldSize, QSizeF newSize) override;

private:
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
    QColor baseColor_ = QColor(79, 79, 79, 120);
};

} // namespace s7ui::elements
