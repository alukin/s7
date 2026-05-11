#include "oscilloscope_sink_widget.hpp"

#include <qcp.h>

#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

OscilloscopeSinkWidget::OscilloscopeSinkWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    plot_ = new QCustomPlot(this);
    layout->addWidget(plot_);

    plot_->addGraph();
    graph_ = plot_->graph(0);
    graph_->setPen(QPen(QColor(40, 200, 120), 1.5));

    plot_->xAxis->setLabel("time [s]");
    plot_->yAxis->setLabel("amplitude");
    plot_->xAxis->setRange(0.0, 5.0);
    plot_->yAxis->setRange(-1.2, 1.2);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &OscilloscopeSinkWidget::appendSample);
    timer_->start(40);
}

void OscilloscopeSinkWidget::appendSample()
{
    constexpr double dt = 0.04;
    constexpr double kPi = 3.14159265358979323846;
    timeSec_ += dt;

    const double value = std::sin(2.0 * kPi * 0.75 * timeSec_) + 0.25 * std::sin(2.0 * kPi * 2.5 * timeSec_);
    graph_->addData(timeSec_, value);

    const double left = std::max(0.0, timeSec_ - 5.0);
    plot_->xAxis->setRange(left, left + 5.0);
    graph_->data()->removeBefore(left);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}


