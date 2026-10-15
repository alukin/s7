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

namespace {
const QColor kSeriesColor("#1f77b4");
} // namespace

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
    series_.time.push_back(timeSeconds);
    series_.value.push_back(value);
    if (recording_) {
        if (series_.recordedTime.size() >= kMaxRecordedSamples) {
            recordingLimitReached_ = true;
            setRecording(false);
        } else {
            series_.recordedTime.push_back(timeSeconds);
            series_.recordedValue.push_back(value);
        }
    }

    const double minTime = std::max(0.0, timeSeconds - windowSecondsSpin_->value());
    trimVisibleBuffer(minTime);
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
    series_.time.clear();
    series_.value.clear();
    series_.recordedTime.clear();
    series_.recordedValue.clear();
    recordingLimitReached_ = false;
    droppedSamples_ = 0;
    updateStatus();

    if (auto* graph = plot_->graph(0)) {
        graph->data()->clear();
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
    out << "probe,time_s,value\n";
    const int sampleCount = std::min(series_.recordedTime.size(), series_.recordedValue.size());
    for (int i = 0; i < sampleCount; ++i) {
        out << probeName_ << ',' << series_.recordedTime[i] << ',' << series_.recordedValue[i] << '\n';
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
    if (auto* graph = plot_->graph(0)) {
        graph->setData(series_.time, series_.value, true);
    }

    if (!series_.time.isEmpty()) {
        plot_->xAxis->setRange(series_.time.front(), series_.time.back());
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

    plot_->addGraph();
    auto* graph = plot_->graph(0);
    graph->setName(probeName_);
    QPen pen(kSeriesColor);
    pen.setWidthF(1.6);
    graph->setPen(pen);
}

void ScopeViewWindow::trimVisibleBuffer(const double minTime)
{
    int trimCount = 0;
    const int total = series_.time.size();
    while (trimCount < total && series_.time[trimCount] < minTime) {
        ++trimCount;
    }
    if (trimCount > 0) {
        series_.time.erase(series_.time.begin(), series_.time.begin() + trimCount);
        series_.value.erase(series_.value.begin(), series_.value.begin() + trimCount);
    }
}

} // namespace s7studio::ui
