#pragma once

#include <qschematic/items/node.hpp>

class QGraphicsSceneMouseEvent;
class QPainter;

namespace s7ui::elements {

class BasicNodeItem : public QSchematic::Items::Node
{
    Q_OBJECT

public:
    explicit BasicNodeItem(const QString& typeName, QGraphicsItem* parent = nullptr);

    void setNodeId(const QString& nodeId);
    QString nodeId() const;

    void setNodeName(const QString& nodeName);
    QString nodeName() const;

    void setTypeName(const QString& typeName);
    QString typeName() const;

    void setParamsJson(const QString& paramsJson);
    QString paramsJson() const;

    void setPortLayout(int inputPorts, int outputPorts);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    virtual void openPropertiesDialog();

private:
    void refreshToolTip();

    QString typeName_;
    QString nodeName_;
    QString nodeId_;
    QString paramsJson_ = "{}";
    int inputPorts_ = 1;
    int outputPorts_ = 1;
};

} // namespace s7ui::elements

