// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <QString>
#include <QPointF>
#include <QHash>
#include <QVector>
#include <QJsonObject>
#include <cstdint>
#include <mutex>
#include <memory>
#include <optional>
#include <span>
#include <limits>
#include <vector>

namespace s7studio::observation {
enum class TriggerMode { Rolling, Auto, Normal, Single };
struct TriggerSettings {
    TriggerMode mode = TriggerMode::Rolling;
    std::size_t channel = 0;
    bool rising = true;
    double level = 0, hysteresis = 0;
    int pretriggerPercent = 25;
    std::uint64_t holdoffSteps = 0, autoTimeoutSteps = 4096;
    bool operator==(const TriggerSettings&) const = default;
};
struct TriggerCapture {
    TriggerSettings settings;
    std::uint64_t id = 0;
    bool triggered = false, partial = false;
    std::int64_t step = 0;
    double time = 0;
};
enum class TriggerState { Rolling, Filling, Armed, Collecting, Holdoff, Held };
struct TriggerStatus {
    TriggerState state;
    std::uint64_t captureId, aborted;
    bool finished;
};
struct LiveChannel { QString name; QString unit; QString sourceType; };
struct LiveStatus { bool finished; QString error; std::uint64_t received; };
struct LiveFrame {
    std::uint64_t runId = 0, generation = 0, received = 0, evicted = 0, gaps = 0;
    std::uint64_t rateNumerator = 0, rateDenominator = 1;
    std::size_t capacity = 0;
    bool finished = false;
    QString error;
    QVector<LiveChannel> channels;
    QJsonObject provenance;
    double dt = 0;
    std::optional<TriggerCapture> trigger;
    std::vector<std::int64_t> steps;
    std::vector<double> times, values; // values[frame * channels.size() + channel]
    [[nodiscard]] std::optional<std::size_t> nearest(double time) const;
    [[nodiscard]] double plottedTime(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> nearestPlotted(double time) const;
    // Min/max per consecutive bucket, in original order; raw data remains intact.
    [[nodiscard]] QList<QPointF> envelope(std::size_t channel, std::size_t buckets,
        double lower = -std::numeric_limits<double>::infinity(),
        double upper = std::numeric_limits<double>::infinity()) const;
};
QJsonObject triggerMetadata(const LiveFrame& frame);
// A LiveOscilloscope observer's saved "trigger" preset. Missing keys keep the
// defaults; any malformed or out-of-range key rejects the whole preset.
std::optional<TriggerSettings> triggerSettingsFromJson(const QJsonObject& json, QString* error = nullptr);
QJsonObject triggerSettingsToJson(const TriggerSettings& settings);

// Fixed allocation, no disk. One producer appends complete same-step frames;
// readers copy in small chunks and abandon expired snapshots without blocking
// acquisition for an entire window. Settings only change logical capacity.
class LiveAcquisition {
public:
    static constexpr std::size_t maxPositions = 65'536;
    static constexpr std::size_t maxChannels = 8;
    LiveAcquisition(std::uint64_t runId, double dt, QVector<LiveChannel> channels,
                    QJsonObject provenance = {}, std::uint64_t rateNumerator = 0, std::uint64_t rateDenominator = 1);
    void append(std::int64_t step, double time, std::span<const double> values);
    bool setCapacity(std::size_t capacity);
    bool setDuration(const QString& seconds, QString* error = nullptr);
    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] LiveStatus status() const;
    [[nodiscard]] std::optional<LiveFrame> snapshot() const;
    bool configureTrigger(const TriggerSettings& settings, QString* error = nullptr);
    void rearmTrigger();
    [[nodiscard]] TriggerStatus triggerStatus() const;
    [[nodiscard]] std::optional<LiveFrame> triggerSnapshot() const;
    void finish(const QString& error = {});
    [[nodiscard]] const QVector<LiveChannel>& channels() const { return channels_; }
    [[nodiscard]] double dt() const { return dt_; }
    [[nodiscard]] std::size_t allocatedBytes() const;
private:
    // Spare physical history allows chunked snapshots of a full logical window
    // while the producer advances. It is fixed, budgeted, and never exposed.
    static constexpr std::size_t storagePositions = 2 * maxPositions;
    const std::uint64_t runId_;
    const double dt_;
    const std::uint64_t rateNumerator_, rateDenominator_;
    const QVector<LiveChannel> channels_;
    const QJsonObject provenance_;
    mutable std::mutex mutex_;
    std::vector<std::int64_t> steps_;
    std::vector<double> times_, values_;
    std::size_t capacity_ = 4096, count_ = 0;
    std::uint64_t received_ = 0, generation_ = 1, gaps_ = 0;
    bool finished_ = false;
    QString error_;
    // Two fixed banks: one collects complete same-step frames, one holds the
    // latest completed capture. Publication swaps roles; no full-window copy
    // or allocation occurs on the causal stepping thread.
    struct TriggerBank { std::vector<std::int64_t> steps; std::vector<double> times, values; };
    TriggerBank triggerBanks_[2];
    TriggerSettings triggerSettings_;
    int writingBank_ = 0, completedBank_ = -1;
    std::uint64_t triggerWritten_ = 0, captureStart_ = 0, captureEnd_ = 0;
    std::uint64_t captureSequence_ = 0, triggerAborted_ = 0;
    std::uint64_t completedStart_ = 0, completedCount_ = 0, completedReceived_ = 0, completedGaps_ = 0;
    TriggerCapture collectingInfo_, completedInfo_;
    bool armed_ = false, collecting_ = false, held_ = false, haveCaptureEnd_ = false;
    std::optional<double> previousTriggerValue_;
    std::int64_t waitingSince_ = 0, lastCaptureEnd_ = 0;
    void resetTriggerLocked(bool discardCompleted);
    void appendTriggerLocked(std::int64_t step, double time, std::span<const double> values, bool gap);
    void publishTriggerLocked(bool partial);
};
using LiveSources = QHash<QString, std::shared_ptr<LiveAcquisition>>;
} // namespace s7studio::observation
