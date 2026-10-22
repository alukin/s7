// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/pair_analysis.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace s7studio::analysis {
namespace {
struct Sum {
    double value = 0, correction = 0;
    void add(double x) { const double y = x-correction, t = value+y; correction = (t-value)-y; value = t; }
};
void widen(double& low, double& high)
{
    if (low == high) {
        const double pad = low == 0 ? .5 : std::max(std::abs(low)*.05, std::numeric_limits<double>::denorm_min());
        low -= pad; high += pad;
    }
}
}
void PairAnalyzer::reset()
{
    settings_.reset(); history_.clear(); last_ = 0; segmentStart_ = true;
    haveLast_ = false;
    invalid_ = 0; gaps_ = 0; evicted_ = 0;
}
PairResult PairAnalyzer::analyze(PairKind kind, const observation::LiveFrame& frame, const PairSettings& s,
                                const std::function<bool()>& cancelled)
{
    PairResult r; r.kind = kind; r.settings = s; r.runId = frame.runId; r.generation = frame.generation;
    auto stop = [&] { return cancelled && cancelled(); };
    auto fail = [&](PairResult::State state, const QString& message) { r.state = state; r.message = message; reset(); return r; };
    const auto width = static_cast<std::size_t>(frame.channels.size());
    const auto second = kind == PairKind::Correlation && s.autoCorrelation ? s.first : s.second;
    if (s.first >= width || second >= width || (kind == PairKind::Constellation && s.first == second))
        return fail(PairResult::State::Unavailable, "Select two channels (different channels for XY).");
    if (!frame.rateNumerator || !frame.rateDenominator)
        return fail(PairResult::State::Unavailable, "A known rational model rate is required.");
    if (frame.steps.size() != frame.times.size() || frame.values.size() != frame.steps.size()*width)
        return fail(PairResult::State::Unavailable, "Inconsistent acquisition frame.");
    const auto& aChannel = frame.channels[static_cast<qsizetype>(s.first)];
    const auto& bChannel = frame.channels[static_cast<qsizetype>(second)];
    r.firstName = aChannel.name; r.secondName = bChannel.name; r.firstUnit = aChannel.unit; r.secondUnit = bChannel.unit;
    r.sampleRate = static_cast<double>(frame.rateNumerator)/static_cast<double>(frame.rateDenominator);
    if (frame.steps.empty()) return fail(PairResult::State::Filling, "Filling: waiting for paired samples.");
    r.lastStep = frame.steps.back();
    if (kind == PairKind::Constellation) {
        if (s.densitySize != 64 && s.densitySize != 128 && s.densitySize != 256)
            return fail(PairResult::State::Unavailable, "Density grid must be 64, 128 or 256 bins per axis.");
        if (!settings_ || *settings_ != s || run_ != frame.runId || generation_ != frame.generation ||
            rateNumerator_ != frame.rateNumerator || rateDenominator_ != frame.rateDenominator ||
            frame.steps.back() < last_ || !s.persistenceSteps) {
            reset(); settings_ = s; run_ = frame.runId; generation_ = frame.generation;
            rateNumerator_ = frame.rateNumerator; rateDenominator_ = frame.rateDenominator;
        }
        for (std::size_t i = 0; i < frame.steps.size(); ++i) {
            if ((i % 1024 == 0) && stop()) return fail(PairResult::State::Cancelled, "Cancelled");
            const auto step = frame.steps[i];
            if (haveLast_ && step <= last_) continue; // Overlapping snapshots contribute each model step once.
            if (haveLast_ && step != last_+1) { ++gaps_; segmentStart_ = true; }
            last_ = step; haveLast_ = true;
            const double x = frame.values[i*width+s.first], y = frame.values[i*width+second];
            if (!std::isfinite(x) || !std::isfinite(y)) { ++invalid_; segmentStart_ = true; continue; }
            history_.push_back({step, frame.times[i], x, y, segmentStart_}); segmentStart_ = false;
            if (history_.size() > observation::LiveAcquisition::maxPositions) { history_.pop_front(); ++evicted_; }
        }
        if (s.persistenceSteps) while (!history_.empty() &&
            static_cast<std::uint64_t>(frame.steps.back()-history_.front().step) >= s.persistenceSteps) { history_.pop_front(); ++evicted_; }
        if (history_.empty()) return fail(PairResult::State::Unavailable, "No finite same-step X/Y pairs; invalid coverage is not interpolated.");
        history_.front().segmentStart = true;
        r.points.assign(history_.begin(), history_.end()); r.invalid = invalid_; r.gaps = gaps_; r.evicted = evicted_;
        r.firstStep = r.points.front().step;
        r.xMin = r.xMax = r.points.front().x; r.yMin = r.yMax = r.points.front().y;
        for (const auto& p : r.points) { r.xMin = std::min(r.xMin, p.x); r.xMax = std::max(r.xMax, p.x); r.yMin = std::min(r.yMin, p.y); r.yMax = std::max(r.yMax, p.y); }
        widen(r.xMin, r.xMax); widen(r.yMin, r.yMax);
        if (!std::isfinite(r.xMax-r.xMin) || !std::isfinite(r.yMax-r.yMin))
            return fail(PairResult::State::Unavailable, "XY range overflow; raw pairs cannot be plotted at this scale.");
        r.density.assign(s.densitySize*s.densitySize, 0);
        for (const auto& p : r.points) {
            const auto x = std::min(s.densitySize-1, static_cast<std::size_t>((p.x-r.xMin)/(r.xMax-r.xMin)*static_cast<double>(s.densitySize)));
            const auto y = std::min(s.densitySize-1, static_cast<std::size_t>((p.y-r.yMin)/(r.yMax-r.yMin)*static_cast<double>(s.densitySize)));
            auto& count = r.density[y*s.densitySize+x]; if (count != std::numeric_limits<std::uint32_t>::max()) ++count;
        }
        r.state = PairResult::State::Ready; return r;
    }
    const auto n = s.size;
    if (n < 64 || n > 65536 || (n & (n-1)) || s.maxLag >= n)
        return fail(PairResult::State::Unavailable, "Correlation needs power-of-two N=64–65536 and max lag < N.");
    if (frame.steps.size() < n) return fail(PairResult::State::Filling, QStringLiteral("Filling: %1/%2 consecutive samples.").arg(frame.steps.size()).arg(n));
    const auto offset = frame.steps.size()-n;
    r.firstStep = frame.steps[offset];
    double scaleA = 0, scaleB = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if ((i % 1024 == 0) && stop()) return fail(PairResult::State::Cancelled, "Cancelled");
        if (frame.steps[offset+i] != r.firstStep+static_cast<std::int64_t>(i))
            return fail(PairResult::State::Unavailable, "Correlation requires consecutive steps; gaps are not zero-filled.");
        const double a = frame.values[(offset+i)*width+s.first], b = frame.values[(offset+i)*width+second];
        if (!std::isfinite(a) || !std::isfinite(b)) return fail(PairResult::State::Unavailable, "Correlation interval contains nonfinite samples.");
        scaleA = std::max(scaleA, std::abs(a)); scaleB = std::max(scaleB, std::abs(b));
    }
    if (scaleA == 0) scaleA = 1;
    if (scaleB == 0) scaleB = 1;
    std::size_t padded = 1; while (padded < 2*n-1) padded *= 2;
    std::vector<std::complex<double>> a(padded), b(padded);
    Sum meanA, meanB, energyA, energyB;
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = frame.values[(offset+i)*width+s.first]/scaleA; b[i] = frame.values[(offset+i)*width+second]/scaleB;
        meanA.add(a[i].real()/static_cast<double>(n)); meanB.add(b[i].real()/static_cast<double>(n));
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (s.removeMean) { a[i] -= meanA.value; b[i] -= meanB.value; }
        energyA.add(a[i].real()*a[i].real()); energyB.add(b[i].real()*b[i].real());
    }
    const double coefficientDivisor = std::sqrt(energyA.value)*std::sqrt(energyB.value);
    if (s.normalization == CorrelationNormalization::Coefficient && coefficientDivisor == 0)
        return fail(PairResult::State::Unavailable, "Coefficient is undefined for zero-energy inputs (after optional mean removal).");
    auto fa = fft_.transform(a); if (stop()) return fail(PairResult::State::Cancelled, "Cancelled");
    auto fb = fft_.transform(b); if (stop()) return fail(PairResult::State::Cancelled, "Cancelled");
    for (std::size_t i = 0; i < padded; ++i) fa[i] = std::conj(fa[i])*fb[i];
    auto correlation = fft_.transform(fa, true);
    if (stop()) return fail(PairResult::State::Cancelled, "Cancelled");
    int expA, expB;
    const double mantissa = std::frexp(scaleA, &expA)*std::frexp(scaleB, &expB);
    const auto maxLag = static_cast<int>(s.maxLag);
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        const auto overlap = n-static_cast<std::size_t>(std::abs(lag));
        const auto index = lag < 0 ? padded-static_cast<std::size_t>(-lag) : static_cast<std::size_t>(lag);
        double value = correlation[index].real();
        switch (s.normalization) {
        case CorrelationNormalization::Coefficient: value /= coefficientDivisor; break;
        case CorrelationNormalization::Biased: value = std::scalbn((value/static_cast<double>(n))*mantissa, expA+expB); break;
        case CorrelationNormalization::Unbiased: value = std::scalbn((value/static_cast<double>(overlap))*mantissa, expA+expB); break;
        case CorrelationNormalization::Raw: value = std::scalbn(value*mantissa, expA+expB); break;
        }
        if (!std::isfinite(value)) return fail(PairResult::State::Unavailable, "Correlation normalization overflow.");
        r.lags.push_back(lag); r.correlations.push_back(value);
        const auto score = s.absolutePeak ? std::abs(value) : value;
        const auto best = s.absolutePeak ? std::abs(r.correlations[r.peakIndex]) : r.correlations[r.peakIndex];
        const auto tolerance = 64*std::numeric_limits<double>::epsilon()*std::max(std::abs(score), std::abs(best));
        const auto bestLag = r.lags[r.peakIndex];
        if (score > best+tolerance || (std::abs(score-best) <= tolerance &&
            (std::abs(lag) < std::abs(bestLag) || (std::abs(lag) == std::abs(bestLag) && lag < bestLag)))) r.peakIndex = r.lags.size()-1;
    }
    r.state = PairResult::State::Ready; return r;
}
}
