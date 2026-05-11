#include "sink_node_item.hpp"

#include "visual_sink_detail_window.hpp"
#include "visual_sink_preview_widget.hpp"

#include <QGraphicsProxyWidget>

#include <algorithm>

namespace s7ui::elements {

SinkNodeItem::SinkNodeItem(const QString& typeName, const int inputCount, QGraphicsItem* parent)
    : BasicNodeItem(typeName, parent), typeName_(typeName)
{
    setSize(260, 150);

    proxy_ = new QGraphicsProxyWidget(this);
    preview_ = new VisualSinkPreviewWidget(true);
    preview_->setSignalMode(VisualSinkPreviewWidget::SignalMode::Flat);
    proxy_->setWidget(preview_);
    connect(preview_, &VisualSinkPreviewWidget::doubleClicked, this, [this] { openPropertiesDialog(); });

    inputCount_ = 0;
    setInputCount(inputCount);
    updatePreviewGeometry();
}

SinkNodeItem::~SinkNodeItem() = default;

void SinkNodeItem::setInputCount(int count)
{
    count = std::clamp(count, 1, 16);
    if (count == inputCount_)
    {
        return;
    }

    inputCount_ = count;
    preview_->setInputCount(inputCount_);
    if (detailWindow_)
    {
        detailWindow_->setInputCount(inputCount_);
    }

    clearConnectors();
    setPortLayout(inputCount_, 0);
}

void SinkNodeItem::setPreviewLevels(const QVector<double>& levels)
{
    preview_->setStaticLevels(levels);
}

void SinkNodeItem::sizeChangedEvent(QSizeF oldSize, QSizeF newSize)
{
    QSchematic::Items::Node::sizeChangedEvent(oldSize, newSize);
    Q_UNUSED(oldSize);
    Q_UNUSED(newSize);
    updatePreviewGeometry();
}

void SinkNodeItem::updatePreviewGeometry()
{
    if (!proxy_)
    {
        return;
    }

    const QRectF inner = sizeRect().adjusted(20, 14, -14, -14);
    proxy_->setGeometry(inner);
}

void SinkNodeItem::openPropertiesDialog()
{
    if (!detailWindow_)
    {
        detailWindow_ = std::make_unique<VisualSinkDetailWindow>(typeName_);
        detailWindow_->setInputCount(inputCount_);
        detailWindow_->setIdentity(nodeName(), nodeId());
        connect(detailWindow_.get(), &VisualSinkDetailWindow::inputCountChanged,
                this, [this](int inputs) { setInputCount(inputs); });
        connect(detailWindow_.get(), &VisualSinkDetailWindow::identityChanged,
                this, [this](const QString& name, const QString& id) {
                    setNodeName(name);
                    setNodeId(id);
                });
    }

    detailWindow_->setIdentity(nodeName(), nodeId());

    detailWindow_->show();
    detailWindow_->raise();
    detailWindow_->activateWindow();
}

} // namespace s7ui::elements

