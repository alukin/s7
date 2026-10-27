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

#include <QCheckBox>
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
#include <QSignalBlocker>
#include <QTimer>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QtGraphs/QLineSeries>

#include <algorithm>
#include <cmath>
#include <limits>
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
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(900, 360);
    setWindowTitle(QStringLiteral("Scope - %1").arg(probeName_));
    resize(900, 520);

    auto* rootLayout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    auto* resultsControls = new QHBoxLayout();

    autoScaleYCheck_ = new QCheckBox(QStringLiteral("Auto Y"), this);
    autoScaleYCheck_->setChecked(true);
    controls->addWidget(autoScaleYCheck_);
    fitAllTimeCheck_ = new QCheckBox(QStringLiteral("Fit full run"), this);
    fitAllTimeCheck_->setChecked(true);
    controls->addWidget(fitAllTimeCheck_);
    clearButton_ = new QPushButton(QStringLiteral("Clear display"), this);
    clearButton_->setToolTip(QStringLiteral("Clear this display. Captured data remains available for saving, export and reopening."));
    exportButton_ = new QPushButton(QStringLiteral("Export Full CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    auto* saveButton = new QPushButton(QStringLiteral("Save Run…"), this);
    auto* openButton = new QPushButton(QStringLiteral("Open Run…"), this);
    auto* removeButton = new QPushButton(QStringLiteral("Remove comparisons"), this);
    auto* detailsButton = new QPushButton(QStringLiteral("Run details…"), this);
    detailsButton->setObjectName(QStringLiteral("scope.runDetails"));
    saveButton->setObjectName(QStringLiteral("scope.saveRun"));
    openButton->setObjectName(QStringLiteral("scope.openRun"));
    removeButton->setObjectName(QStringLiteral("scope.removeComparisons"));
    saveButton->setToolTip(QStringLiteral("Save a snapshot of the current recording, including capture-loss warnings. Comparisons are saved separately."));
    openButton->setToolTip(QStringLiteral("Overlay saved results at their original model timestamps."));
    resultsControls->addWidget(saveButton);
    resultsControls->addWidget(openButton);
    resultsControls->addWidget(removeButton);
    resultsControls->addWidget(detailsButton);
    connect(detailsButton, &QPushButton::clicked, this, &ScopeViewWindow::showRunDetails);
    connect(saveButton, &QPushButton::clicked, this, &ScopeViewWindow::saveRunResults);
    connect(openButton, &QPushButton::clicked, this, &ScopeViewWindow::openRunResultsFile);
    connect(removeButton, &QPushButton::clicked, this, &ScopeViewWindow::clearComparisons);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("viewer.status"));
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);

    rootLayout->addLayout(controls);
    rootLayout->addLayout(resultsControls);
    rootLayout->addWidget(statusLabel_);

    plot_ = new viewers::LinePlotView(this);
    plot_->setLegendVisible(true);
    plot_->setAxisTitles(QStringLiteral("model time (s)"), QStringLiteral("value (unit unspecified)"));
    plot_->bridge()->inspectionEnabled = true;
    emit plot_->bridge()->inspectionChanged();
    plot_->setToolTip(QStringLiteral("Hover over a trace for stored sample values. Zoom with the mouse wheel and pan by dragging. Keyboard: +/− zoom, arrows pan, Home fits all data."));
    rootLayout->addWidget(plot_, 1);
    timeScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    timeScrollBar_->setRange(0, 10000);
    timeScrollBar_->setEnabled(false);
    timeScrollBar_->setToolTip(QStringLiteral("Scroll the visible time range. Zoom with the mouse wheel and pan by dragging. Keyboard: +/− zoom, arrows pan, Home fits all data."));
    rootLayout->addWidget(timeScrollBar_);

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
    // The handler refers to this window's widgets.
    recording_.setStatusChangedHandler({});
}

void ScopeViewWindow::showEvent(QShowEvent* event)
{
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
    QStringList parts;
    if (!sourceStatusText().isEmpty()) parts << sourceStatusText();
    if (!cacheStatus.isEmpty()) parts << (source() ? QStringLiteral("display cache: ") : QString()) + cacheStatus;
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void ScopeViewWindow::clearBuffers()
{
    discardPresentationHistory();
    recording_.clearLive();
    syncSeries(true);
    timeScrollBar_->setEnabled(false);
    dirty_ = true;
}

void ScopeViewWindow::clearComparisons()
{
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
