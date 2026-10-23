// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "observation/live_acquisition.hpp"
#include <algorithm>
#include <cmath>

namespace s7studio::observation {
double LiveFrame::plottedTime(std::size_t i) const
{
    if (!trigger || !trigger->triggered) return times[i];
    const auto delta = static_cast<double>(steps[i]-trigger->step);
    return rateNumerator ? delta*static_cast<double>(rateDenominator)/static_cast<double>(rateNumerator) : delta*dt;
}
std::optional<std::size_t> LiveFrame::nearestPlotted(double time) const
{
    if (times.empty() || !std::isfinite(time) || time < plottedTime(0) || time > plottedTime(times.size()-1)) return {};
    std::size_t lo = 0, hi = times.size();
    while (lo < hi) { const auto mid = (lo+hi)/2; if (plottedTime(mid) < time) lo = mid+1; else hi = mid; }
    if (lo && (lo == times.size() || time-plottedTime(lo-1) <= plottedTime(lo)-time)) --lo;
    return lo;
}
QJsonObject triggerMetadata(const LiveFrame& f)
{
    if (!f.trigger) return {};
    const auto& t = *f.trigger; const auto& s = t.settings;
    const char* modes[]{"rolling", "auto", "normal", "single"};
    return {{"mode", modes[static_cast<int>(s.mode)]}, {"channel", static_cast<int>(s.channel)}, {"edge", s.rising ? "rising" : "falling"},
        {"level", QString::number(s.level, 'g', 17)}, {"hysteresis", QString::number(s.hysteresis, 'g', 17)},
        {"pretrigger_percent", s.pretriggerPercent}, {"holdoff_steps", QString::number(s.holdoffSteps)}, {"auto_timeout_steps", QString::number(s.autoTimeoutSteps)},
        {"capture_id", QString::number(t.id)}, {"triggered", t.triggered}, {"partial", t.partial}, {"sampled_trigger_step", QString::number(t.step)},
        {"sampled_trigger_time_s", QString::number(t.time, 'g', 17)}, {"expected_positions", static_cast<int>(f.capacity)},
        {"captured_positions", static_cast<int>(f.steps.size())}, {"interpolated", false}};
}
void LiveAcquisition::resetTriggerLocked(bool discard)
{
    triggerWritten_ = 0; collecting_ = armed_ = held_ = haveCaptureEnd_ = false; previousTriggerValue_.reset();
    if (discard) { completedBank_ = -1; writingBank_ = 0; triggerAborted_ = 0; }
}
bool LiveAcquisition::configureTrigger(const TriggerSettings& s, QString* error)
{
    if (error) error->clear();
    if (static_cast<int>(s.mode) < 0 || static_cast<int>(s.mode) > 3 || s.channel >= static_cast<std::size_t>(channels_.size()) ||
        !std::isfinite(s.level) || !std::isfinite(s.hysteresis) || s.hysteresis < 0 ||
        !std::isfinite(s.level-s.hysteresis/2) || !std::isfinite(s.level+s.hysteresis/2) ||
        (s.hysteresis > 0 && !(s.level-s.hysteresis/2 < s.level+s.hysteresis/2)) ||
        s.pretriggerPercent < 0 || s.pretriggerPercent > 100 || !s.autoTimeoutSteps) {
        if (error) *error = "Trigger needs a valid channel, finite level/hysteresis, nonnegative hysteresis, pre-trigger 0–100%, and positive timeout.";
        return false;
    }
    std::lock_guard lock(mutex_);
    if (s != triggerSettings_) { triggerSettings_ = s; ++generation_; resetTriggerLocked(true); }
    return true;
}
void LiveAcquisition::rearmTrigger() { std::lock_guard lock(mutex_); resetTriggerLocked(false); }
void LiveAcquisition::appendTriggerLocked(std::int64_t step, double time, std::span<const double> values, bool gap)
{
    const auto& s = triggerSettings_;
    if (s.mode == TriggerMode::Rolling || held_) return;
    const double value = values[s.channel];
    if (gap || !std::isfinite(value)) {
        if (collecting_) ++triggerAborted_;
        triggerWritten_ = 0; collecting_ = armed_ = false; previousTriggerValue_.reset();
        if (!std::isfinite(value)) return;
    }
    auto& bank = triggerBanks_[writingBank_];
    const auto slot = static_cast<std::size_t>(triggerWritten_ % maxPositions);
    bank.steps[slot] = step; bank.times[slot] = time;
    std::copy(values.begin(), values.end(), bank.values.begin()+static_cast<std::ptrdiff_t>(slot*values.size()));
    if (!triggerWritten_) waitingSince_ = step;
    ++triggerWritten_;
    if (collecting_) { if (triggerWritten_ == captureEnd_) publishTriggerLocked(false); return; }
    if (haveCaptureEnd_ && static_cast<std::uint64_t>(step)-static_cast<std::uint64_t>(lastCaptureEnd_) <= s.holdoffSteps) {
        armed_ = false; previousTriggerValue_.reset(); return;
    }
    const auto pre = static_cast<std::uint64_t>(s.pretriggerPercent)*(capacity_-1)/100;
    bool fire = false;
    if (triggerWritten_ > pre) {
        if (s.hysteresis == 0) {
            if (previousTriggerValue_) {
                const auto before = *previousTriggerValue_;
                fire = s.rising ? ((before < s.level && value >= s.level) || (before <= s.level && value > s.level)) :
                    ((before > s.level && value <= s.level) || (before >= s.level && value < s.level));
            }
        } else {
            const auto low = s.level-s.hysteresis/2, high = s.level+s.hysteresis/2;
            if (s.rising ? value <= low : value >= high) armed_ = true;
            fire = armed_ && (s.rising ? value >= high : value <= low);
        }
    }
    previousTriggerValue_ = value;
    if (fire) {
        collectingInfo_ = {s, 0, true, false, step, time};
        captureStart_ = triggerWritten_-1-pre; captureEnd_ = captureStart_+capacity_; collecting_ = true;
        if (triggerWritten_ == captureEnd_) publishTriggerLocked(false);
    } else if (s.mode == TriggerMode::Auto && triggerWritten_ >= capacity_ &&
               static_cast<std::uint64_t>(step)-static_cast<std::uint64_t>(waitingSince_) >= s.autoTimeoutSteps) {
        collectingInfo_ = {s, 0, false, false, 0, 0};
        captureStart_ = triggerWritten_-capacity_; publishTriggerLocked(false);
    }
}
void LiveAcquisition::publishTriggerLocked(bool partial)
{
    completedBank_ = writingBank_; writingBank_ = 1-writingBank_;
    completedInfo_ = collectingInfo_; completedInfo_.id = ++captureSequence_; completedInfo_.partial = partial;
    completedStart_ = captureStart_; completedCount_ = triggerWritten_-captureStart_;
    completedReceived_ = received_; completedGaps_ = gaps_;
    const auto lastSlot = static_cast<std::size_t>((triggerWritten_-1)%maxPositions);
    lastCaptureEnd_ = triggerBanks_[completedBank_].steps[lastSlot]; haveCaptureEnd_ = true;
    held_ = triggerSettings_.mode == TriggerMode::Single;
    collecting_ = armed_ = false; triggerWritten_ = 0; previousTriggerValue_.reset();
}
TriggerStatus LiveAcquisition::triggerStatus() const
{
    std::lock_guard lock(mutex_);
    const auto pre = static_cast<std::uint64_t>(triggerSettings_.pretriggerPercent)*(capacity_-1)/100;
    const bool holdoff = haveCaptureEnd_ && received_ && static_cast<std::uint64_t>(steps_[(received_-1)%storagePositions])-static_cast<std::uint64_t>(lastCaptureEnd_) <= triggerSettings_.holdoffSteps;
    const auto state = triggerSettings_.mode == TriggerMode::Rolling ? TriggerState::Rolling : held_ ? TriggerState::Held : collecting_ ? TriggerState::Collecting :
        holdoff ? TriggerState::Holdoff : triggerWritten_ <= pre ? TriggerState::Filling : TriggerState::Armed;
    return {state, completedBank_ < 0 ? 0 : completedInfo_.id, triggerAborted_, finished_};
}
std::optional<LiveFrame> LiveAcquisition::triggerSnapshot() const
{
    LiveFrame f; int bankIndex; std::uint64_t first, count;
    {
        std::lock_guard lock(mutex_); if (completedBank_ < 0) return {};
        bankIndex = completedBank_; first = completedStart_; count = completedCount_;
        f.runId = runId_; f.generation = generation_; f.capacity = capacity_; f.received = completedReceived_;
        f.evicted = completedReceived_-count; f.gaps = completedGaps_; f.finished = finished_; f.error = error_; f.trigger = completedInfo_;
    }
    f.channels = channels_; f.provenance = provenance_; f.dt = dt_; f.rateNumerator = rateNumerator_; f.rateDenominator = rateDenominator_;
    f.steps.resize(count); f.times.resize(count); const auto width = static_cast<std::size_t>(channels_.size()); f.values.resize(count*width);
    for (std::size_t offset = 0; offset < count; offset += 256) {
        std::lock_guard lock(mutex_);
        if (generation_ != f.generation || completedBank_ != bankIndex || completedInfo_.id != f.trigger->id) return {};
        const auto& bank = triggerBanks_[bankIndex];
        for (auto i = offset; i < std::min<std::size_t>(count, offset+256); ++i) {
            const auto slot = static_cast<std::size_t>((first+i)%maxPositions);
            f.steps[i] = bank.steps[slot]; f.times[i] = bank.times[slot];
            std::copy_n(bank.values.begin()+static_cast<std::ptrdiff_t>(slot*width), width, f.values.begin()+static_cast<std::ptrdiff_t>(i*width));
        }
    }
    return f;
}
}
