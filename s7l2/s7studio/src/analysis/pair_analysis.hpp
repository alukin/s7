// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/fft.hpp"
#include "observation/live_acquisition.hpp"
#include <deque>
#include <functional>

namespace s7studio::analysis {
enum class PairKind { Constellation, Correlation };
enum class CorrelationNormalization { Raw, Biased, Unbiased, Coefficient };
struct PairSettings {
    std::size_t first = 0, second = 1, size = 4096, maxLag = 1024, densitySize = 128;
    std::uint64_t persistenceSteps = 0; // 0: current raw frame only; otherwise age in model steps.
    bool autoCorrelation = false, removeMean = false, absolutePeak = false;
    CorrelationNormalization normalization = CorrelationNormalization::Coefficient;
    bool operator==(const PairSettings&) const = default;
};
struct XYPoint {
    std::int64_t step;
    double time, x, y;
    bool segmentStart;
};
struct PairResult {
    enum class State { Ready, Filling, Unavailable, Cancelled } state = State::Unavailable;
    PairKind kind = PairKind::Constellation;
    PairSettings settings;
    QString message, firstName, secondName, firstUnit, secondUnit;
    std::uint64_t runId = 0, generation = 0, revision = 0, invalid = 0, gaps = 0, evicted = 0;
    std::int64_t firstStep = 0, lastStep = 0;
    double sampleRate = 0;
    std::shared_ptr<const observation::LiveFrame> frame;
    std::vector<XYPoint> points;
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;
    std::vector<std::uint32_t> density; // row 0 is minimum Y; values saturate.
    std::vector<int> lags;
    std::vector<double> correlations;
    std::size_t peakIndex = 0;
};
// One serial engine per panel; all data is read-only and bounded by 65536 raw
// positions plus a density grid <=256² or a zero-padded FFT <=131072.
class PairAnalyzer {
public:
    PairResult analyze(PairKind kind, const observation::LiveFrame& frame, const PairSettings& settings,
                       const std::function<bool()>& cancelled = {});
    void reset();
private:
    Fft fft_;
    std::optional<PairSettings> settings_;
    std::uint64_t run_ = 0, generation_ = 0, rateNumerator_ = 0, rateDenominator_ = 0;
    std::uint64_t invalid_ = 0, gaps_ = 0, evicted_ = 0;
    std::int64_t last_ = 0;
    bool segmentStart_ = true;
    bool haveLast_ = false;
    std::deque<XYPoint> history_;
};
class PairWorker {
public:
    explicit PairWorker(PairKind kind);
    ~PairWorker();
    void submit(std::shared_ptr<const observation::LiveFrame> frame, PairSettings settings, std::uint64_t revision);
    void cancel();
    std::optional<PairResult> takeResult();
    bool busy() const;
    std::uint64_t replacedRequests() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
