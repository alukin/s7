// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "scope_view_window.hpp"
#include "observation/run_metadata.hpp"
#include <QDialog>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QSet>
#include "observation/scope_results.hpp"

#include "viewers/line_plot_view.hpp"
#include "viewers/spectrum_view.hpp"
#include "viewers/pair_analysis_view.hpp"
#include "instrument_controls.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QtGraphs/QLineSeries>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace s7studio::ui {

namespace {

const QList<QColor> kSeriesColors{QColor("#1f77b4"), QColor("#d62728"), QColor("#2ca02c"),
                                  QColor("#9467bd"), QColor("#ff7f0e"), QColor("#17becf")};

auto styleFor(const ScopeRecording::Series& series, const qsizetype index) -> viewers::LinePlotView::SeriesStyle
{
    return {kSeriesColors[index % kSeriesColors.size()], series.isComparison() ? 1.2 : 1.6, series.isComparison()};
}

// Qt Graphs retains its own copy of every plotted point. Keep the presentation
// proportional to the physical plot width, while preserving peaks and gaps.
auto pointsForPlot(const QList<QPointF>& points, const double lower, const double upper,
                   const qsizetype pixelColumns) -> QList<QPointF>
{
    if (points.size() <= pixelColumns * 3 + 2) return points;
    bool timeOrdered = std::isfinite(lower) && std::isfinite(upper) && upper > lower;
    double previousTime = -std::numeric_limits<double>::infinity();
    for (const auto& point : points) {
        if (!std::isfinite(point.x())) continue;
        if (point.x() < previousTime) { timeOrdered = false; break; }
        previousTime = point.x();
    }
    struct Bucket { qsizetype low = -1, high = -1, gap = -1; };
    QVector<Bucket> buckets(pixelColumns);
    const qsizetype stride = (points.size() - 2 + pixelColumns - 1) / pixelColumns;
    qsizetype previousColumn = 0;
    for (qsizetype i = 1; i < points.size() - 1; ++i) {
        const auto& point = points[i];
        qsizetype column = (i - 1) / stride;
        if (timeOrdered && std::isfinite(point.x())) {
            const double fraction = (point.x() - lower) / (upper - lower);
            column = fraction <= 0.0 ? 0 : fraction >= 1.0 ? pixelColumns - 1
                : static_cast<qsizetype>(fraction * pixelColumns);
            previousColumn = column;
        } else if (timeOrdered) column = previousColumn;
        auto& bucket = buckets[column];
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            if (bucket.gap < 0) bucket.gap = i;
        } else {
            if (bucket.low < 0 || point.y() < points[bucket.low].y()) bucket.low = i;
            if (bucket.high < 0 || point.y() > points[bucket.high].y()) bucket.high = i;
        }
    }
    QList<QPointF> reduced;
    reduced.reserve(pixelColumns * 3 + 2);
    reduced.push_back(points.front());
    for (const auto& bucket : buckets) {
        qsizetype indices[]{bucket.low, bucket.high, bucket.gap};
        std::sort(std::begin(indices), std::end(indices));
        for (const auto index : indices)
            if (index >= 0 && (reduced.isEmpty() || reduced.back() != points[index]))
                reduced.push_back(points[index]);
    }
    reduced.push_back(points.back());
    return reduced;
}

auto plotPixelColumns(viewers::LinePlotView* plot) -> qsizetype
{
    const auto areaWidth = plot->bridge()->plotAreaSize.width() * plot->devicePixelRatioF();
    return std::isfinite(areaWidth) && areaWidth > 0
        ? std::clamp(static_cast<qsizetype>(std::ceil(areaWidth)), qsizetype(1), qsizetype(8192))
        : qsizetype(1364); // Before the QML plot area has been laid out.
}

} // namespace

struct ScopeViewWindow::AnalysisJob {
    std::jthread worker;
    std::mutex mutex;
    std::shared_ptr<observation::LiveFrame> frame;
    QString error;
    bool truncated = false;
    bool done = false;
    ~AnalysisJob() { worker.request_stop(); if (worker.joinable()) worker.join(); }
};

namespace {
struct AnalysisRow {
    double time = 0.0;
    std::vector<double> values;
};

auto analysisFrame(std::map<std::int64_t, AnalysisRow>& rows, const QVector<observation::LiveChannel>& channels,
                   const QJsonObject& provenance, const std::uint64_t runId,
                   const std::uint64_t generation, const std::uint64_t received, const bool finished)
    -> std::shared_ptr<observation::LiveFrame>
{
    auto frame = std::make_shared<observation::LiveFrame>();
    frame->runId = runId;
    frame->generation = generation;
    frame->received = received;
    frame->finished = finished;
    frame->channels = channels;
    frame->provenance = provenance;
    frame->dt = provenance.value(QStringLiteral("dt_seconds")).toDouble();
    const auto rate = provenance.value(QStringLiteral("rate")).toObject();
    frame->rateNumerator = rate.value(QStringLiteral("numerator")).toString().toULongLong();
    frame->rateDenominator = rate.value(QStringLiteral("denominator")).toString().toULongLong();
    frame->steps.reserve(rows.size());
    frame->times.reserve(rows.size());
    frame->values.reserve(rows.size()*static_cast<std::size_t>(channels.size()));
    for (auto& [step, row] : rows) {
        frame->steps.push_back(step);
        frame->times.push_back(row.time);
        frame->values.insert(frame->values.end(), row.values.begin(), row.values.end());
    }
    return frame;
}
} // namespace

void ScopeViewWindow::refreshAnalysisRuns()
{
    if (!analysisRun_) return;
    const auto previous = analysisRun_->currentData().toString();
    QStringList runs;
    bool live = static_cast<bool>(source());
    for (const auto& series : recording_.series()) {
        if (series.isComparison()) {
            if (!runs.contains(series.comparisonLabel)) runs.push_back(series.comparisonLabel);
        } else if (series.hasSamples) live = true;
    }
    const QSignalBlocker blocker(analysisRun_);
    analysisRun_->clear();
    if (live) analysisRun_->addItem(QStringLiteral("Current run"), QStringLiteral("current"));
    for (const auto& run : runs) analysisRun_->addItem(run, run);
    const auto index = analysisRun_->findData(previous);
    if (index >= 0) analysisRun_->setCurrentIndex(index);
    analysisRun_->setEnabled(analysisRun_->count() > 0);
    analyzeButton_->setEnabled(analysisRun_->count() > 0);
    const auto selected = analysisRun_->currentData().toString();
    if (selected != analysisIntervalRun_) {
        analysisIntervalRun_ = selected;
        analysisIntervalAuto_ = true;
    }
    updateAutoAnalysisInterval();
}

void ScopeViewWindow::updateAutoAnalysisInterval()
{
    if (!analysisIntervalAuto_ || analysisRun_->count() == 0) return;
    const auto selected = analysisRun_->currentData().toString();
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (const auto& series : recording_.series()) if (series.hasSamples &&
        ((selected == QStringLiteral("current") && !series.isComparison()) || series.comparisonLabel == selected)) {
        low = std::min(low, series.firstTime); high = std::max(high, series.lastTime);
    }
    if (low <= high) {
        const double start = selected == QStringLiteral("current") && low >= 0 ? 0.0 : low;
        ui::setCompactNumberValueSilently(analysisStart_, start);
        ui::setCompactNumberValueSilently(analysisEnd_, start + (high - start) / 4.0);
    }
}

void ScopeViewWindow::invalidateAnalysis()
{
    if (analysisJob_) { analysisJob_->worker.request_stop(); analysisJob_.reset(); }
    spectrum_->clear(); correlation_->clear(); trajectory_->clear();
    analysisStatus_->setText(QStringLiteral("Selection changed — press Analyze for a new snapshot."));
}

void ScopeViewWindow::analyzeInterval()
{
    if (!analysisRun_ || analysisRun_->currentIndex() < 0) return;
    autoInterval_->flush();  // analyze the interval the latest data implies
    analysisIntervalAuto_ = false;
    const double lower = analysisStart_->value(), upper = analysisEnd_->value();
    if (lower > upper) { analysisStatus_->setText(QStringLiteral("From must be no later than To.")); return; }
    invalidateAnalysis();
    const auto limit = std::clamp(QSettings{}.value(QStringLiteral("scope/analysisSampleLimit"), 16384).toInt(), 64, 65536);
    const auto selected = analysisRun_->currentData().toString();
    const auto generation = ++analysisSequence_;
    if (selected == QStringLiteral("current") && source()) {
        const auto journal = source();
        const auto selectedIds = selectedChannels();
        const auto allChannels = journal->channels();
        const auto provenance = journal->provenance();
        QVector<observation::LiveChannel> channels;
        QHash<observation::ChannelId, std::size_t> indices;
        for (const auto id : selectedIds) if (id < static_cast<observation::ChannelId>(allChannels.size()) && !indices.contains(id)) {
            const auto& channel = allChannels[static_cast<qsizetype>(id)];
            const auto separator = channel.name.indexOf(QLatin1Char('/'));
            channels.push_back({separator < 0 ? channel.name : channel.name.mid(separator+1),
                observation::probeUnit(provenance, channel.name), channel.sourceType});
            indices.insert(id, static_cast<std::size_t>(channels.size()-1));
        }
        if (channels.isEmpty()) { analysisStatus_->setText(QStringLiteral("No channels are bound to this Scope.")); return; }
        analysisJob_ = std::make_unique<AnalysisJob>();
        auto* job = analysisJob_.get();
        analysisStatus_->setText(QStringLiteral("Reading exact samples from the run…"));
        job->worker = std::jthread([job, journal, channels, indices, provenance, lower, upper, limit, generation](std::stop_token stop) {
            const auto status = journal->status();
            std::map<std::int64_t, AnalysisRow> rows;
            bool truncated = false;
            QString error;
            std::uint64_t cursor = 0;
            while (cursor < status.samples && !stop.stop_requested()) {
                const auto page = journal->read(cursor, static_cast<qsizetype>(std::min<std::uint64_t>(4096, status.samples-cursor)));
                if (!page.error.isEmpty() || page.next <= cursor) { error = page.error.isEmpty() ? QStringLiteral("Run read stalled") : page.error; break; }
                for (const auto& sample : page.samples) {
                    const auto index = indices.constFind(sample.channel);
                    if (index == indices.cend() || sample.timeSeconds < lower || sample.timeSeconds > upper) continue;
                    auto [it, inserted] = rows.try_emplace(sample.step);
                    if (inserted) { it->second.time = sample.timeSeconds;
                        it->second.values.assign(static_cast<std::size_t>(channels.size()), std::numeric_limits<double>::quiet_NaN()); }
                    else {
                        const double dt = provenance.value(QStringLiteral("dt_seconds")).toDouble();
                        const double tolerance = std::max(std::abs(dt)*1e-8,
                            32*std::numeric_limits<double>::epsilon()*std::abs(sample.timeSeconds));
                        if (!std::isfinite(sample.timeSeconds) || std::abs(it->second.time-sample.timeSeconds) > tolerance) {
                            error = QStringLiteral("Channels report different model times for the same logical step.");
                            break;
                        }
                    }
                    it->second.values[*index] = sample.value;
                    while (rows.size() > static_cast<std::size_t>(limit)) { rows.erase(rows.begin()); truncated = true; }
                }
                if (!error.isEmpty()) break;
                cursor = page.next;
            }
            if (stop.stop_requested()) return;
            auto frame = analysisFrame(rows, channels, provenance, journal->runId(), generation, status.samples, status.finished);
            std::lock_guard lock(job->mutex);
            job->frame = std::move(frame);
            job->error = std::move(error);
            job->truncated = truncated;
            job->done = true;
        });
        return;
    }
    QJsonObject metadata;
    QVector<observation::LiveChannel> channels;
    QVector<QString> keys;
    bool incompleteRecording = false;
    for (auto it = recording_.series().cbegin(); it != recording_.series().cend(); ++it) {
        if (!it->hasSamples || (selected == QStringLiteral("current") ? it->isComparison() : it->comparisonLabel != selected)) continue;
        if (keys.isEmpty()) metadata = it->isComparison() ? it->sourceMetadata : recording_.runMetadata();
        incompleteRecording |= it->spillFailed || !it->recordingWarning.isEmpty();
        channels.push_back({it->channelName, it->channelUnit, QStringLiteral("f64")});
        keys.push_back(it.key());
    }
    if (keys.isEmpty()) { analysisStatus_->setText(QStringLiteral("The selected run has no samples.")); return; }
    const auto provenance = metadata.value(QStringLiteral("provenance")).toObject();
    incompleteRecording |= metadata.value(QStringLiteral("incomplete")).toBool();
    if (selected == QStringLiteral("current")) incompleteRecording |= recording_.droppedSampleCount() > 0;
    const double dt = provenance.value(QStringLiteral("dt_seconds")).toDouble();
    if (!(dt > 0) || !std::isfinite(dt)) {
        analysisStatus_->setText(QStringLiteral("Analysis needs the run's recorded dt and rational sample rate.")); return;
    }
    std::map<std::int64_t, AnalysisRow> rows;
    bool truncated = false;
    QString error;
    for (qsizetype column = 0; column < keys.size(); ++column) {
        bool columnTruncated = false;
        const auto samples = recording_.analysisSamples(keys[column], lower, upper, limit, &columnTruncated, &error);
        if (!error.isEmpty()) { analysisStatus_->setText(error); return; }
        truncated |= columnTruncated;
        for (const auto& sample : samples) {
            const long double rawStep = static_cast<long double>(sample.x())/dt;
            if (!std::isfinite(static_cast<double>(rawStep)) || std::abs(rawStep) > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
                analysisStatus_->setText(QStringLiteral("Model time is outside the supported step range.")); return;
            }
            const auto step = static_cast<std::int64_t>(std::llround(rawStep));
            const auto reconstructed = static_cast<long double>(step)*dt;
            const auto tolerance = std::max(1e-8L*static_cast<long double>(dt),
                32*static_cast<long double>(std::numeric_limits<double>::epsilon())*std::abs(static_cast<long double>(sample.x())));
            if (std::abs(reconstructed-static_cast<long double>(sample.x())) > tolerance) {
                analysisStatus_->setText(QStringLiteral("Sample times do not align with the run's dt; analysis will not resample them.")); return;
            }
            auto [it, inserted] = rows.try_emplace(step);
            if (inserted) { it->second.time = sample.x();
                it->second.values.assign(static_cast<std::size_t>(channels.size()), std::numeric_limits<double>::quiet_NaN()); }
            it->second.values[static_cast<std::size_t>(column)] = sample.y();
            while (rows.size() > static_cast<std::size_t>(limit)) { rows.erase(rows.begin()); truncated = true; }
        }
    }
    const auto runId = metadata.value(QStringLiteral("run_id")).toString().toULongLong();
    const auto frame = analysisFrame(rows, channels, provenance, runId,
        generation, static_cast<std::uint64_t>(rows.size()), metadata.value(QStringLiteral("finished")).toBool());
    if (frame->steps.empty()) { analysisStatus_->setText(QStringLiteral("No samples in the selected interval.")); return; }
    spectrum_->setFrame(frame); correlation_->setFrame(frame); trajectory_->setFrame(frame);
    if (frame->steps.size() >= 64) { spectrum_->setAnalysisSize(static_cast<int>(frame->steps.size()));
        correlation_->setAnalysisSize(static_cast<int>(frame->steps.size())); }
    analysisStatus_->setText(QStringLiteral("%1 exact positions • t=%2…%3 s • dt %4 s • %5%6")
        .arg(frame->steps.size()).arg(frame->times.front(), 0, 'g', 8)
        .arg(frame->times.back(), 0, 'g', 8).arg(dt, 0, 'g', 8)
        .arg(truncated ? QStringLiteral("newest samples within configured cap") : QStringLiteral("full selected interval"))
        .arg(incompleteRecording ? QStringLiteral(" • recording incomplete") : QString()));
}

void ScopeViewWindow::pollAnalysis()
{
    if (!analysisJob_) return;
    std::shared_ptr<observation::LiveFrame> frame;
    QString error;
    bool truncated = false;
    {
        std::lock_guard lock(analysisJob_->mutex);
        if (!analysisJob_->done) return;
        frame = std::move(analysisJob_->frame);
        error = std::move(analysisJob_->error);
        truncated = analysisJob_->truncated;
    }
    analysisJob_.reset();
    if (!error.isEmpty()) { analysisStatus_->setText(error); return; }
    if (!frame || frame->steps.empty()) { analysisStatus_->setText(QStringLiteral("No samples in the selected interval.")); return; }
    spectrum_->setFrame(frame); correlation_->setFrame(frame); trajectory_->setFrame(frame);
    if (frame->steps.size() >= 64) { spectrum_->setAnalysisSize(static_cast<int>(frame->steps.size()));
        correlation_->setAnalysisSize(static_cast<int>(frame->steps.size())); }
    analysisStatus_->setText(QStringLiteral("%1 exact positions • t=%2…%3 s • dt %4 s • %5%6")
        .arg(frame->steps.size()).arg(frame->times.front(), 0, 'g', 8)
        .arg(frame->times.back(), 0, 'g', 8).arg(frame->dt, 0, 'g', 8)
        .arg(truncated ? QStringLiteral("newest samples within configured cap") : QStringLiteral("full selected interval"))
        .arg(source() && source()->status().incomplete() ? QStringLiteral(" • recording incomplete") : QString()));
}

auto ScopeViewWindow::viewerType() -> viewers::ViewerType
{
    return {QStringLiteral("Scope"), {}, QStringLiteral("N→plot"), {}, {},
            [](const viewers::ViewerInstanceSpec& spec) -> viewers::ObservationViewer* {
                return new ScopeViewWindow(spec.instanceId, nullptr,
                    Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
            }};
}

ScopeViewWindow::ScopeViewWindow(QString probeName,
                                 QWidget* parent,
                                 const Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , probeName_(std::move(probeName))
    , recording_(probeName_)
{
    autoInterval_ = new Throttle([this] { updateAutoAnalysisInterval(); }, this);
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(900, 360);
    setWindowTitle(QStringLiteral("Scope - %1").arg(probeName_));
    resize(900, 520);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 4, 6, 4);
    rootLayout->setSpacing(3);
    auto* controls = new QHBoxLayout();
    controls->setSpacing(4);

    autoScaleYCheck_ = new QCheckBox(QStringLiteral("Auto Y"), this);
    autoScaleYCheck_->setChecked(true);
    autoScaleYCheck_->setToolTip(QStringLiteral("Keep the Time plot's Y axis fitted to displayed samples."));
    controls->addWidget(autoScaleYCheck_);
    fitAllTimeCheck_ = new QCheckBox(QStringLiteral("Fit full run"), this);
    fitAllTimeCheck_->setChecked(true);
    fitAllTimeCheck_->setToolTip(QStringLiteral("Keep the full recorded time span visible as samples arrive."));
    controls->addWidget(fitAllTimeCheck_);
    controls->addSpacing(8);
    clearButton_ = new QPushButton(this);
    ui::compactActionButton(clearButton_, QStyle::SP_DialogResetButton, QStringLiteral("Clear display"),
        QStringLiteral("Captured data remains available for saving, export and reopening."));
    exportButton_ = new QPushButton(this);
    ui::compactActionButton(exportButton_, QStyle::SP_ArrowDown, QStringLiteral("Export full CSV"),
        QStringLiteral("Export all recorded samples as CSV."));
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addSpacing(8);
    auto* saveButton = new QPushButton(this);
    auto* openButton = new QPushButton(this);
    auto* removeButton = new QPushButton(this);
    auto* detailsButton = new QPushButton(this);
    detailsButton->setObjectName(QStringLiteral("scope.runDetails"));
    saveButton->setObjectName(QStringLiteral("scope.saveRun"));
    openButton->setObjectName(QStringLiteral("scope.openRun"));
    removeButton->setObjectName(QStringLiteral("scope.removeComparisons"));
    ui::compactActionButton(saveButton, QStyle::SP_DialogSaveButton, QStringLiteral("Save run"),
        QStringLiteral("Save the current recording with capture warnings. Comparisons are saved separately."));
    ui::compactActionButton(openButton, QStyle::SP_DialogOpenButton, QStringLiteral("Open run"),
        QStringLiteral("Overlay saved results at their original model timestamps."));
    ui::compactActionButton(removeButton, QStyle::SP_TrashIcon, QStringLiteral("Remove comparisons"),
        QStringLiteral("Remove saved and archived comparison traces from this Scope."));
    ui::compactActionButton(detailsButton, QStyle::SP_MessageBoxInformation, QStringLiteral("Run details"),
        QStringLiteral("Show model and provenance details for recorded runs."));
    for (auto* button : {saveButton, openButton, removeButton, detailsButton}) controls->addWidget(button);
    connect(detailsButton, &QPushButton::clicked, this, &ScopeViewWindow::showRunDetails);
    connect(saveButton, &QPushButton::clicked, this, &ScopeViewWindow::saveRunResults);
    connect(openButton, &QPushButton::clicked, this, &ScopeViewWindow::openRunResultsFile);
    connect(removeButton, &QPushButton::clicked, this, &ScopeViewWindow::clearComparisons);
    rootLayout->addLayout(controls);

    plot_ = new viewers::LinePlotView(this);
    plot_->setLegendVisible(true);
    plot_->setAxisTitles(QStringLiteral("model time (s)"), QStringLiteral("value (unit unspecified)"));
    plot_->bridge()->inspectionEnabled = true;
    plot_->bridge()->hoverTipEnabled = false;
    emit plot_->bridge()->inspectionChanged();
    plot_->setToolTip(QStringLiteral("Hover over a trace for stored sample values. Zoom with the mouse wheel and pan by dragging. Keyboard: +/− zoom, arrows pan, Home fits all data."));
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("scope.analysisTabs"));
    auto* timePage = new QWidget(tabs_);
    auto* timeLayout = new QVBoxLayout(timePage);
    timeLayout->setContentsMargins(0, 0, 0, 0);
    timeLayout->addWidget(plot_, 1);
    timeScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    timeScrollBar_->setRange(0, 10000);
    timeScrollBar_->setEnabled(false);
    timeScrollBar_->setToolTip(QStringLiteral("Scroll the visible time range. Zoom with the mouse wheel and pan by dragging. Keyboard: +/− zoom, arrows pan, Home fits all data."));
    timeLayout->addWidget(timeScrollBar_);
    tabs_->addTab(timePage, QStringLiteral("Time"));
    spectrum_ = new viewers::SpectrumView(tabs_);
    correlation_ = new viewers::PairAnalysisView(analysis::PairKind::Correlation, tabs_);
    trajectory_ = new viewers::PairAnalysisView(analysis::PairKind::Constellation, tabs_);
    spectrum_->setSnapshotMode(true);
    correlation_->setSnapshotMode(true);
    trajectory_->setSnapshotMode(true);
    spectrum_->controlPanel()->setReadoutLines(4);
    correlation_->controlPanel()->setReadoutLines(4);
    trajectory_->controlPanel()->setReadoutLines(4);
    tabs_->addTab(spectrum_, QStringLiteral("Spectrum"));
    tabs_->addTab(correlation_, QStringLiteral("Correlation"));
    tabs_->addTab(trajectory_, QStringLiteral("XY Trajectory"));
    auto* showAnalysisControls = new QToolButton(tabs_);
    showAnalysisControls->setObjectName(QStringLiteral("scope.toggleAnalysisControls"));
    showAnalysisControls->setText(QStringLiteral("Controls"));
    showAnalysisControls->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    showAnalysisControls->setCheckable(true);
    showAnalysisControls->setChecked(true);
    showAnalysisControls->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    showAnalysisControls->setIconSize({16, 16});
    showAnalysisControls->setToolTip(QStringLiteral("Show or hide analysis settings to expand the plot."));
    showAnalysisControls->setAccessibleName(QStringLiteral("Hide analysis controls"));
    tabs_->setCornerWidget(showAnalysisControls, Qt::TopRightCorner);
    connect(showAnalysisControls, &QToolButton::toggled, this, [this, showAnalysisControls](const bool shown) {
        for (auto* sidebar : {spectrum_->controlPanel(), correlation_->controlPanel(), trajectory_->controlPanel()})
            sidebar->setVisible(shown);
        showAnalysisControls->setIcon(style()->standardIcon(shown ? QStyle::SP_ArrowDown : QStyle::SP_ArrowRight));
        showAnalysisControls->setAccessibleName(shown ? QStringLiteral("Hide analysis controls") : QStringLiteral("Show analysis controls"));
    });
    connect(tabs_, &QTabWidget::currentChanged, showAnalysisControls,
        [showAnalysisControls](const int index) { showAnalysisControls->setVisible(index != 0); });
    showAnalysisControls->hide();

    controls->addSpacing(8);
    analysisRun_ = new QComboBox(this);
    analysisRun_->setObjectName(QStringLiteral("scope.analysisRun"));
    analysisRun_->setFixedWidth(140);
    analysisRun_->setToolTip(QStringLiteral("Select the current run or a saved comparison for analysis."));
    analysisStart_ = ui::compactNumber(this);
    analysisEnd_ = ui::compactNumber(this);
    for (auto* spin : {analysisStart_, analysisEnd_}) {
        spin->setRange(-1e12, 1e12);
        spin->setSuffix(QStringLiteral(" s"));
    }
    analysisStart_->setObjectName(QStringLiteral("scope.analysisStart"));
    analysisEnd_->setObjectName(QStringLiteral("scope.analysisEnd"));
    analysisStart_->setToolTip(QStringLiteral("First model time included in the analysis interval."));
    analysisEnd_->setToolTip(QStringLiteral("Last model time included in the analysis interval."));
    auto* visibleButton = new QPushButton(this);
    auto* fullButton = new QPushButton(this);
    visibleButton->setObjectName(QStringLiteral("scope.visibleAnalysisRange"));
    fullButton->setObjectName(QStringLiteral("scope.fullAnalysisRange"));
    ui::compactActionButton(visibleButton, QStyle::SP_TitleBarNormalButton, QStringLiteral("Use visible time"),
        QStringLiteral("Copy the current Time plot range into the analysis interval."));
    ui::compactActionButton(fullButton, QStyle::SP_TitleBarMaxButton, QStringLiteral("Use full run"),
        QStringLiteral("Set the analysis interval to the selected run's full time range."));
    analyzeButton_ = new QPushButton(this);
    analyzeButton_->setObjectName(QStringLiteral("scope.analyze"));
    ui::compactActionButton(analyzeButton_, QStyle::SP_MediaPlay, QStringLiteral("Analyze"),
        QStringLiteral("Analyze exact recorded samples in the selected run and time interval."));
    controls->addWidget(new QLabel(QStringLiteral("Run"), this));
    controls->addWidget(analysisRun_);
    controls->addWidget(new QLabel(QStringLiteral("From"), this));
    controls->addWidget(analysisStart_);
    controls->addWidget(new QLabel(QStringLiteral("To"), this));
    controls->addWidget(analysisEnd_);
    controls->addWidget(visibleButton);
    controls->addWidget(fullButton);
    controls->addWidget(analyzeButton_);
    warningButton_ = new QToolButton(this);
    warningButton_->setObjectName(QStringLiteral("scope.statusWarning"));
    warningButton_->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    warningButton_->setFixedSize(30, 28);
    warningButton_->setAccessibleName(QStringLiteral("Scope recording warning"));
    warningButton_->hide();
    controls->addWidget(warningButton_);
    controls->addStretch(1);
    analysisStatus_ = new QLabel(QStringLiteral("Select a run and interval, then Analyze."), this);
    analysisStatus_->setObjectName(QStringLiteral("scope.analysisStatus"));
    analysisStatus_->setWordWrap(true);
    rootLayout->addWidget(tabs_, 1);
    auto* readoutRow = new QHBoxLayout();
    readoutRow->setSpacing(8);
    readoutRow->addWidget(analysisStatus_, 1);
    hoverReadout_ = new QLabel(this);
    hoverReadout_->setObjectName(QStringLiteral("scope.hoverReadout"));
    hoverReadout_->setTextFormat(Qt::PlainText);
    hoverReadout_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hoverReadout_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    readoutRow->addWidget(hoverReadout_, 1);
    rootLayout->addLayout(readoutRow);
    connect(analyzeButton_, &QPushButton::clicked, this, &ScopeViewWindow::analyzeInterval);
    connect(visibleButton, &QPushButton::clicked, this, [this] {
        analysisIntervalAuto_ = false;
        analysisStart_->setValue(plot_->xMin()); analysisEnd_->setValue(plot_->xMax());
    });
    connect(fullButton, &QPushButton::clicked, this, [this] {
        analysisIntervalAuto_ = false;
        const auto selected = analysisRun_->currentData().toString();
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const auto& series : recording_.series()) if (series.hasSamples &&
            ((selected == QStringLiteral("current") && !series.isComparison()) || series.comparisonLabel == selected)) {
            low = std::min(low, series.firstTime); high = std::max(high, series.lastTime);
        }
        if (selected == QStringLiteral("current") && source()) {
            const auto selectedIds = selectedChannels();
            const auto first = source()->read(0, 4096);
            for (const auto& sample : first.samples) if (selectedIds.contains(sample.channel))
                low = std::min(low, sample.timeSeconds);
            const auto last = source()->tail(selectedIds, 1);
            for (const auto& channel : last.channels) for (const auto& sample : channel.samples)
                high = std::max(high, sample.timeSeconds);
        }
        if (low <= high) { analysisStart_->setValue(low); analysisEnd_->setValue(high); }
    });
    connect(analysisRun_, &QComboBox::currentIndexChanged, this, [this] { refreshAnalysisRuns(); invalidateAnalysis(); });
    connect(analysisStart_, &QDoubleSpinBox::valueChanged, this, [this] { analysisIntervalAuto_ = false; invalidateAnalysis(); });
    connect(analysisEnd_, &QDoubleSpinBox::valueChanged, this, [this] { analysisIntervalAuto_ = false; invalidateAnalysis(); });
    auto* analysisPoll = new QTimer(this);
    analysisPoll->setInterval(40);
    connect(analysisPoll, &QTimer::timeout, this, &ScopeViewWindow::pollAnalysis);
    analysisPoll->start();
    refreshAnalysisRuns();

    recording_.setStatusChangedHandler([this] { updateStatus(); });

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    // A hidden Scope keeps recording; its plot is rebuilt when it is shown.
    connect(repaintTimer_, &QTimer::timeout, this, [this] {
        if (isVisible()) refreshPlot();
    });
    repaintTimer_->start();

    connect(clearButton_, &QPushButton::clicked, this, &ScopeViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &ScopeViewWindow::exportRecordedCsv);
    connect(timeScrollBar_, &QScrollBar::valueChanged, this, &ScopeViewWindow::scrollToTimePosition);
    connect(fitAllTimeCheck_, &QCheckBox::toggled, this, [this](const bool fitAll) {
        if (fitAll && hasTimeRange_) {
            plot_->setXRange(dataMinTime_, dataMaxTime_ > dataMinTime_ ? dataMaxTime_ : dataMinTime_ + 1.0);
        }
        updateTimeScrollBar();
        setVisibleData();
    });
    connect(plot_, &viewers::LinePlotView::xRangeChanged, this, &ScopeViewWindow::onXRangeChanged);
    connect(plot_, &viewers::LinePlotView::hoveredPoint, this, &ScopeViewWindow::updateHover);
    connect(plot_->bridge(), &viewers::LinePlotBridge::plotAreaChanged, this, [this] {
        if (hasTimeRange_ && plotPixelColumns(plot_) != lastPlotPixelColumns_) setVisibleData();
    });
}

ScopeViewWindow::~ScopeViewWindow()
{
    if (analysisJob_) { analysisJob_->worker.request_stop(); analysisJob_.reset(); }
    // The handler refers to this window's widgets.
    recording_.setStatusChangedHandler({});
}

bool ScopeViewWindow::refreshSource()
{
    if (source() && analysisSourceRunId_ != source()->runId()) {
        analysisSourceRunId_ = source()->runId();
        if (analysisRun_->currentData().toString() == QStringLiteral("current")) analysisIntervalAuto_ = true;
    }
    const bool more = viewers::ObservationViewer::refreshSource();
    autoInterval_->request();
    updateStatus();
    return more;
}

void ScopeViewWindow::showEvent(QShowEvent* event)
{
    updateAutoAnalysisInterval();
    refreshPlot();
    QWidget::showEvent(event);
}

auto ScopeViewWindow::probeName() const -> QString
{
    return probeName_;
}

void ScopeViewWindow::appendObservedSample(const viewers::ObservedSample& sample)
{
    const bool changed = recording_.setChannelUnit(sample.role, sample.unit);
    appendSample(sample.role, sample.timeSeconds, sample.value);
    if (changed) syncSeries(false);
}

void ScopeViewWindow::appendSample(const double timeSeconds, const double value)
{
    appendSample(probeName_, timeSeconds, value);
}

void ScopeViewWindow::appendSample(const QString& channel, const double timeSeconds, const double value)
{
    const auto known = lines_.size();
    recording_.appendSample(channel, timeSeconds, value);
    if (recording_.series().size() != known) syncSeries(false);
    dirty_ = true;
}

void ScopeViewWindow::syncSeries(const bool restyle)
{
    if (restyle) updateHover(0, 0, false);
    const auto& series = recording_.series();
    for (auto it = lines_.begin(); it != lines_.end();) {
        if (restyle || !series.contains(it.key())) {
            plot_->removeSeries(it.value());
            it = lines_.erase(it);
        } else {
            ++it;
        }
    }
    // New series get the next color; a rebuild colors in drawing order.
    for (auto it = series.cbegin(); it != series.cend(); ++it) {
        if (lines_.contains(it.key())) { plot_->setSeriesName(lines_.value(it.key()), it->displayName()); continue; }
        lines_.insert(it.key(), plot_->addSeries(it->displayName(), styleFor(it.value(), lines_.size())));
    }
    QSet<QString> units;
    bool unknown = false;
    for (const auto& entry : series) { if (entry.channelUnit.isEmpty()) unknown = true; else units.insert(entry.channelUnit); }
    const auto yTitle = units.size() == 1 && !unknown ? QStringLiteral("value (%1)").arg(*units.begin())
        : units.isEmpty() ? QStringLiteral("value (unit unspecified)") : QStringLiteral("value (mixed units; see legend)");
    plot_->setAxisTitles(QStringLiteral("model time (s)"), yTitle);
    hasTimeRange_ = false;
    dirty_ = true;
    refreshAnalysisRuns();
}

auto ScopeViewWindow::plottedSeries(const QString& key) const -> QLineSeries*
{
    return lines_.value(key);
}

void ScopeViewWindow::clearSamples()
{
    clearBuffers();
}

void ScopeViewWindow::beginRun(const QString& previousRunLabel, bool preserve)
{
    if (!preserve) { clearSamples(); return; }
    if (source()) { clearSamples(); return; } // retained run sources are projected independently
    recording_.archiveLive(previousRunLabel);
    syncSeries(true);
    dirty_ = true;
}

void ScopeViewWindow::setComparisonSources(const QVector<viewers::ComparisonBinding>& comparisons)
{
    for (const auto& comparison : comparisons) {
        if (projectedComparisons_.contains(comparison.source)) continue;
        QVector<observation::ChannelId> channels;
        for (const auto& input : comparison.inputs) channels.push_back(input.channel);
        if (channels.isEmpty()) continue;
        projectedComparisons_.push_back(comparison.source); // one attempt per window; reopening can retry a storage failure
        QString error;
        auto projection = observation::scopeProjection(*comparison.source, probeName_, channels, &error);
        if (!projection) { recording_.setCaptureError(error); continue; }
        recording_.appendComparison(*projection, comparison.label);
        syncSeries(false);
    }
}

void ScopeViewWindow::setDroppedSampleCount(const std::uint64_t dropped)
{
    recording_.setDroppedSampleCount(dropped);
}

void ScopeViewWindow::updateStatus()
{
    const auto cacheStatus = recording_.statusText();
    const bool incomplete = recording_.exportMayBeIncomplete() ||
        (source() && source()->status().incomplete()) || previewDroppedCount() > 0;
    warningButton_->setVisible(incomplete);
    warningButton_->setToolTip(sourceStatusText() + (cacheStatus.isEmpty() ? QString()
        : QStringLiteral("\nDisplay cache: ") + cacheStatus));
}

void ScopeViewWindow::clearBuffers()
{
    invalidateAnalysis();
    discardPresentationHistory();
    recording_.clearLive();
    syncSeries(true);
    timeScrollBar_->setEnabled(false);
    dirty_ = true;
}

void ScopeViewWindow::clearComparisons()
{
    invalidateAnalysis();
    recording_.clearComparisons();
    syncSeries(true);
    refreshPlot();
}

void ScopeViewWindow::updateHover(const double timeSeconds, const double value, const bool active)
{
    constexpr double tolerance = 10.0; // physical pointer pixels
    auto* bridge = plot_->bridge();
    const auto area = bridge->plotAreaSize;
    const double sx = area.width() / (plot_->xMax() - plot_->xMin());
    const double sy = area.height() / (plot_->yMax() - plot_->yMin());
    QVariantList rows, markers;
    double bestDistance = std::numeric_limits<double>::infinity();
    double bestTime = 0.0;
    if (active && area.width() > 0 && area.height() > 0 && std::isfinite(sx) && std::isfinite(sy)) {
        for (auto it = recording_.series().cbegin(); it != recording_.series().cend(); ++it) {
            auto* line = lines_.value(it.key());
            if (!line || line->count() == 0) continue;
            const auto nearby = recording_.surroundingSamples(it.key(), timeSeconds);
            const auto pixel = [&](const QPointF& sample) {
                return QPointF((sample.x() - timeSeconds) * sx, (sample.y() - value) * sy);
            };
            double distance = std::numeric_limits<double>::infinity();
            std::optional<QPointF> sample;
            for (const auto& point : {nearby.before, nearby.after}) {
                if (!point || !std::isfinite(point->y())) continue;
                const auto p = pixel(*point);
                if (const double d = std::hypot(p.x(), p.y()); d < distance) {
                    distance = d;
                    sample = *point;
                }
            }
            if (nearby.before && nearby.after && std::isfinite(nearby.before->y())
                && std::isfinite(nearby.after->y())) {
                const auto p = pixel(*nearby.before), q = pixel(*nearby.after);
                const auto delta = q - p;
                const double length = QPointF::dotProduct(delta, delta);
                const double fraction = length > 0
                    ? std::clamp(-QPointF::dotProduct(p, delta) / length, 0.0, 1.0) : 0.0;
                const double segmentDistance = std::hypot(p.x() + fraction * delta.x(),
                                                           p.y() + fraction * delta.y());
                if (segmentDistance < distance) {
                    distance = segmentDistance;
                    sample = fraction < .5 ? nearby.before : nearby.after;
                }
            }
            if (!sample || distance > tolerance) continue;
            const QString unit = it->channelUnit.isEmpty() ? QString() : QStringLiteral(" %1").arg(it->channelUnit);
            const QString label = it->comparisonLabel.isEmpty() ? it->channelName
                : it->comparisonLabel + QStringLiteral(" / ") + it->channelName;
            rows.append(QVariantMap{{"text", QStringLiteral("%1 = %2%3 • %4 s")
                .arg(label).arg(sample->y(), 0, 'g', 8).arg(unit).arg(sample->x(), 0, 'g', 8)},
                {"color", line->color()}});
            markers.append(QVariantMap{{"x", sample->x()}, {"y", sample->y()}, {"color", line->color()}});
            if (distance < bestDistance) { bestDistance = distance; bestTime = sample->x(); }
        }
    }
    if (rows != bridge->hoverTip || markers != bridge->hoverMarkers) {
        bridge->hoverTip = rows;
        bridge->hoverMarkers = markers;
        emit bridge->hoverTipChanged();
    }
    QStringList values;
    for (const auto& row : rows) values.push_back(row.toMap().value(QStringLiteral("text")).toString());
    const auto readout = values.join(QStringLiteral("  |  "));
    hoverReadout_->setText(readout);
    hoverReadout_->setToolTip(readout);
    const bool cursorVisible = !rows.isEmpty();
    if (bridge->cursorVisible != cursorVisible || (cursorVisible && bridge->cursorX != bestTime)) {
        bridge->cursorVisible = cursorVisible;
        if (cursorVisible) bridge->cursorX = bestTime;
        emit bridge->inspectionChanged();
    }
}

void ScopeViewWindow::showRunDetails()
{
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("scope.runDetailsDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Run details — %1").arg(probeName_));
    dialog->resize(860, 600);
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(QStringLiteral("Snapshot taken when opened. Reopen Run details to refresh recording status."), dialog));
    auto* runs = new QComboBox(dialog);
    runs->setObjectName(QStringLiteral("scope.detailsRuns"));
    QVector<QJsonObject> metadata;
    if (source()) {
        const auto status = source()->status();
        metadata.push_back({{"run_id", QString::number(source()->runId())}, {"finished", status.finished},
            {"stopped", status.stopped}, {"incomplete", status.incomplete()},
            {"storage_error", status.storageError}, {"run_error", status.runError}, {"provenance", source()->provenance()}});
        runs->addItem(QStringLiteral("Current run"));
    } else if (!recording_.runMetadata().isEmpty()) {
        metadata.push_back(recording_.runMetadata());
        runs->addItem(QStringLiteral("Current run"));
    }
    QSet<QString> labels;
    for (const auto& series : recording_.series()) {
        if (!series.isComparison() || labels.contains(series.comparisonLabel)) continue;
        labels.insert(series.comparisonLabel);
        auto data = series.sourceMetadata;
        data["incomplete"] = data.value("incomplete").toBool() || !series.recordingWarning.isEmpty();
        metadata.push_back(data);
        runs->addItem(series.comparisonLabel);
    }
    if (metadata.isEmpty()) { metadata.push_back({}); runs->addItem(QStringLiteral("No recorded run")); }
    layout->addWidget(runs);
    auto* tabs = new QTabWidget(dialog);
    auto* summary = new QPlainTextEdit(tabs);
    auto* effective = new QPlainTextEdit(tabs);
    auto* requested = new QPlainTextEdit(tabs);
    summary->setObjectName(QStringLiteral("scope.detailsSummary"));
    effective->setObjectName(QStringLiteral("scope.detailsEffectiveModel"));
    requested->setObjectName(QStringLiteral("scope.detailsRequestedModel"));
    for (auto* text : {summary, effective, requested}) text->setReadOnly(true);
    tabs->addTab(summary, QStringLiteral("Summary"));
    tabs->addTab(effective, QStringLiteral("Effective model"));
    tabs->addTab(requested, QStringLiteral("Requested model"));
    layout->addWidget(tabs);
    const auto select = [metadata, summary, effective, requested](int index) {
        const auto& data = metadata[index];
        const auto p = data.value("provenance").toObject();
        summary->setPlainText(observation::runMetadataSummary(data));
        effective->setPlainText(p.value("effective_model_json").toString());
        requested->setPlainText(p.value("requested_model_json").toString());
    };
    connect(runs, &QComboBox::currentIndexChanged, dialog, select);
    select(0);
    dialog->show();
}

void ScopeViewWindow::exportRecordedCsv()
{
    if (recording_.exportMayBeIncomplete()) {
        QMessageBox::warning(this, QStringLiteral("Export may be incomplete"),
            QStringLiteral("Capture loss or a storage failure affected this recording or an imported run. "
                           "CSV contains only the retained samples. Save Run preserves the current recording's "
                           "capture-loss metadata; CSV does not."));
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

    QString error;
    if (!writeRecordedCsv(file, &error)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), error);
    }
}

bool ScopeViewWindow::writeRecordedCsv(QIODevice& device, QString* error) const
{
    if (!source()) return recording_.writeRecordedCsv(device, error);
    auto projection = observation::scopeProjection(*source(), probeName_, selectedChannels(), error);
    if (!projection) return false;
    projection->copyComparisonsFrom(recording_);
    return projection->writeRecordedCsv(device, error);
}

bool ScopeViewWindow::writeRunResults(QIODevice& device, QString* error) const
{
    if (!source()) return recording_.writeRunResults(device, error);
    auto projection = observation::scopeProjection(*source(), probeName_, selectedChannels(), error);
    return projection && projection->writeRunResults(device, error);
}

bool ScopeViewWindow::openRunResults(QIODevice& device, const QString& label, QString* error)
{
    if (!recording_.openRunResults(device, label, error)) return false;
    syncSeries(false);
    refreshPlot();
    return true;
}

void ScopeViewWindow::saveRunResults()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save current Scope run"),
        probeName_ + QStringLiteral(".s7scope"), QStringLiteral("S7 Scope results (*.s7scope)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    QString error;
    if (!file.open(QIODevice::WriteOnly)) error = file.errorString();
    else if (writeRunResults(file, &error) && !file.commit()) error = file.errorString();
    if (!error.isEmpty()) QMessageBox::critical(this, QStringLiteral("Save run failed"), error);
}

void ScopeViewWindow::openRunResultsFile()
{
    const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Open runs for comparison"), {},
        QStringLiteral("S7 Scope results (*.s7scope)"));
    for (const auto& path : paths) {
        QFile file(path);
        QString error;
        if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
        else static_cast<void>(openRunResults(file, QFileInfo(path).completeBaseName(), &error));
        if (!error.isEmpty()) QMessageBox::critical(this, QStringLiteral("Open run failed"), error);
    }
}

void ScopeViewWindow::setVisibleData()
{
    const bool fitAll = fitAllTimeCheck_->isChecked();
    const auto pixelColumns = plotPixelColumns(plot_);
    lastPlotPixelColumns_ = pixelColumns;
    for (auto it = lines_.cbegin(); it != lines_.cend(); ++it) {
        viewers::LinePlotView::setPoints(it.value(),
            pointsForPlot(recording_.visiblePoints(it.key(), plot_->xMin(), plot_->xMax(), fitAll),
                plot_->xMin(), plot_->xMax(), pixelColumns));
    }
}

void ScopeViewWindow::onXRangeChanged()
{
    updateTimeScrollBar();
    if (!fitAllTimeCheck_->isChecked() && hasTimeRange_) {
        setVisibleData();
    }
}

void ScopeViewWindow::refreshPlot()
{
    if (!dirty_) {
        return;
    }

    dirty_ = false;
    const auto& series = recording_.series();
    for (auto it = lines_.cbegin(); it != lines_.cend(); ++it) {
        if (const auto found = series.constFind(it.key()); found != series.cend()) {
            plot_->setSeriesName(it.value(), found->displayName());
        }
    }
    double minTime = 0.0;
    double maxTime = 0.0;
    if (recording_.timeRange(minTime, maxTime)) {
        dataMinTime_ = minTime;
        dataMaxTime_ = maxTime;
        hasTimeRange_ = true;
        if (fitAllTimeCheck_->isChecked()) {
            const QSignalBlocker blocker(plot_);
            plot_->setXRange(minTime, maxTime > minTime ? maxTime : minTime + 1.0);
        }
        updateTimeScrollBar();
        setVisibleData();
    } else {
        for (auto* line : std::as_const(lines_)) viewers::LinePlotView::setPoints(line, {});
    }

    if (autoScaleYCheck_->isChecked()) {
        // Auto Y must preserve the requested time range, including zoom/pan.
        static_cast<void>(plot_->fitYToVisibleData());
    }
}

void ScopeViewWindow::updateTimeScrollBar()
{
    if (!hasTimeRange_) {
        timeScrollBar_->setEnabled(false);
        return;
    }
    const double totalSpan = dataMaxTime_ - dataMinTime_;
    const double viewSpan = plot_->xMax() - plot_->xMin();
    const bool canScroll = totalSpan > 0.0 && viewSpan < totalSpan;
    timeScrollBar_->setEnabled(canScroll && !fitAllTimeCheck_->isChecked());
    const int pageStep = totalSpan > 0.0
        ? std::clamp(static_cast<int>(std::round(10000.0 * viewSpan / totalSpan)), 1, 10000)
        : 10000;
    updatingTimeScrollBar_ = true;
    timeScrollBar_->setPageStep(pageStep);
    const double scrollableSpan = std::max(0.0, totalSpan - viewSpan);
    const double normalized = scrollableSpan > 0.0
        ? (plot_->xMin() - dataMinTime_) / scrollableSpan
        : 0.0;
    timeScrollBar_->setValue(std::clamp(static_cast<int>(std::round(normalized * 10000.0)), 0, 10000));
    updatingTimeScrollBar_ = false;
}

void ScopeViewWindow::scrollToTimePosition(const int position)
{
    if (updatingTimeScrollBar_ || !hasTimeRange_ || fitAllTimeCheck_->isChecked()) {
        return;
    }
    const double viewSpan = std::min(plot_->xMax() - plot_->xMin(), dataMaxTime_ - dataMinTime_);
    const double scrollableSpan = std::max(0.0, dataMaxTime_ - dataMinTime_ - viewSpan);
    const double lower = dataMinTime_ + scrollableSpan * static_cast<double>(position) / 10000.0;
    plot_->setXRange(lower, lower + viewSpan);
}

} // namespace s7studio::ui
