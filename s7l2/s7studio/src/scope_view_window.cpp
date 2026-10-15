#include "scope_view_window.hpp"

#include "qcp.h"

#include <QCheckBox>
#include <QColor>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPen>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryFile>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace s7studio::ui {

namespace {
constexpr qint64 kSpillIndexStride = 4096;
}

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

    autoScaleYCheck_ = new QCheckBox(QStringLiteral("Auto Y"), this);
    autoScaleYCheck_->setChecked(true);
    controls->addWidget(autoScaleYCheck_);
    fitAllTimeCheck_ = new QCheckBox(QStringLiteral("Fit full run"), this);
    fitAllTimeCheck_->setChecked(true);
    controls->addWidget(fitAllTimeCheck_);
    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    exportButton_ = new QPushButton(QStringLiteral("Export Full CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    controls->addWidget(statusLabel_);

    rootLayout->addLayout(controls);

    plot_ = new QCustomPlot(this);
    rootLayout->addWidget(plot_, 1);
    timeScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    timeScrollBar_->setRange(0, 10000);
    timeScrollBar_->setEnabled(false);
    timeScrollBar_->setToolTip(QStringLiteral("Scroll the visible time range. Zoom with the mouse wheel and pan by dragging in the plot."));
    rootLayout->addWidget(timeScrollBar_);

    configurePlot();

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &ScopeViewWindow::refreshPlot);
    repaintTimer_->start();

    connect(clearButton_, &QPushButton::clicked, this, &ScopeViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &ScopeViewWindow::exportRecordedCsv);
    connect(timeScrollBar_, &QScrollBar::valueChanged, this, &ScopeViewWindow::scrollToTimePosition);
    connect(fitAllTimeCheck_, &QCheckBox::toggled, this, [this](const bool fitAll) {
        if (fitAll && hasTimeRange_) {
            plot_->xAxis->setRange(dataMinTime_, dataMaxTime_ > dataMinTime_ ? dataMaxTime_ : dataMinTime_ + 1.0);
        }
        updateTimeScrollBar();
        const auto range = plot_->xAxis->range();
        for (auto it = series_.begin(); it != series_.end(); ++it) {
            setVisibleGraphData(it.value(), range.lower, range.upper, fitAll);
        }
        plot_->replot(QCustomPlot::rpQueuedReplot);
    });
    connect(plot_->xAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange& range) {
                updateTimeScrollBar();
                if (!fitAllTimeCheck_->isChecked() && hasTimeRange_) {
                    for (auto it = series_.begin(); it != series_.end(); ++it) {
                        setVisibleGraphData(it.value(), range.lower, range.upper, false);
                    }
                    plot_->replot(QCustomPlot::rpQueuedReplot);
                }
            });
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
    if (!series.hasSamples) {
        series.firstTime = timeSeconds;
        series.hasSamples = true;
    }
    series.lastTime = timeSeconds;
    series.time.push_back(timeSeconds);
    series.value.push_back(value);
    if (series.spillFile) {
        if (!appendToSpill(series, timeSeconds, value)) {
            series.spillFailed = true;
            temporaryWriteError_ = QStringLiteral("temporary data write failed; recorded samples beyond this point are not full resolution");
            updateStatus();
        }
    } else if (!series.spillFailed && series.time.size() >= kMaxInMemorySamplesPerSeries && !spillSeries(series)) {
        // spillSeries already set temporaryWriteError_ and called updateStatus()
        // on failure; spillFailed stops retrying it on every further sample.
        series.spillFailed = true;
    }
    // The in-memory buffer is bounded either way so memory stays flat; once
    // spilling has failed there is no disk backup, so decimating here is
    // irreversible data loss, not a safe trim, which is why spillFailed also
    // keeps the warning above on screen instead of clearing after one retry.
    if (series.time.size() > kMaxInMemorySamplesPerSeries) {
        decimatePlotBuffer(series);
    }

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

void ScopeViewWindow::updateStatus()
{
    QStringList parts;
    bool spilled = false;
    bool spillFailed = false;
    for (auto it = series_.cbegin(); it != series_.cend(); ++it) {
        spilled |= static_cast<bool>(it->spillFile);
        spillFailed |= it->spillFailed;
    }
    if (spilled) {
        parts << QStringLiteral("temporary disk buffer active");
    }
    if (spillFailed) {
        parts << QStringLiteral("WARNING: temporary disk buffer unavailable; recording is no longer full resolution");
    }
    if (droppedSamples_ > 0) {
        parts << QStringLiteral("display dropped %1 samples").arg(droppedSamples_);
    }
    if (!temporaryWriteError_.isEmpty()) {
        parts << temporaryWriteError_;
    }
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void ScopeViewWindow::clearBuffers()
{
    series_.clear();
    plot_->clearGraphs();
    hasTimeRange_ = false;
    timeScrollBar_->setEnabled(false);
    droppedSamples_ = 0;
    temporaryWriteError_.clear();
    updateStatus();

    for (int index = 0; index < plot_->graphCount(); ++index) {
        plot_->graph(index)->data()->clear();
    }

    dirty_ = true;
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::exportRecordedCsv()
{
    const bool anySpillFailed = std::any_of(series_.cbegin(), series_.cend(),
                                            [](const Series& series) { return series.spillFailed; });
    if (anySpillFailed) {
        QMessageBox::warning(this, QStringLiteral("Export may be incomplete"),
                             QStringLiteral("The temporary disk buffer failed during this recording, so some "
                                            "channels were decimated in memory and this export is not full "
                                            "resolution for the affected time range."));
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export scope data"),
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
        if (series.spillFile) {
            series.spillFile->flush();
            series.spillFile->seek(0);
            QDataStream in(series.spillFile.data());
            in.setByteOrder(QDataStream::LittleEndian);
            double time = 0.0;
            double value = 0.0;
            while (!in.atEnd()) {
                in >> time >> value;
                if (in.status() != QDataStream::Ok) {
                    break;
                }
                out << probeName_ << ',' << it.key() << ',' << time << ',' << value << '\n';
            }
            series.spillFile->seek(series.spillFile->size());
        } else {
            const int sampleCount = std::min(series.time.size(), series.value.size());
            for (int i = 0; i < sampleCount; ++i) {
                out << probeName_ << ',' << it.key() << ',' << series.time[i] << ',' << series.value[i] << '\n';
            }
        }
    }
}

auto ScopeViewWindow::spillSeries(Series& series) -> bool
{
    QSettings settings;
    temporaryDirectory_ = settings.value(QStringLiteral("scope/temporaryDirectory"), QDir::tempPath()).toString();
    if (!QDir().mkpath(temporaryDirectory_)) {
        temporaryWriteError_ = QStringLiteral("cannot create temporary data directory");
        updateStatus();
        return false;
    }
    auto file = QSharedPointer<QTemporaryFile>::create(
        QDir(temporaryDirectory_).filePath(QStringLiteral("s7scope-XXXXXX.bin")));
    file->setAutoRemove(true);
    if (!file->open()) {
        temporaryWriteError_ = QStringLiteral("cannot create temporary data file: %1").arg(file->errorString());
        updateStatus();
        return false;
    }
    file->setAutoRemove(true);
    QDataStream out(file.data());
    out.setByteOrder(QDataStream::LittleEndian);
    for (int index = 0; index < series.time.size(); ++index) {
        if (index % kSpillIndexStride == 0) {
            series.spillIndex.push_back(Series::SpillIndexEntry{series.time[index], file->pos()});
        }
        out << series.time[index] << series.value[index];
    }
    if (out.status() != QDataStream::Ok || !file->flush()) {
        temporaryWriteError_ = QStringLiteral("cannot write temporary data file: %1").arg(file->errorString());
        updateStatus();
        return false;
    }
    series.spilledSamples = series.time.size();
    series.spillFile = std::move(file);
    updateStatus();
    return true;
}

auto ScopeViewWindow::appendToSpill(Series& series, const double timeSeconds, const double value) -> bool
{
    if (!series.spillFile || !series.spillFile->seek(series.spillFile->size())) {
        return false;
    }
    if (series.spilledSamples % kSpillIndexStride == 0) {
        series.spillIndex.push_back(Series::SpillIndexEntry{timeSeconds, series.spillFile->pos()});
    }
    QDataStream out(series.spillFile.data());
    out.setByteOrder(QDataStream::LittleEndian);
    out << timeSeconds << value;
    if (out.status() != QDataStream::Ok) {
        return false;
    }
    ++series.spilledSamples;
    return true;
}

void ScopeViewWindow::setVisibleGraphData(Series& series,
                                          const double lowerTime,
                                          const double upperTime,
                                          const bool fitFullRun)
{
    auto* graph = plot_->graph(series.graphIndex);
    if (!graph) {
        return;
    }
    if (!series.spillFile || fitFullRun || upperTime - lowerTime >= dataMaxTime_ - dataMinTime_) {
        graph->setData(series.time, series.value, true);
        return;
    }

    auto index = std::upper_bound(series.spillIndex.cbegin(), series.spillIndex.cend(), lowerTime,
        [](const double time, const Series::SpillIndexEntry& entry) { return time < entry.timeSeconds; });
    if (index != series.spillIndex.cbegin()) {
        --index;
    }
    if (series.spillIndex.isEmpty() || !series.spillFile->seek(series.spillIndex.isEmpty() ? 0 : index->byteOffset)) {
        graph->setData(series.time, series.value, true);
        return;
    }

    QDataStream in(series.spillFile.data());
    in.setByteOrder(QDataStream::LittleEndian);
    QVector<double> time;
    QVector<double> value;
    double sampleTime = 0.0;
    double sampleValue = 0.0;
    double previousTime = 0.0;
    double previousValue = 0.0;
    bool hasPrevious = false;
    while (!in.atEnd()) {
        in >> sampleTime >> sampleValue;
        if (in.status() != QDataStream::Ok || sampleTime > upperTime) {
            break;
        }
        if (sampleTime < lowerTime) {
            previousTime = sampleTime;
            previousValue = sampleValue;
            hasPrevious = true;
            continue;
        }
        if (hasPrevious) {
            time.push_back(previousTime);
            value.push_back(previousValue);
            hasPrevious = false;
        }
        time.push_back(sampleTime);
        value.push_back(sampleValue);
    }
    series.spillFile->seek(series.spillFile->size());
    while (time.size() > kMaxInMemorySamplesPerSeries) {
        QVector<double> reducedTime;
        QVector<double> reducedValue;
        reducedTime.reserve((time.size() + 1) / 2);
        reducedValue.reserve((value.size() + 1) / 2);
        for (int sample = 0; sample < time.size(); sample += 2) {
            reducedTime.push_back(time[sample]);
            reducedValue.push_back(value[sample]);
        }
        time = std::move(reducedTime);
        value = std::move(reducedValue);
    }
    graph->setData(time, value, true);
}

void ScopeViewWindow::decimatePlotBuffer(Series& series)
{
    QVector<double> time;
    QVector<double> value;
    time.reserve((series.time.size() + 1) / 2);
    value.reserve((series.value.size() + 1) / 2);
    for (int index = 0; index < series.time.size(); index += 2) {
        time.push_back(series.time[index]);
        value.push_back(series.value[index]);
    }
    series.time = std::move(time);
    series.value = std::move(value);
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
        }
        if (series.hasSamples) {
            minTime = std::min(minTime, series.firstTime);
            maxTime = std::max(maxTime, series.lastTime);
        }
    }
    if (minTime <= maxTime) {
        dataMinTime_ = minTime;
        dataMaxTime_ = maxTime;
        hasTimeRange_ = true;
        if (fitAllTimeCheck_->isChecked()) {
            plot_->xAxis->setRange(minTime, maxTime > minTime ? maxTime : minTime + 1.0);
        }
        updateTimeScrollBar();
        const auto range = plot_->xAxis->range();
        for (auto it = series_.begin(); it != series_.end(); ++it) {
            setVisibleGraphData(it.value(), range.lower, range.upper, fitAllTimeCheck_->isChecked());
        }
    }

    if (autoScaleYCheck_->isChecked()) {
        plot_->rescaleAxes(true);
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::updateTimeScrollBar()
{
    if (!hasTimeRange_) {
        timeScrollBar_->setEnabled(false);
        return;
    }
    const double totalSpan = dataMaxTime_ - dataMinTime_;
    const double viewSpan = plot_->xAxis->range().size();
    const bool canScroll = totalSpan > 0.0 && viewSpan < totalSpan;
    timeScrollBar_->setEnabled(canScroll && !fitAllTimeCheck_->isChecked());
    const int pageStep = totalSpan > 0.0
        ? std::clamp(static_cast<int>(std::round(10000.0 * viewSpan / totalSpan)), 1, 10000)
        : 10000;
    updatingTimeScrollBar_ = true;
    timeScrollBar_->setPageStep(pageStep);
    const double scrollableSpan = std::max(0.0, totalSpan - viewSpan);
    const double normalized = scrollableSpan > 0.0
        ? (plot_->xAxis->range().lower - dataMinTime_) / scrollableSpan
        : 0.0;
    timeScrollBar_->setValue(std::clamp(static_cast<int>(std::round(normalized * 10000.0)), 0, 10000));
    updatingTimeScrollBar_ = false;
}

void ScopeViewWindow::scrollToTimePosition(const int position)
{
    if (updatingTimeScrollBar_ || !hasTimeRange_ || fitAllTimeCheck_->isChecked()) {
        return;
    }
    const double viewSpan = std::min(plot_->xAxis->range().size(), dataMaxTime_ - dataMinTime_);
    const double scrollableSpan = std::max(0.0, dataMaxTime_ - dataMinTime_ - viewSpan);
    const double lower = dataMinTime_ + scrollableSpan * static_cast<double>(position) / 10000.0;
    plot_->xAxis->setRange(lower, lower + viewSpan);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeViewWindow::configurePlot()
{
    plot_->legend->setVisible(true);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend | QCP::iSelectPlottables);
    plot_->xAxis->setLabel(QStringLiteral("time (s)"));
    plot_->yAxis->setLabel(probeName_);

}

} // namespace s7studio::ui
