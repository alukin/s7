// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/time_view_worker.hpp"
#include "analysis/worker_pool.hpp"
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

namespace s7studio::analysis {
using Clock = std::chrono::steady_clock;

namespace {
void prepareTimeResult(TimeViewResult& result, const TimeViewSettings& settings)
{
    if (!result.frame) return;
    const auto& frame = *result.frame;
    result.preparedTime = settings.prepareTime;
    if (settings.fitX && !frame.times.empty()) {
        result.hasXRange = true;
        if (frame.trigger && frame.trigger->triggered) {
            const auto pre = static_cast<std::size_t>(frame.trigger->settings.pretriggerPercent)*(frame.capacity-1)/100;
            const double post = static_cast<double>(frame.capacity-1-pre);
            const double span = settings.timeSpan > 0 ? settings.timeSpan : static_cast<double>(frame.capacity-1)*frame.dt;
            result.xMin = -static_cast<double>(pre)/static_cast<double>(frame.capacity-1)*span;
            result.xMax = post/static_cast<double>(frame.capacity-1)*span;
        } else {
            result.xMax = frame.times.back();
            result.xMin = result.xMax-(settings.timeSpan > 0 ? settings.timeSpan : static_cast<double>(frame.capacity-1)*frame.dt);
        }
    }
    if (!settings.prepareTime) return;
    const double lower = result.hasXRange ? result.xMin : settings.lower;
    const double upper = result.hasXRange ? result.xMax : settings.upper;
    const auto started = Clock::now();
    bool warned = false, overflow = false;
    for (std::size_t channel = 0; channel < static_cast<std::size_t>(frame.channels.size()); ++channel) {
        if (channel >= result.points.size() || !settings.visible[channel]) continue;
        bool continuous = !frame.envelopeGaps && !frame.envelopeInvalid[channel];
        for (std::size_t i = 0; i < frame.steps.size(); ++i) {
            if ((!frame.timeEnvelope && i && frame.steps[i] != frame.steps[i-1]+1) ||
                !std::isfinite(frame.values[i*static_cast<std::size_t>(frame.channels.size())+channel])) { continuous = false; break; }
        }
        if (!continuous) { warned = true; continue; }
        auto points = frame.envelope(channel, settings.buckets, lower, upper);
        for (auto& point : points) {
            point.setY(point.y()*settings.gain[channel]+settings.offset[channel]);
            if (!std::isfinite(point.y())) { continuous = false; overflow = true; break; }
        }
        if (!continuous) { points.clear(); warned = true; continue; }
        for (const auto& point : points) {
            if (!settings.autoY || point.x() < lower || point.x() > upper) continue;
            if (!result.hasYRange) { result.yMin = result.yMax = point.y(); result.hasYRange = true; }
            else { result.yMin = std::min(result.yMin, point.y()); result.yMax = std::max(result.yMax, point.y()); }
        }
        result.points[channel] = std::move(points);
    }
    if (overflow) result.warning = "Display gain/offset overflow; trace hidden. Reduce gain/offset; raw inspection remains available.";
    else if (warned) result.warning = "Discontinuous/nonfinite trace hidden; raw inspection remains available.";
    result.prepareMs = std::chrono::duration<double, std::milli>(Clock::now()-started).count();
}
}

struct TimeViewWorker::State : std::enable_shared_from_this<State> {
    struct Request {
        std::shared_ptr<observation::LiveAcquisition> source;
        std::shared_ptr<const observation::LiveFrame> frame;
        TimeViewSettings settings;
        std::uint64_t revision = 0, epoch = 0;
        bool triggered = false;
    };
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> epoch{1};
    std::optional<Request> pending;
    std::optional<TimeViewResult> ready;
    bool running = false;
    std::uint64_t replaced = 0;

    void enqueue(Request request)
    {
        bool start = false;
        {
            std::lock_guard lock(mutex);
            if (pending) ++replaced;
            pending = std::move(request);
            if (!running) { running = true; start = true; }
        }
        if (start) {
            auto self = shared_from_this();
            analysisPool().start([self] { self->run(); });
        }
    }
    void run()
    {
        for (;;) {
            std::optional<Request> request;
            {
                std::lock_guard lock(mutex);
                if (!pending) { running = false; return; }
                request = std::move(pending); pending.reset();
            }
            TimeViewResult result; result.revision = request->revision;
            const auto snapshotStart = Clock::now();
            try {
                if (request->source) {
                    result.sourceSnapshot = true;
                    auto snapshot = request->triggered ? request->source->triggerSnapshot() : request->source->snapshot();
                    if (snapshot) result.rawFrame = std::make_shared<observation::LiveFrame>(*snapshot);
                    if (request->settings.prepareTime && !request->triggered && request->settings.timeSpan > 0)
                        snapshot = request->source->timeSnapshot();
                    result.snapshotMs = std::chrono::duration<double, std::milli>(Clock::now()-snapshotStart).count();
                    if (snapshot) result.frame = std::make_shared<observation::LiveFrame>(std::move(*snapshot));
                } else result.frame = request->frame;
                prepareTimeResult(result, request->settings);
            } catch (const std::exception& error) {
                result.frame.reset(); result.warning = QStringLiteral("Time-view preparation failed: %1").arg(QString::fromUtf8(error.what()));
            } catch (...) {
                result.frame.reset(); result.warning = "Time-view preparation failed.";
            }
            std::lock_guard lock(mutex);
            if (epoch.load() == request->epoch) ready = std::move(result);
        }
    }
};

TimeViewWorker::TimeViewWorker() : state_(std::make_shared<State>()) { (void)analysisPool(); }
TimeViewWorker::~TimeViewWorker() { cancel(); }
void TimeViewWorker::cancel()
{
    std::lock_guard lock(state_->mutex);
    ++state_->epoch; state_->pending.reset(); state_->ready.reset(); state_->replaced = 0;
}
void TimeViewWorker::submitSource(std::shared_ptr<observation::LiveAcquisition> source, bool triggered,
                                  TimeViewSettings settings, std::uint64_t revision)
{
    if (!source) return;
    state_->enqueue(State::Request{std::move(source), {}, settings, revision, state_->epoch.load(), triggered});
}
void TimeViewWorker::submitFrame(std::shared_ptr<const observation::LiveFrame> frame,
                                 TimeViewSettings settings, std::uint64_t revision)
{
    if (!frame) return;
    state_->enqueue(State::Request{{}, std::move(frame), settings, revision, state_->epoch.load(), false});
}
std::optional<TimeViewResult> TimeViewWorker::takeResult()
{
    std::lock_guard lock(state_->mutex);
    auto result = std::move(state_->ready); state_->ready.reset(); return result;
}
bool TimeViewWorker::busy() const { std::lock_guard lock(state_->mutex); return state_->running || state_->pending.has_value(); }
std::uint64_t TimeViewWorker::replacedRequests() const { std::lock_guard lock(state_->mutex); return state_->replaced; }
}
