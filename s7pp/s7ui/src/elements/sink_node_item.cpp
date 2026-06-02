#include "sink_node_item.hpp"

#include "visual_sink_detail_window.hpp"
#include "visual_sink_preview_widget.hpp"

#include <QGraphicsProxyWidget>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>

namespace s7ui::elements {

namespace
{
constexpr qreal kSinkMinWidthMm = 70.0;
constexpr qreal kSinkMinHeightMm = 43.0;
constexpr qreal kSinkExtraHeightPerInputMm = 8.0;
}

SinkNodeItem::SinkNodeItem(const QString& typeName, const int inputCount, QGraphicsItem* parent)
    : BasicNodeItem(typeName, parent), typeName_(typeName)
{
    proxy_ = new QGraphicsProxyWidget(this);
    // Clip only the embedded widget item; clipping all children at node level
    // can clip connector visuals/hit-areas that intentionally sit on edges.
    proxy_->setFlag(QGraphicsItem::ItemClipsToShape, true);
    preview_ = new VisualSinkPreviewWidget(true);
    preview_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    proxy_->setAcceptedMouseButtons(Qt::NoButton);
    preview_->setSignalMode(VisualSinkPreviewWidget::SignalMode::Flat);
    proxy_->setWidget(preview_);

    // Polling timer — only active when a live Probe is attached.
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(33); // ~30 Hz
    connect(pollTimer_, &QTimer::timeout, this, &SinkNodeItem::pollProbeData);

    complexMode_ = (typeName_ == "ComplexScopeSink");

    inputCount_ = 0;
    setInputCount(complexMode_ ? 1 : inputCount);

    if (complexMode_)
    {
        // One complex input connector, two plotted traces (Re/Im).
        preview_->setInputCount(2);
        setParamsJson(R"({"mode":"re_im"})");
    }
    updatePreviewGeometry();
}

SinkNodeItem::~SinkNodeItem()
{
    if (pollTimer_)
    {
        pollTimer_->stop();
    }
}

void SinkNodeItem::setVisualSize(const QSizeF& size)
{
    if (size.isValid() && size.width() > 0.0 && size.height() > 0.0) {
        visualSize_ = size;
        setSize(std::max<qreal>(size.width(), kSinkMinWidthMm),
                std::max<qreal>(size.height(), kSinkMinHeightMm));
        updatePreviewGeometry();
    }
}

void SinkNodeItem::setPreviewArea(const QRectF& previewArea)
{
    if (previewArea.isValid() && previewArea.width() > 0.0 && previewArea.height() > 0.0) {
        previewArea_ = previewArea;
        updatePreviewGeometry();
    }
}

std::shared_ptr<QSchematic::Items::Item> SinkNodeItem::deepCopy() const
{
    auto clone = std::make_shared<SinkNodeItem>(typeName_, inputCount_, parentItem());
    copyAttributes(*clone);
    clone->setInputCount(inputCount_);
    return clone;
}

void SinkNodeItem::setInputCount(int count)
{
    if (complexMode_)
    {
        inputCount_ = 1;
        clearConnectors();
        setPortLayout(1, 0);
        setSize(std::max<qreal>(size().width(), kSinkMinWidthMm),
                std::max<qreal>(size().height(), kSinkMinHeightMm));
        preview_->setInputCount(2);
        if (detailWindow_)
        {
            detailWindow_->setInputCount(1);
        }
        updatePreviewGeometry();
        return;
    }

    count = std::clamp(count, 1, 16);
    if (count == inputCount_)
    {
        updatePreviewGeometry();
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
    const qreal baseHeight = std::max<qreal>(size().height(), kSinkMinHeightMm);
    const qreal targetHeight = std::max<qreal>(baseHeight,
        kSinkMinHeightMm + static_cast<qreal>(inputCount_ - 1) * kSinkExtraHeightPerInputMm);
    setSize(std::max<qreal>(size().width(), kSinkMinWidthMm), targetHeight);
    updatePreviewGeometry();
}

void SinkNodeItem::setPreviewLevels(const QVector<double>& levels)
{
    preview_->setStaticLevels(levels);
}

void SinkNodeItem::setProbe(std::shared_ptr<s7::Probe> probe)
{
    complexProbe_.reset();
    probe_ = probe;
    if (detailWindow_)
    {
        detailWindow_->setProbe(probe);
    }
    if (probe)
    {
        // Switch preview to live mode
        preview_->setSignalMode(VisualSinkPreviewWidget::SignalMode::Live);
        startPolling();
    }
    else
    {
        stopPolling();
    }
}

void SinkNodeItem::setComplexProbe(std::shared_ptr<s7::ComplexProbe> probe)
{
    probe_.reset();
    complexProbe_ = probe;
    if (detailWindow_)
    {
        detailWindow_->setComplexProbe(probe);
    }
    if (probe)
    {
        preview_->setSignalMode(VisualSinkPreviewWidget::SignalMode::Live);
        preview_->setInputCount(2);
        startPolling();
    }
    else
    {
        stopPolling();
    }
}

void SinkNodeItem::clearProbe()
{
    probe_.reset();
    complexProbe_.reset();
    if (detailWindow_)
    {
        detailWindow_->clearProbe();
    }
    stopPolling();
    // Revert to flat static display
    preview_->setSignalMode(VisualSinkPreviewWidget::SignalMode::Flat);
}

void SinkNodeItem::pollProbeData()
{
    if (auto cprobe = complexProbe_.lock())
    {
        constexpr std::size_t kMaxSamples = 512;
        const auto samples = cprobe->readChannel(0, kMaxSamples);

        QVector<double> re;
        QVector<double> im;
        re.reserve(static_cast<qsizetype>(samples.size()));
        im.reserve(static_cast<qsizetype>(samples.size()));
        for (const auto& v : samples)
        {
            re.push_back(std::real(v));
            im.push_back(std::imag(v));
        }
        preview_->updateChannelData(0, re);
        preview_->updateChannelData(1, im);
        return;
    }

    auto probe = probe_.lock();
    if (!probe)
    {
        stopPolling();
        return;
    }

    // Read the most recent 512 samples per channel and update the preview.
    constexpr std::size_t kMaxSamples = 512;
    const int nCh = std::min(probe->channelCount(), inputCount_);
    for (int ch = 0; ch < nCh; ++ch)
    {
        const auto samples = probe->readChannel(ch, kMaxSamples);
        QVector<double> data;
        data.reserve(static_cast<qsizetype>(samples.size()));
        for (const auto v : samples)
        {
            data.push_back(v);
        }
        preview_->updateChannelData(ch, data);
    }
}

void SinkNodeItem::startPolling()
{
    if (pollTimer_ && !pollTimer_->isActive())
    {
        pollTimer_->start();
    }
}

void SinkNodeItem::stopPolling()
{
    if (pollTimer_ && pollTimer_->isActive())
    {
        pollTimer_->stop();
    }
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

    const QRectF r = sizeRect();
    const QRectF inner = previewArea_.has_value() && previewArea_->isValid()
                             ? *previewArea_
                             : r.adjusted(std::max<qreal>(6.0, r.width() * 0.16),
                                          std::max<qreal>(4.0, r.height() * 0.14),
                                          -std::max<qreal>(5.0, r.width() * 0.10),
                                          -std::max<qreal>(6.0, r.height() * 0.20));
    proxy_->setGeometry(inner);

    // Keep the embedded widget's backing size in lock-step with proxy geometry.
    // Use resize (not fixed size) so QWidget internals do not keep stale hard
    // constraints that can paint an extra rectangle in proxy rendering.
    if (preview_)
    {
        const int w = std::max(1, static_cast<int>(std::lround(inner.width())));
        const int h = std::max(1, static_cast<int>(std::lround(inner.height())));
        preview_->resize(w, h);
    }
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
    if (auto cprobe = complexProbe_.lock())
    {
        detailWindow_->setComplexProbe(cprobe);
    }
    else
    {
        detailWindow_->setProbe(probe_.lock());
    }

    detailWindow_->show();
    detailWindow_->raise();
    detailWindow_->activateWindow();
}

} // namespace s7ui::elements
