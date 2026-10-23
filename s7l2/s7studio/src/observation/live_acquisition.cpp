// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "observation/live_acquisition.hpp"
#include <QPointF>
#include <QRegularExpression>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace s7studio::observation {
std::optional<std::size_t> LiveFrame::nearest(double time) const
{
    if (times.empty() || !std::isfinite(time) || time < times.front() || time > times.back()) return {};
    const auto it = std::lower_bound(times.begin(), times.end(), time);
    auto i = static_cast<std::size_t>(it - times.begin());
    if (i > 0 && (i == times.size() || time - times[i-1] <= times[i] - time)) --i;
    return i;
}
QList<QPointF> LiveFrame::envelope(std::size_t channel, std::size_t buckets, double lower, double upper) const
{
    QList<QPointF> result;
    if (channel >= static_cast<std::size_t>(channels.size()) || times.empty()) return result;
    const auto stride = static_cast<std::size_t>(channels.size());
    if (upper < plottedTime(0) || lower > plottedTime(times.size()-1) || lower > upper) return result;
    auto bound = [&](double value, bool inclusive) {
        std::size_t lo = 0, hi = times.size();
        while (lo < hi) { const auto mid = (lo+hi)/2; if (plottedTime(mid) < value || (inclusive && plottedTime(mid) == value)) lo = mid+1; else hi = mid; }
        return lo;
    };
    auto first = bound(lower, false), last = bound(upper, true);
    if (first > 0) --first; // Preserve line intersections with viewport boundaries.
    if (last < times.size()) ++last;
    const auto bucketSize = std::max<std::size_t>(1, (last - first + std::max<std::size_t>(1, buckets) - 1) / std::max<std::size_t>(1, buckets));
    // Invalid/gapped intervals must be segmented by the renderer. Do not reduce
    // them into a misleading continuous line.
    for (std::size_t i = 0; i < times.size(); ++i)
        if (!std::isfinite(values[i*stride+channel]) || (i && steps[i] != steps[i-1]+1)) return {};
    for (std::size_t begin = first; begin < last; begin += bucketSize) {
        const auto end = std::min(last, begin + bucketSize);
        auto lo = begin, hi = begin;
        for (auto i = begin; i < end; ++i) {
            const double v = values[i*stride+channel];
            if (v < values[lo*stride+channel]) lo = i;
            if (v > values[hi*stride+channel]) hi = i;
        }
        // Include bucket endpoints; extrema stay in chronological order.
        std::size_t indices[]{begin, lo, hi, end-1};
        std::sort(std::begin(indices), std::end(indices));
        auto previous = times.size();
        for (auto i : indices) if (i != previous) {
            result.push_back({plottedTime(i), values[i*stride+channel]}); previous = i;
        }
    }
    return result;
}
LiveAcquisition::LiveAcquisition(std::uint64_t id, double dt, QVector<LiveChannel> channels, QJsonObject provenance, std::uint64_t rateNumerator, std::uint64_t rateDenominator)
    : runId_(id), dt_(dt), rateNumerator_(rateNumerator), rateDenominator_(rateDenominator), channels_(std::move(channels)), provenance_(std::move(provenance))
{
    if (!std::isfinite(dt) || dt <= 0 || !rateDenominator || channels_.isEmpty() || channels_.size() > static_cast<qsizetype>(maxChannels))
        throw std::invalid_argument("Live acquisition requires a positive dt and 1–8 channels.");
    steps_.resize(storagePositions); times_.resize(storagePositions);
    values_.resize(storagePositions * static_cast<std::size_t>(channels_.size()));
    for (auto& bank : triggerBanks_) {
        bank.steps.resize(maxPositions); bank.times.resize(maxPositions);
        bank.values.resize(maxPositions*static_cast<std::size_t>(channels_.size()));
    }
}
void LiveAcquisition::append(std::int64_t step, double time, std::span<const double> values)
{
    if (values.size() != static_cast<std::size_t>(channels_.size()) || !std::isfinite(time))
        throw std::invalid_argument("Invalid live acquisition frame.");
    std::lock_guard lock(mutex_);
    if (finished_) throw std::logic_error("Live acquisition is finished.");
    bool gap = false;
    if (received_) {
        const auto previous = static_cast<std::size_t>((received_-1) % storagePositions);
        if (step <= steps_[previous] || time <= times_[previous]) throw std::invalid_argument("Live steps/time must increase.");
        gap = step != steps_[previous]+1;
        if (gap) ++gaps_;
    }
    const auto slot = static_cast<std::size_t>(received_ % storagePositions);
    steps_[slot] = step; times_[slot] = time;
    std::copy(values.begin(), values.end(), values_.begin() + static_cast<std::ptrdiff_t>(slot*values.size()));
    ++received_; count_ = std::min(count_ + 1, capacity_);
    appendTriggerLocked(step, time, values, gap);
}
bool LiveAcquisition::setCapacity(std::size_t capacity)
{
    if (capacity < 2 || capacity > maxPositions) return false;
    std::lock_guard lock(mutex_);
    if (capacity != capacity_) { capacity_ = capacity; count_ = std::min(count_, capacity); ++generation_; resetTriggerLocked(true); }
    return true;
}
bool LiveAcquisition::setDuration(const QString& seconds, QString* error)
{
    using boost::multiprecision::cpp_int;
    auto fail = [&] { if (error) *error = QStringLiteral("Enter a positive decimal duration fitting 2–65536 samples at this run's rate."); return false; };
    if (error) error->clear();
    if (!rateNumerator_ || seconds.size() > 80) return fail();
    static const QRegularExpression decimal(QStringLiteral(R"(^([0-9]+)(?:\.([0-9]*))?(?:[eE]([+-]?[0-9]{1,2}))?$)"));
    const auto match = decimal.match(seconds.trimmed());
    if (!match.hasMatch()) return fail();
    const auto digits = match.captured(1) + match.captured(2);
    cpp_int numerator = 0, denominator = rateDenominator_;
    for (const auto c : digits) { numerator *= 10; numerator += c.digitValue(); }
    if (numerator == 0) return fail();
    numerator *= rateNumerator_;
    const int exponent = match.captured(3).toInt() - static_cast<int>(match.captured(2).size());
    if (exponent < -100 || exponent > 100) return fail();
    for (int i = 0; i < std::abs(exponent); ++i) { if (exponent < 0) denominator *= 10; else numerator *= 10; }
    const cpp_int capacity = (numerator + denominator - 1) / denominator + 1;
    if (capacity < 2 || capacity > maxPositions) return fail();
    return setCapacity(capacity.convert_to<std::size_t>());
}
std::size_t LiveAcquisition::capacity() const { std::lock_guard lock(mutex_); return capacity_; }
LiveStatus LiveAcquisition::status() const { std::lock_guard lock(mutex_); return {finished_, error_, received_}; }
std::size_t LiveAcquisition::allocatedBytes() const
{
    auto bytes = steps_.capacity()*sizeof(std::int64_t) + (times_.capacity()+values_.capacity())*sizeof(double);
    for (const auto& bank : triggerBanks_) bytes += bank.steps.capacity()*sizeof(std::int64_t)+(bank.times.capacity()+bank.values.capacity())*sizeof(double);
    return bytes;
}
std::optional<LiveFrame> LiveAcquisition::snapshot() const
{
    LiveFrame result;
    std::size_t count;
    std::uint64_t first;
    {
        std::lock_guard lock(mutex_);
        result.runId = runId_; result.generation = generation_; result.received = received_;
        result.evicted = received_ - count_; result.gaps = gaps_; result.capacity = capacity_;
        result.finished = finished_; result.error = error_; count = count_; first = received_ - count_;
    }
    result.dt = dt_; result.rateNumerator = rateNumerator_; result.rateDenominator = rateDenominator_;
    result.channels = channels_; result.provenance = provenance_;
    result.steps.resize(count); result.times.resize(count);
    const auto width = static_cast<std::size_t>(channels_.size());
    result.values.resize(count*width);
    for (std::size_t offset = 0; offset < count; offset += 256) {
        std::lock_guard lock(mutex_);
        // A resize/eviction invalidates this request; never mix windows.
        if (generation_ != result.generation || received_ > first + offset + storagePositions) return {};
        const auto end = std::min(count, offset + 256);
        for (auto i = offset; i < end; ++i) {
            const auto slot = static_cast<std::size_t>((first+i) % storagePositions);
            result.steps[i] = steps_[slot]; result.times[i] = times_[slot];
            std::copy_n(values_.begin() + static_cast<std::ptrdiff_t>(slot*width), width,
                        result.values.begin() + static_cast<std::ptrdiff_t>(i*width));
        }
    }
    return result;
}
void LiveAcquisition::finish(const QString& error)
{
    std::lock_guard lock(mutex_);
    if (!finished_) { if (collecting_) publishTriggerLocked(true); finished_ = true; error_ = error; }
}
} // namespace s7studio::observation
