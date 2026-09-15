#include "scope_view_window.hpp"

#include "qcp.h"

#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPen>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace s7studio::ui {

namespace {

QColor channelColor(int index)
{
    static const QColor palette[] = {
        QColor("#1f77b4"), QColor("#ff7f0e"), QColor("#2ca02c"), QColor("#d62728"),
        QColor("#9467bd"), QColor("#8c564b"), QColor("#e377c2"), QColor("#17becf")
    };
    constexpr int paletteSize = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if (index < 0) {
        return QColor("#1f77b4");
    }
    return palette[index % paletteSize];
}

} // namespace

ScopeViewWindow::ScopeViewWindow(QString scopeId,
                                 const int channelCount,
                                 QWidget* parent,
                                 const Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , scopeId_(std::move(scopeId))
    , channelCount_(std::max(1, channelCount))
    , channels_(channelCount_)
{
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(640, 360);
    setWindowTitle(QStringLiteral("Scope - %1").arg(scopeId_));
    resize(900, 520);

    auto* rootLayout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();

    controls->addWidget(new QLabel(QStringLiteral("Window (s):"), this));
    windowSecondsSpin_ = new QDoubleSpinBox(this);
    windowSecondsSpin_->setRange(0.05, 120.0);
    windowSecondsSpin_->setDecimals(2);
    windowSecondsSpin_->setSingleStep(0.25);
    windowSecondsSpin_->setValue(5.0);
    controls->addWidget(windowSecondsSpin_);

    autoScaleYCheck_ = new QCheckBox(QStringLiteral("Auto Y"), this);
    autoScaleYCheck_->setChecked(true);
    controls->addWidget(autoScaleYCheck_);

    recordButton_ = new QPushButton(QStringLiteral("Start Record"), this);
    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    exportButton_ = new QPushButton(QStringLiteral("Export CSV"), this);
    configureButton_ = new QPushButton(QStringLiteral("Configure"), this);
    controls->addWidget(recordButton_);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addWidget(configureButton_);
    controls->addStretch(1);

    rootLayout->addLayout(controls);

    plot_ = new QCustomPlot(this);
    rootLayout->addWidget(plot_, 1);

    configurePlot();

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &ScopeViewWindow::refreshPlot);
    repaintTimer_->start();

    connect(recordButton_, &QPushButton::clicked, this, &ScopeViewWindow::toggleRecording);
    connect(clearButton_, &QPushButton::clicked, this, &ScopeViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &ScopeViewWindow::exportRecordedCsv);
    connect(configureButton_, &QPushButton::clicked, this, [this] {
        emit configureRequested(scopeId_, channelCount_);
    });
}

auto ScopeViewWindow::scopeId() const -> QString
{
    return scopeId_;
}

auto ScopeViewWindow::channelCount() const -> int
{
    return channelCount_;
}

void ScopeViewWindow::appendSample(const int channel, const double timeSeconds, const double value)
{
    if (channel < 0 || channel >= channelCount_) {
        return;
    }

    auto& series = channels_[channel];
    series.time.push_back(timeSeconds);
    series.value.push_back(value);
    if (recording_) {
        series.recordedTime.push_back(timeSeconds);
        series.recordedValue.push_back(value);
    }

    const double minTime = std::max(0.0, timeSeconds - windowSecondsSpin_->value());
    trimVisibleBuffer(series, minTime);
    dirty_ = true;
}

void ScopeViewWindow::clearSamples()
{
    clearBuffers();
}

void ScopeViewWindow::clearBuffers()
{
    for (auto& series : channels_) {
        series.time.clear();
        series.value.clear();
        series.recordedTime.clear();
        series.recordedValue.clear();
    }

    for (int i = 0; i < plot_->graphCount(); ++i) {
        if (auto* graph = plot_->graph(i)) {
            graph->data()->clear();
        }
    }

    dirty_ = true;
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::exportRecordedCsv()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export recorded scope data"),
        QStringLiteral("%1_samples.csv").arg(scopeId_),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());
        return;
    }

    QTextStream out(&file);
    out << "scope_id,channel,time_s,value\n";
    for (int channel = 0; channel < channelCount_; ++channel) {
        const auto& series = channels_[channel];
        const int sampleCount = std::min(series.recordedTime.size(), series.recordedValue.size());
        for (int i = 0; i < sampleCount; ++i) {
            out << scopeId_ << ',' << channel << ',' << series.recordedTime[i] << ',' << series.recordedValue[i] << '\n';
        }
    }
}

void ScopeViewWindow::toggleRecording()
{
    recording_ = !recording_;
    recordButton_->setText(recording_ ? QStringLiteral("Stop Record") : QStringLiteral("Start Record"));
}

void ScopeViewWindow::refreshPlot()
{
    if (!dirty_) {
        return;
    }

    dirty_ = false;
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();

    for (int channel = 0; channel < channelCount_; ++channel) {
        const auto& series = channels_[channel];
        if (auto* graph = plot_->graph(channel)) {
            graph->setData(series.time, series.value, true);
        }

        if (!series.time.isEmpty()) {
            minX = std::min(minX, series.time.front());
            maxX = std::max(maxX, series.time.back());
        }
    }

    if (minX <= maxX) {
        plot_->xAxis->setRange(minX, maxX);
    }

    if (autoScaleYCheck_->isChecked()) {
        plot_->rescaleAxes(true);
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::configurePlot()
{
    plot_->legend->setVisible(true);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend | QCP::iSelectPlottables);
    plot_->xAxis->setLabel(QStringLiteral("time (s)"));
    plot_->yAxis->setLabel(QStringLiteral("value"));

    for (int channel = 0; channel < channelCount_; ++channel) {
        plot_->addGraph();
        auto* graph = plot_->graph(channel);
        graph->setName(QStringLiteral("ch%1").arg(channel));
        QPen pen(channelColor(channel));
        pen.setWidthF(1.6);
        graph->setPen(pen);
    }
}

void ScopeViewWindow::trimVisibleBuffer(ChannelSeries& channel, const double minTime)
{
    int trimCount = 0;
    const int total = channel.time.size();
    while (trimCount < total && channel.time[trimCount] < minTime) {
        ++trimCount;
    }
    if (trimCount > 0) {
        channel.time.erase(channel.time.begin(), channel.time.begin() + trimCount);
        channel.value.erase(channel.value.begin(), channel.value.begin() + trimCount);
    }
}

} // namespace s7studio::ui


