#pragma once

#include "basic_node_item.hpp"

#include <memory>
#include <QVector>

class QGraphicsProxyWidget;
class QGraphicsSceneMouseEvent;

namespace s7ui::elements {

class VisualSinkDetailWindow;
class VisualSinkPreviewWidget;

class SinkNodeItem : public BasicNodeItem
{
    Q_OBJECT

public:
    explicit SinkNodeItem(const QString& typeName, int inputCount = 2, QGraphicsItem* parent = nullptr);
    ~SinkNodeItem() override;

    void setInputCount(int count);
    void setPreviewLevels(const QVector<double>& levels);

protected:
    void sizeChangedEvent(QSizeF oldSize, QSizeF newSize) override;
    void openPropertiesDialog() override;

private:
    void updatePreviewGeometry();

    QString typeName_;
    int inputCount_ = 2;
    QGraphicsProxyWidget* proxy_ = nullptr;
    VisualSinkPreviewWidget* preview_ = nullptr;
    std::unique_ptr<VisualSinkDetailWindow> detailWindow_;
};

} // namespace s7ui::elements

