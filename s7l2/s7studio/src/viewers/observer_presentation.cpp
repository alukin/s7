// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "observer_presentation.hpp"
#include "line_plot_view.hpp"
#include "trajectory_plot.hpp"
#include "analysis/worker_pool.hpp"
#include <QtGraphs/QLineSeries>
#include <QtGraphs/QScatterSeries>
#include <QThreadPool>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>

namespace s7studio::viewers {
namespace {
QString unit(const QString& text) { return text.isEmpty() ? "unit unspecified" : text; }
QList<QPointF> reduce(const QList<QPointF>& points, int maximum) {
    if (points.size() <= maximum) return points;
    QList<QPointF> reduced;
    const auto buckets = std::max(1, maximum/4);
    for (int b = 0; b < buckets; ++b) {
        const auto begin = points.size()*b/buckets, end = points.size()*(b+1)/buckets;
        auto lo = begin, hi = begin;
        for (auto i = begin; i < end; ++i) { if (points[i].y() < points[lo].y()) lo = i; if (points[i].y() > points[hi].y()) hi = i; }
        std::array<qsizetype, 4> indices{begin, lo, hi, end-1}; std::sort(indices.begin(), indices.end());
        qsizetype previous = -1;
        for (auto i : indices) if (i != previous) { reduced.push_back(points[i]); previous = i; }
    }
    return reduced;
}
}
PreviewData preparePreview(const PreviewRequest& request)
{
    PreviewData result; result.revision = request.revision;
    QVector<QVector<observation::Sample>> samples(request.descriptor.inputs.size());
    bool phase = request.descriptor.type == "PhasePortrait" || request.descriptor.type == "PhaseTrajectory3D";
    QVector<int> selected = request.channels;
    if (phase) { selected.clear(); for (int i = 0; i < request.descriptor.inputs.size() && i < 3; ++i) selected.push_back(i); }
    if (selected.size() > 8) selected.resize(8);
    double origin = 0;
    bool timeEnvelope = false, envelopeGaps = false;
    std::array<bool, 8> envelopeInvalid{};
    if (request.live) {
        const auto frame = request.trigger.mode == observation::TriggerMode::Rolling ? std::optional{request.live->timeSnapshot()} : request.live->triggerSnapshot();
        if (!frame) { result.status = "Waiting for a completed sampled trigger capture."; return result; }
        timeEnvelope = frame->timeEnvelope; envelopeGaps = frame->envelopeGaps; envelopeInvalid = frame->envelopeInvalid;
        result.run = frame->runId;
        result.summary = frame->trigger ? (frame->trigger->triggered ? "Triggered" : "Auto timeout") : "Rolling";
        if (!frame->error.isEmpty()) result.summary.prepend("Error • ");
        else if (frame->gaps) result.summary.prepend("Data gaps • ");
        const auto begin = frame->steps.size() > 4096 ? frame->steps.size()-4096 : 0;
        for (auto channel : selected) if (channel >= 0 && channel < samples.size() && channel < frame->channels.size())
            for (auto i = begin; i < frame->steps.size(); ++i) samples[channel].push_back({static_cast<observation::ChannelId>(channel), frame->steps[i], frame->times[i], frame->values[i*static_cast<std::size_t>(frame->channels.size())+static_cast<std::size_t>(channel)]});
        if (frame->trigger && frame->trigger->triggered) { origin = frame->trigger->time; result.timeAxis = "Time from sampled trigger (s)"; }
        result.status = QStringLiteral("Recent ≤4096 positions/channel • %1 • gaps %2%3%4").arg(frame->trigger ? (frame->trigger->triggered ? "Triggered" : "Auto timeout: UNTRIGGERED") : "Rolling")
            .arg(frame->gaps).arg(frame->trigger && frame->trigger->partial ? " • PARTIAL" : "").arg(frame->finished ? " • run finished" : "");
        if (!frame->times.empty()) result.status += QStringLiteral(" • model time %1–%2 s (%3 s window)")
            .arg(frame->times[begin], 0, 'g', 10).arg(frame->times.back(), 0, 'g', 10)
            .arg(frame->times.back()-frame->times[begin], 0, 'g', 6);
        if (!frame->error.isEmpty()) result.status += " • ERROR: " + frame->error;
    } else if (request.source) {
        result.summary = "Recorded";
        result.run = request.source->runId(); const auto channels = request.source->channels();
        QVector<observation::ChannelId> ids; QVector<int> indices;
        for (auto index : selected) if (index >= 0 && index < samples.size())
            for (const auto& channel : channels) if (channel.name == request.descriptor.inputs[index].name) { ids.push_back(channel.id); indices.push_back(index); break; }
        const auto tail = request.source->tail(ids);
        if (!tail.status.storageError.isEmpty() || !tail.status.runError.isEmpty()) result.summary.prepend("Error • ");
        else if (tail.status.captureDropped) result.summary.prepend("Capture loss • ");
        for (int i = 0; i < tail.channels.size(); ++i) samples[indices[i]] = tail.channels[i].samples;
        result.status = QStringLiteral("Recent ≤4096 positions/channel • capture loss %1%2").arg(tail.status.captureDropped)
            .arg(tail.status.finished ? (tail.status.stopped ? " • stopped" : " • run finished") : "");
        if (!tail.status.storageError.isEmpty() || !tail.status.runError.isEmpty()) result.status += " • ERROR: " + tail.status.storageError + " " + tail.status.runError;
    } else { result.status = "Waiting for this observer's prepared run source."; return result; }
    std::size_t invalid = 0, gaps = 0;
    std::optional<double> firstTime, lastTime;
    QStringList unavailable;
    for (auto index : selected) if (index < 0 || index >= samples.size()) unavailable.push_back(QString::number(index));
    for (auto index : selected) if (index >= 0 && index < samples.size()) {
        const auto& input = request.descriptor.inputs[index]; const auto& data = samples[index];
        for (const auto& sample : data) if (std::isfinite(sample.timeSeconds)) {
            firstTime = firstTime ? std::min(*firstTime, sample.timeSeconds) : sample.timeSeconds;
            lastTime = lastTime ? std::max(*lastTime, sample.timeSeconds) : sample.timeSeconds;
        }
        const auto label = input.name + " [" + unit(input.unit) + "]";
        result.axes.push_back(phase ? QStringList{"X", "Y", "Z"}.value(index)+" ["+unit(input.unit)+"]" : label);
        QString latest = "no sample";
        if (!data.empty()) { const auto& value = data.back(); latest = QStringLiteral("step %1 • %2 s • %3").arg(value.step).arg(value.timeSeconds, 0, 'g', 12).arg(value.value, 0, 'g', 17); }
        result.details.push_back(QStringLiteral("%1 ← %2.output %3 • %4").arg(label, input.element).arg(input.port).arg(latest));
        result.rawPositions += static_cast<std::size_t>(data.size());
        if (!phase) {
            PreviewData::Trace trace; trace.name = label; trace.channel = index; QList<QPointF> segment;
            std::optional<std::int64_t> previous;
            for (const auto& value : data) {
                const bool finite = std::isfinite(value.value) && std::isfinite(value.timeSeconds) && !envelopeGaps && !envelopeInvalid[static_cast<std::size_t>(index)];
                if (!finite || (!timeEnvelope && previous && value.step != *previous+1)) {
                    if (!segment.empty()) { trace.segments.push_back(std::move(segment)); segment.clear(); }
                    if (!finite) ++invalid; else ++gaps;
                }
                previous = value.step;
                if (finite) segment.push_back({value.timeSeconds-origin, value.value});
            }
            if (!segment.empty()) trace.segments.push_back(std::move(segment));
            while (trace.segments.size() > 16) trace.segments.pop_front();
            const int budget = trace.segments.empty() ? 1024 : 1024/static_cast<int>(trace.segments.size());
            for (auto& line : trace.segments) { line = reduce(line, budget); result.vertices += static_cast<std::size_t>(line.size()); }
            result.traces.push_back(std::move(trace));
        }
    }
    if (phase && samples.size() >= (request.descriptor.type == "PhaseTrajectory3D" ? 3 : 2)) {
        const int axes = request.descriptor.type == "PhaseTrajectory3D" ? 3 : 2;
        std::map<std::int64_t, std::array<std::optional<observation::Sample>, 3>> aligned;
        for (int axis = 0; axis < axes; ++axis) for (const auto& sample : samples[axis]) aligned[sample.step][axis] = sample;
        QList<QPointF> xy; std::optional<std::int64_t> previous;
        for (const auto& [step, values] : aligned) {
            bool valid = true; for (int axis = 0; axis < axes; ++axis) valid &= values[axis].has_value() && std::isfinite(values[axis]->value);
            if (!valid) { ++invalid; continue; }
            if (previous && step != *previous+1) { ++gaps; xy.clear(); result.trajectory.clear(); }
            previous = step;
            if (axes == 2) xy.push_back({values[0]->value, values[1]->value});
            else result.trajectory.push_back({values[0]->value, values[1]->value, values[2]->value});
        }
        if (axes == 2) {
            // Preserve causal trajectory order; extrema-in-time reduction is for Time only.
            QList<QPointF> reduced; const auto count = std::min<qsizetype>(1024, xy.size());
            for (qsizetype i = 0; i < count; ++i) reduced.push_back(xy[count == 1 ? 0 : i*(xy.size()-1)/(count-1)]);
            result.vertices = static_cast<std::size_t>(reduced.size()); result.traces.push_back({"Same-step XY", {std::move(reduced)}});
        } else {
            QList<TrajectoryPoint> reduced; const auto count = std::min<qsizetype>(1024, result.trajectory.size());
            for (qsizetype i = 0; i < count; ++i) reduced.push_back(result.trajectory[count == 1 ? 0 : i*(result.trajectory.size()-1)/(count-1)]);
            result.trajectory = std::move(reduced); result.vertices = static_cast<std::size_t>(result.trajectory.size());
        }
        result.status += " • same-step coordinates; latest continuous segment";
        if (axes == 3) result.status += " • " + result.axes.join(" • ");
    }
    result.status += QStringLiteral(" • gaps %1 • invalid/unpaired %2 • plotted %3/%4 channels").arg(gaps).arg(invalid).arg(selected.size()-unavailable.size()).arg(request.descriptor.inputs.size());
    if (!unavailable.empty()) result.status += " • Unavailable selected channel indices: " + unavailable.join(", ");
    if (result.vertices == 0) result.status += " • Waiting for finite, connected samples";
    if (firstTime && lastTime) result.summary += QStringLiteral(" • %1–%2 s").arg(*firstTime, 0, 'g', 5).arg(*lastTime, 0, 'g', 5);
    if (gaps || invalid) result.summary.prepend("Data gaps • ");
    if (result.vertices == 0 && !result.summary.startsWith("Error")) result.summary = "Waiting for finite samples";
    if (!unavailable.empty()) result.summary.prepend("Unavailable channels • ");
    return result;
}

struct PreviewWorker::State {
    std::mutex mutex;
    std::optional<PreviewRequest> pending;
    std::optional<PreviewData> ready;
    bool running = false;
    std::uint64_t epoch = 0;
};
PreviewWorker::PreviewWorker() : state_(std::make_shared<State>()) {}
PreviewWorker::~PreviewWorker() { cancel(); }
void PreviewWorker::cancel() { std::lock_guard lock(state_->mutex); ++state_->epoch; state_->pending.reset(); state_->ready.reset(); }
void PreviewWorker::submit(PreviewRequest request) {
    std::lock_guard lock(state_->mutex); state_->pending = std::move(request); if (state_->running) return;
    state_->running = true;
    analysis::analysisPool().start([state = state_] {
        for (;;) {
            PreviewRequest next; std::uint64_t epoch;
            { std::lock_guard guard(state->mutex); if (!state->pending) { state->running = false; return; } next = std::move(*state->pending); state->pending.reset(); epoch = state->epoch; }
            PreviewData data;
            try { data = preparePreview(next); } catch (const std::exception& ex) { data.revision = next.revision; data.status = QString::fromUtf8(ex.what()); }
            { std::lock_guard guard(state->mutex); if (epoch == state->epoch) state->ready = std::move(data); }
        }
    });
}
std::optional<PreviewData> PreviewWorker::takeResult() { std::lock_guard lock(state_->mutex); auto result = std::move(state_->ready); state_->ready.reset(); return result; }
bool PreviewWorker::busy() const { std::lock_guard lock(state_->mutex); return state_->running; }

ObserverPresentation::ObserverPresentation(const QString& type, QWidget* parent) : QWidget(parent), type_(type) {
    setObjectName("observer.presentation"); auto* root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0);
    if (type == "PhaseTrajectory3D") { trajectory_ = new ui::TrajectoryPlotView(this); root->addWidget(trajectory_); }
    else { line_ = new LinePlotView(this); line_->setLegendVisible(false); line_->bridge()->compactAxes = true; root->addWidget(line_);
        connect(line_, &LinePlotView::navigated, this, &ObserverPresentation::navigated); }
}
void ObserverPresentation::present(const PreviewData& data) {
    if (trajectory_) {
        const auto bounds = TrajectoryBounds::of(data.trajectory); trajectory_->setAxisLabels(bounds, data.axes, true);
        trajectory_->bridge()->trajectoryGeometry().setTrajectory(data.trajectory, bounds, 1024);
        trajectory_->bridge()->setHead(!data.trajectory.empty(), data.trajectory.empty() ? QVector3D{} : toScene(data.trajectory.back(), bounds));
        return;
    }
    int needed = 0; for (const auto& trace : data.traces) for (const auto& segment : trace.segments) needed += segment.size()>1;
    while (line_->series().size() < needed) line_->addSeries("", {channelColor(type_, 0)});
    int i = 0;
    while (isolated_.size() < data.traces.size()) isolated_.push_back(line_->addScatterSeries("", channelColor(type_, 0)));
    int channel = 0;
    for (const auto& trace : data.traces) {
        QList<QPointF> singlePoints;
        for (const auto& points : trace.segments) {
            if (points.size()==1) { singlePoints.push_back(points.front()); continue; }
            if (points.empty()) continue;
            auto* series = line_->series()[i++]; series->setColor(channelColor(type_, trace.channel)); LinePlotView::setPoints(series, points);
        }
        isolated_[channel]->setColor(channelColor(type_, trace.channel)); LinePlotView::setScatterPoints(isolated_[channel++], singlePoints);
    }
    for (; i < line_->series().size(); ++i) LinePlotView::setPoints(line_->series()[i], {});
    for (; channel < isolated_.size(); ++channel) LinePlotView::setScatterPoints(isolated_[channel], {});
    line_->setAxisTitles(type_ == "PhasePortrait" ? data.axes.value(0) : data.timeAxis, type_ == "PhasePortrait" ? data.axes.value(1) : "Value (supplied units)");
    static_cast<void>(line_->fitAll());
}
QColor ObserverPresentation::channelColor(const QString& type, int channel) {
    static const QList<QColor> scope{QColor("#1f77b4"), QColor("#d62728"), QColor("#2ca02c"), QColor("#9467bd"), QColor("#ff7f0e"), QColor("#17becf")};
    static const QList<QColor> live{QColor("#1565c0"), QColor("#c62828"), QColor("#00875a"), QColor("#8e24aa"), QColor("#c25e00"), QColor("#007b83"), QColor("#544d00"), QColor("#555555")};
    const auto& palette = type == "Scope" ? scope : live; return palette[std::max(0, channel)%palette.size()];
}
}
