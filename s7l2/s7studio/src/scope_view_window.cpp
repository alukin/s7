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
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace s7studio::ui {

ScopeViewWindow::ScopeViewWindow(QString probeName,
                                 QWidget* parent,
                                 const Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , probeName_(std::move(probeName))
{
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(640, 360);
    setWindowTitle(QStringLiteral("Scope - %1").arg(probeName_));
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
    controls->addWidget(recordButton_);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    controls->addWidget(statusLabel_);

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
}

auto ScopeViewWindow::probeName() const -> QString
{
    return probeName_;
}

void ScopeViewWindow::appendSample(const double timeSeconds, const double value)
{
    appendSample(probeName_, timeSeconds, value);
}

void ScopeViewWindow::appendSample(const QString& channel, const double timeSeconds, const double value)
{
    auto& series = series_[channel];
    if (series.graphIndex < 0) {
        plot_->addGraph();
        series.graphIndex = plot_->graphCount() - 1;
        auto* graph = plot_->graph(series.graphIndex);
        graph->setName(channel);
        static const QVector<QColor> colors{QColor("#1f77b4"), QColor("#d62728"), QColor("#2ca02c"),
                                            QColor("#9467bd"), QColor("#ff7f0e"), QColor("#17becf")};
        QPen pen(colors[(series_.size() - 1) % colors.size()]);
        pen.setWidthF(1.6);
        graph->setPen(pen);
    }
    series.time.push_back(timeSeconds);
    series.value.push_back(value);
    if (recording_) {
        if (series.recordedTime.size() >= kMaxRecordedSamples) {
            recordingLimitReached_ = true;
            setRecording(false);
        } else {
            series.recordedTime.push_back(timeSeconds);
            series.recordedValue.push_back(value);
        }
    }

    const double minTime = std::max(0.0, timeSeconds - windowSecondsSpin_->value());
    trimVisibleBuffer(series, minTime);
    dirty_ = true;
}

void ScopeViewWindow::clearSamples()
{
    clearBuffers();
}

void ScopeViewWindow::setDroppedSampleCount(const std::uint64_t dropped)
{
    droppedSamples_ = dropped;
    updateStatus();
}

void ScopeViewWindow::setRecording(const bool recording)
{
    recording_ = recording;
    recordButton_->setText(recording_ ? QStringLiteral("Stop Record") : QStringLiteral("Start Record"));
    updateStatus();
}

void ScopeViewWindow::updateStatus()
{
    QStringList parts;
    if (recording_) {
        parts << QStringLiteral("recording");
    } else if (recordingLimitReached_) {
        parts << QStringLiteral("recording stopped at %1 samples").arg(kMaxRecordedSamples);
    }
    if (droppedSamples_ > 0) {
        parts << QStringLiteral("display dropped %1 samples").arg(droppedSamples_);
    }
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void ScopeViewWindow::clearBuffers()
{
    series_.clear();
    plot_->clearGraphs();
    recordingLimitReached_ = false;
    droppedSamples_ = 0;
    updateStatus();

    for (int index = 0; index < plot_->graphCount(); ++index) {
        plot_->graph(index)->data()->clear();
    }

    dirty_ = true;
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::exportRecordedCsv()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export recorded scope data"),
        QStringLiteral("%1_samples.csv").arg(probeName_),
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
    out << "scope,channel,time_s,value\n";
    for (auto it = series_.cbegin(); it != series_.cend(); ++it) {
        const auto& series = it.value();
        const int sampleCount = std::min(series.recordedTime.size(), series.recordedValue.size());
        for (int i = 0; i < sampleCount; ++i) {
            out << probeName_ << ',' << it.key() << ',' << series.recordedTime[i] << ',' << series.recordedValue[i] << '\n';
        }
    }
}

void ScopeViewWindow::toggleRecording()
{
    if (!recording_) {
        recordingLimitReached_ = false;
    }
    setRecording(!recording_);
}

void ScopeViewWindow::refreshPlot()
{
    if (!dirty_) {
        return;
    }

    dirty_ = false;
    double minTime = std::numeric_limits<double>::max();
    double maxTime = std::numeric_limits<double>::lowest();
    for (auto it = series_.cbegin(); it != series_.cend(); ++it) {
        const auto& series = it.value();
        if (auto* graph = plot_->graph(series.graphIndex)) {
            graph->setName(it.key());
            graph->setData(series.time, series.value, true);
        }
        if (!series.time.isEmpty()) {
            minTime = std::min(minTime, series.time.front());
            maxTime = std::max(maxTime, series.time.back());
        }
    }
    if (minTime <= maxTime) {
        plot_->xAxis->setRange(minTime, maxTime);
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
    plot_->yAxis->setLabel(probeName_);

}

void ScopeViewWindow::trimVisibleBuffer(Series& series, const double minTime)
{
    int trimCount = 0;
    const int total = series.time.size();
    while (trimCount < total && series.time[trimCount] < minTime) {
        ++trimCount;
    }
    if (trimCount > 0) {
        series.time.erase(series.time.begin(), series.time.begin() + trimCount);
        series.value.erase(series.value.begin(), series.value.begin() + trimCount);
    }
}

} // namespace s7studio::ui
