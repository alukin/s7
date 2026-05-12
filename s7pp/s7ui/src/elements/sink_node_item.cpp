#include "sink_node_item.hpp"

#include "visual_sink_detail_window.hpp"
#include "visual_sink_preview_widget.hpp"

#include <QGraphicsProxyWidget>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <complex>
#include <cstddef>

namespace s7ui::elements {

SinkNodeItem::SinkNodeItem(const QString& typeName, const int inputCount, QGraphicsItem* parent)
    : BasicNodeItem(typeName, parent), typeName_(typeName)
{
    setSize(72, 44);

    proxy_ = new QGraphicsProxyWidget(this);
    preview_ = new VisualSinkPreviewWidget(true);
    preview_->setSignalMode(VisualSinkPreviewWidget::SignalMode::Flat);
    proxy_->setWidget(preview_);
    connect(preview_, &VisualSinkPreviewWidget::doubleClicked, this, [this] { openPropertiesDialog(); });

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

void SinkNodeItem::setInputCount(int count)
{
    if (complexMode_)
    {
        inputCount_ = 1;
        clearConnectors();
        setPortLayout(1, 0);
        preview_->setInputCount(2);
        if (detailWindow_)
        {
            detailWindow_->setInputCount(1);
        }
        return;
    }

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

void SinkNodeItem::setProbe(std::shared_ptr<s7::Probe> probe)
{
    complexProbe_.reset();
    probe_ = probe;
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

