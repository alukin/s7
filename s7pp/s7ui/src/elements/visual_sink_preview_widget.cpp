#include "visual_sink_preview_widget.hpp"

#include <qcp.h>

#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

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

}

VisualSinkPreviewWidget::VisualSinkPreviewWidget(const bool compact, QWidget* parent)
    : QWidget(parent), compact_(compact)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    plot_ = new QCustomPlot(this);
    plot_->legend->setVisible(false);
    plot_->xAxis->setRange(0.0, timeWindowSec_);
    plot_->yAxis->setRange(-1.5, 1.5);
    plot_->setInteractions(compact_ ? QCP::iNone : (QCP::iRangeDrag | QCP::iRangeZoom));

    if (compact_)
    {
        plot_->xAxis->setVisible(false);
        plot_->yAxis->setVisible(false);
        plot_->axisRect()->setMargins(QMargins(4, 4, 4, 4));
    }
    else
    {
        plot_->xAxis->setLabel("time [s]");
        plot_->yAxis->setLabel("amplitude");
    }

    layout->addWidget(plot_);

    rebuildGraphs();

    staticLevels_.fill(0.0, inputCount_);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &VisualSinkPreviewWidget::appendSample);
    timer_->start(compact_ ? 80 : 35);
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
    for (int i = 0; i < staticLevels_.size(); ++i)
    {
        if (!std::isfinite(staticLevels_[i]))
        {
            staticLevels_[i] = 0.0;
        }
    }
    rebuildGraphs();
}

int VisualSinkPreviewWidget::inputCount() const
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
}

void VisualSinkPreviewWidget::setSignalMode(const SignalMode mode)
{
    signalMode_ = mode;
}

void VisualSinkPreviewWidget::setStaticLevels(const QVector<double>& levels)
{
    staticLevels_ = levels;
    if (staticLevels_.size() < inputCount_)
    {
        staticLevels_.resize(inputCount_);
    }
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
        graph->setPen(QPen(kPalette[static_cast<std::size_t>(i) % kPalette.size()], compact_ ? 1.0 : 1.5));
        graphs_.push_back(graph);
    }
}

void VisualSinkPreviewWidget::appendSample()
{
    constexpr double kPi = 3.14159265358979323846;
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
        else
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

