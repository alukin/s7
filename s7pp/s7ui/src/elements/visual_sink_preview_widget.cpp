#include "visual_sink_preview_widget.hpp"

#include <qcp.h>

#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace s7ui::elements {

namespace {

constexpr std::array<QColor, 6> kPalette = {
    QColor(40, 200, 120),
    QColor(90, 170, 250),
    QColor(255, 160, 70),
    QColor(230, 105, 200),
    QColor(240, 225, 110),
    QColor(150, 150, 255)
};

} // namespace

VisualSinkPreviewWidget::VisualSinkPreviewWidget(const bool compact, QWidget* parent)
    : QWidget(parent), compact_(compact)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    plot_ = new QCustomPlot(this);
    plot_->setNoAntialiasingOnDrag(true);
    plot_->setAntialiasedElements(QCP::aeAll);
    plot_->setBackground(QBrush(QColor(245, 245, 245)));
    plot_->legend->setVisible(false);
    plot_->axisRect()->setupFullAxesBox(true);
    plot_->xAxis->setRange(0.0, timeWindowSec_);
    plot_->yAxis->setRange(-1.5, 1.5);
    plot_->setInteractions(compact_ ? QCP::iNone : (QCP::iRangeDrag | QCP::iRangeZoom));

    if (compact_)
    {
        plot_->xAxis->setVisible(false);
        plot_->yAxis->setVisible(false);
        plot_->xAxis2->setVisible(false);
        plot_->yAxis2->setVisible(false);
        plot_->axisRect()->setMargins(QMargins(4, 4, 4, 4));
    }
    else
    {
        plot_->xAxis->setLabel("time [s]");
        plot_->yAxis->setLabel("amplitude");
        plot_->axisRect()->setMargins(QMargins(10, 8, 10, 8));
    }

    layout->addWidget(plot_);

    rebuildGraphs();

    staticLevels_.fill(0.0, inputCount_);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &VisualSinkPreviewWidget::appendSample);
    // Keep compact previews quiet by default; animation is only used in
    // explicit Synthetic mode.
    setSignalMode(signalMode_);
}

void VisualSinkPreviewWidget::setInputCount(int count)
{
    count = std::clamp(count, 1, 16);
    if (count == inputCount_)
    {
        return;
    }

    inputCount_ = count;
    staticLevels_.resize(inputCount_);
    for (auto& level : staticLevels_)
    {
        if (!std::isfinite(level))
        {
            level = 0.0;
        }
    }
    liveSamples_.resize(inputCount_);
    rebuildGraphs();
}

auto VisualSinkPreviewWidget::inputCount() const -> int
{
    return inputCount_;
}

void VisualSinkPreviewWidget::setGain(const double gain)
{
    gain_ = std::clamp(gain, 0.1, 6.0);
}

void VisualSinkPreviewWidget::setTimeWindow(const double seconds)
{
    timeWindowSec_ = std::clamp(seconds, 0.5, 12.0);
    if (signalMode_ == SignalMode::Live)
    {
        plot_->xAxis->setRange(0.0, timeWindowSec_);
        plot_->replot(QCustomPlot::rpQueuedReplot);
    }
}

void VisualSinkPreviewWidget::setSignalMode(const SignalMode mode)
{
    signalMode_ = mode;

    if (signalMode_ == SignalMode::Live)
    {
        // Stop the synthetic/flat animation timer; live data is pushed externally.
        if (timer_) timer_->stop();

        // Clear existing graph data.
        for (auto* graph : graphs_)
        {
            graph->data()->clear();
        }
        timeSec_ = 0.0;
        liveSamples_.assign(inputCount_, {});
        plot_->replot(QCustomPlot::rpQueuedReplot);
    }
    else if (signalMode_ == SignalMode::Synthetic)
    {
        if (timer_ && !timer_->isActive())
        {
            timer_->start(compact_ ? 80 : 35);
        }
    }
    else
    {
        // Flat mode intentionally shows no scrolling trace when the schema is
        // not running.  This avoids implying live activity.
        if (timer_) timer_->stop();
        for (auto* graph : graphs_)
        {
            graph->data()->clear();
        }
        timeSec_ = 0.0;
        liveSamples_.assign(inputCount_, {});
        plot_->replot(QCustomPlot::rpQueuedReplot);
    }
}

void VisualSinkPreviewWidget::setStaticLevels(const QVector<double>& levels)
{
    staticLevels_ = levels;
    if (staticLevels_.size() < inputCount_)
    {
        staticLevels_.resize(inputCount_);
    }
}

void VisualSinkPreviewWidget::updateChannelData(int channel, const QVector<double>& samples)
{
    if (signalMode_ != SignalMode::Live) return;
    if (channel < 0 || channel >= graphs_.size()) return;
    if (samples.isEmpty()) return;

    // Replace the graph's data with the provided samples mapped to the active time window.
    QVector<double> keys;
    keys.reserve(samples.size());
    const auto n = static_cast<double>(samples.size());
    const double dt = (n > 1.0) ? (timeWindowSec_ / (n - 1.0)) : timeWindowSec_;
    for (int i = 0; i < samples.size(); ++i)
    {
        keys.push_back(static_cast<double>(i) * dt);
    }

    graphs_[channel]->setData(keys, samples, true);

    // Rescale Y axis to fit the data if needed.
    double yMin = -1.5, yMax = 1.5;
    // Store current samples for Y-range estimation.
    const std::size_t channelIndex = static_cast<std::size_t>(channel);
    if (channelIndex < liveSamples_.size())
    {
        liveSamples_[channelIndex].clear();
        liveSamples_[channelIndex].reserve(samples.size());
        for (const auto v : samples) liveSamples_[channelIndex].push_back(v);
    }

    for (int ch = 0; ch < graphs_.size(); ++ch)
    {
        if (graphs_[ch]->data()->isEmpty()) continue;
        const std::size_t chIndex = static_cast<std::size_t>(ch);
        if (chIndex < liveSamples_.size())
        {
            for (const auto v : liveSamples_[chIndex])
            {
                yMin = std::min(yMin, v * gain_);
                yMax = std::max(yMax, v * gain_);
            }
        }
    }
    const double pad = std::max(0.1, (yMax - yMin) * 0.05);
    plot_->xAxis->setRange(0.0, timeWindowSec_);
    plot_->yAxis->setRange(yMin - pad, yMax + pad);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void VisualSinkPreviewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    emit doubleClicked();
    QWidget::mouseDoubleClickEvent(event);
}

void VisualSinkPreviewWidget::rebuildGraphs()
{
    if (!plot_)
    {
        return;
    }

    plot_->clearGraphs();
    graphs_.clear();
    for (int i = 0; i < inputCount_; ++i)
    {
        plot_->addGraph();
        auto* graph = plot_->graph(i);
        graph->setPen(QPen(kPalette[static_cast<std::size_t>(i) % kPalette.size()], compact_ ? 1.1 : 1.6));
        graphs_.push_back(graph);
    }
    liveSamples_.assign(inputCount_, {});
}

void VisualSinkPreviewWidget::appendSample()
{
    if (signalMode_ == SignalMode::Live) return; // driven externally
    if (signalMode_ == SignalMode::Flat) return;

    constexpr double kPi = std::numbers::pi_v<double>;
    const double dt = compact_ ? 0.08 : 0.035;
    timeSec_ += dt;

    for (int i = 0; i < graphs_.size(); ++i)
    {
        double value = 0.0;
        if (signalMode_ == SignalMode::Synthetic)
        {
            const double f1 = 0.3 + 0.2 * i;
            const double f2 = 1.1 + 0.15 * i;
            const double phase = 0.7 * i;
            value = gain_ * (std::sin(2.0 * kPi * f1 * timeSec_ + phase)
                + 0.2 * std::sin(2.0 * kPi * f2 * timeSec_ + 0.5 * phase));
        }
        else // Flat
        {
            const double base = i < staticLevels_.size() ? staticLevels_[i] : 0.0;
            value = gain_ * base;
        }
        graphs_[i]->addData(timeSec_, value);
    }

    const double left = std::max(0.0, timeSec_ - timeWindowSec_);
    plot_->xAxis->setRange(left, left + timeWindowSec_);
    for (auto* graph : graphs_)
    {
        graph->data()->removeBefore(left);
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

} // namespace s7ui::elements

