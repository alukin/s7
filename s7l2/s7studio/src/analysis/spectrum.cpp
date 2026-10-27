// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/spectrum.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace s7studio::analysis {
namespace {
struct Sum {
    double value = 0, correction = 0;
    void add(double next) { const double y = next-correction, t = value+y; correction = (t-value)-y; value = t; }
};
}
std::vector<double> spectrumWindow(SpectrumWindow window, std::size_t size)
{
    if (size < 64 || size > 65536) throw std::invalid_argument("FFT size must be from 64 to 65536.");
    std::vector<double> weights(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double phase = 2*std::numbers::pi*static_cast<double>(i)/static_cast<double>(size);
        switch (window) {
        case SpectrumWindow::Rectangular: weights[i] = 1; break;
        case SpectrumWindow::Hann: weights[i] = .5-.5*std::cos(phase); break;
        case SpectrumWindow::Hamming: weights[i] = .54-.46*std::cos(phase); break;
        case SpectrumWindow::Blackman: weights[i] = .42-.5*std::cos(phase)+.08*std::cos(2*phase); break;
        case SpectrumWindow::BlackmanHarris: weights[i] = .35875-.48829*std::cos(phase)+.14128*std::cos(2*phase)-.01168*std::cos(3*phase); break;
        default: throw std::invalid_argument("Unknown FFT window.");
        }
    }
    return weights;
}
double spectrumDecibels(double value, SpectrumQuantity quantity, double reference)
{
    if (!(reference > 0) || !std::isfinite(reference) || value < 0 || !std::isfinite(value))
        throw std::invalid_argument("dB requires a finite nonnegative quantity and a positive finite reference.");
    if (value == 0) return -std::numeric_limits<double>::infinity();
    // Subtract logs: the ratio itself can overflow/underflow unnecessarily.
    return (quantity == SpectrumQuantity::Amplitude ? 20 : 10)*(std::log10(value)-std::log10(reference));
}
void SpectrumAnalyzer::reset()
{
    settings_.reset(); previous_.reset(); end_ = 0; averaged_ = 0; skipped_ = 0;
    average_.clear(); hold_.clear();
}
SpectrumResult SpectrumAnalyzer::analyze(const observation::LiveFrame& frame, const SpectrumSettings& settings,
                                        const std::function<bool()>& cancelled)
{
    SpectrumResult result;
    result.runId = frame.runId; result.generation = frame.generation; result.settings = settings; result.size = settings.size;
    auto stop = [&] { return cancelled && cancelled(); };
    auto fail = [&](SpectrumResult::State state, const QString& message) {
        result.state = state; result.message = message;
        if (state != SpectrumResult::State::Cancelled) reset();
        return result;
    };
    const auto n = settings.size;
    if (frame.timeEnvelope) return fail(SpectrumResult::State::Unavailable, "Time display envelopes are not full-rate FFT input.");
    const auto width = static_cast<std::size_t>(frame.channels.size());
    if (n < 64 || n > 65536)
        return fail(SpectrumResult::State::Unavailable, "FFT size must be from 64 to 65536.");
    if (settings.channel >= width || (settings.iq && (settings.quadrature >= width || settings.channel == settings.quadrature)))
        return fail(SpectrumResult::State::Unavailable, "Select a source channel, or two different I/Q channels.");
    if (!frame.rateNumerator || !frame.rateDenominator)
        return fail(SpectrumResult::State::Unavailable, "Spectrum requires a known rational model sample rate.");
    if (settings.averageCount < 1 || settings.averageCount > 128 || !(settings.alpha > 0 && settings.alpha <= 1) ||
        (settings.quantity == SpectrumQuantity::Amplitude && settings.average != SpectrumAverage::None))
        return fail(SpectrumResult::State::Unavailable, "Averaging requires power/PSD, K=1–128 and 0 < alpha <= 1.");
    result.unit = frame.channels[static_cast<qsizetype>(settings.channel)].unit.trimmed();
    if (settings.iq) {
        const auto qUnit = frame.channels[static_cast<qsizetype>(settings.quadrature)].unit.trimmed();
        if (!settings.declaredUnit.trimmed().isEmpty()) result.unit = settings.declaredUnit.trimmed();
        else if (result.unit.isEmpty() || qUnit != result.unit)
            return fail(SpectrumResult::State::Unavailable, "I/Q requires matching supplied units or an explicit I/Q unit declaration.");
    }
    if (frame.steps.size() != frame.times.size() || frame.values.size() != frame.steps.size()*width)
        return fail(SpectrumResult::State::Unavailable, "Inconsistent acquisition frame.");
    if (frame.steps.size() < n)
        return fail(SpectrumResult::State::Filling, QStringLiteral("Filling: %1/%2 raw samples. Increase retention if it is below N.").arg(frame.steps.size()).arg(n));
    const bool changed = !settings_ || *settings_ != settings || run_ != frame.runId || generation_ != frame.generation ||
        rateNumerator_ != frame.rateNumerator || rateDenominator_ != frame.rateDenominator;
    if (changed) {
        reset(); settings_ = settings; run_ = frame.runId; generation_ = frame.generation;
        rateNumerator_ = frame.rateNumerator; rateDenominator_ = frame.rateDenominator;
    }
    const auto hop = static_cast<std::int64_t>(n/2);
    auto last = frame.steps.back();
    if (previous_ && last >= end_ && last-end_ < hop) {
        auto cached = *previous_; cached.reused = true; return cached; // One contribution per hop, never per repaint.
    }
    if (previous_ && last > end_) {
        const auto hops = (last-end_)/hop;
        const auto alignedEnd = end_ + hops*hop;
        // Use a hop-aligned window if it is still retained. At minimum retention
        // it may have expired; use the newest complete window and reset averaging.
        if (alignedEnd-static_cast<std::int64_t>(n)+1 >= frame.steps.front()) last = alignedEnd;
        if (last-end_ != hop) {
            skipped_ += static_cast<std::uint64_t>((last-end_+hop-1)/hop-1);
            averaged_ = 0; average_.clear(); hold_.clear();
        }
    } else if (previous_ && last < end_) {
        reset(); settings_ = settings; // History/config requests cannot reuse a future average.
    }
    const auto first = last-static_cast<std::int64_t>(n)+1;
    const auto it = std::lower_bound(frame.steps.begin(), frame.steps.end(), first);
    const auto offset = static_cast<std::size_t>(it-frame.steps.begin());
    if (it == frame.steps.end() || *it != first || offset+n > frame.steps.size())
        return fail(SpectrumResult::State::Unavailable, "FFT window contains a gap; no resampling or zero filling is applied.");
    std::vector<std::complex<double>> input(n);
    Sum meanI, meanQ;
    for (std::size_t i = 0; i < n; ++i) {
        if ((i % 1024 == 0) && stop()) return fail(SpectrumResult::State::Cancelled, "Cancelled");
        if (frame.steps[offset+i] != first+static_cast<std::int64_t>(i))
            return fail(SpectrumResult::State::Unavailable, "FFT window contains a gap; no resampling or zero filling is applied.");
        const double real = frame.values[(offset+i)*width+settings.channel];
        const double imag = settings.iq ? frame.values[(offset+i)*width+settings.quadrature] : 0;
        if (!std::isfinite(real) || !std::isfinite(imag))
            return fail(SpectrumResult::State::Unavailable, "FFT window contains nonfinite samples.");
        input[i] = {real, imag};
        meanI.add(real/static_cast<double>(n)); meanQ.add(imag/static_cast<double>(n));
    }
    auto weights = spectrumWindow(settings.window, n);
    Sum s1, s2;
    for (std::size_t i = 0; i < n; ++i) {
        if (settings.removeMean) input[i] -= std::complex<double>(meanI.value, meanQ.value);
        input[i] *= weights[i]; s1.add(weights[i]); s2.add(weights[i]*weights[i]);
    }
    auto transformed = fft_.transform(input);
    if (stop()) return fail(SpectrumResult::State::Cancelled, "Cancelled");
    result.firstStep = first; result.lastStep = last;
    result.sampleRate = static_cast<double>(frame.rateNumerator)/static_cast<double>(frame.rateDenominator);
    result.binSpacing = result.sampleRate/static_cast<double>(n);
    result.coherentGain = s1.value/static_cast<double>(n);
    result.enbw = (s2.value/s1.value)*(result.sampleRate/s1.value);
    const auto bins = settings.iq ? n : n/2+1;
    result.frequencies.resize(bins); result.values.resize(bins);
    for (std::size_t i = 0; i < bins; ++i) {
        const auto bin = settings.iq ? (i+(n+1)/2)%n : i;
        const double factor = !settings.iq && bin != 0 && !(n % 2 == 0 && bin == n/2) ? 2 : 1;
        const double magnitude = std::abs(transformed[bin]);
        double value;
        if (settings.quantity == SpectrumQuantity::Amplitude) value = (magnitude/s1.value)*factor;
        else {
            const double denominator = settings.quantity == SpectrumQuantity::Power ? static_cast<double>(n) : result.sampleRate;
            const double rms = (magnitude/std::sqrt(s2.value))/std::sqrt(denominator);
            value = rms*rms*factor;
        }
        if (!std::isfinite(value)) return fail(SpectrumResult::State::Unavailable, "FFT/normalization overflow; select another interval or quantity.");
        result.values[i] = value;
        result.frequencies[i] = (settings.iq ? static_cast<double>(i)-static_cast<double>(n/2) : static_cast<double>(i))*result.binSpacing;
    }
    if (settings.average == SpectrumAverage::Block) {
        if (averaged_ == settings.averageCount) { average_.clear(); averaged_ = 0; }
        if (!averaged_) averageStart_ = first;
        ++averaged_;
        if (average_.empty()) average_ = result.values;
        else for (std::size_t i = 0; i < bins; ++i)
            average_[i] = average_[i]*(static_cast<double>(averaged_-1)/static_cast<double>(averaged_)) + result.values[i]/static_cast<double>(averaged_);
        result.values = average_;
    } else if (settings.average == SpectrumAverage::Exponential) {
        if (!averaged_) averageStart_ = first;
        ++averaged_;
        if (average_.empty()) average_ = result.values;
        else for (std::size_t i = 0; i < bins; ++i) average_[i] = average_[i]*(1-settings.alpha)+result.values[i]*settings.alpha;
        result.values = average_;
    } else { averaged_ = 1; averageStart_ = first; }
    if (settings.peakHold) {
        if (hold_.empty()) { hold_ = result.values; holdStart_ = averageStart_; }
        else for (std::size_t i = 0; i < bins; ++i) hold_[i] = std::max(hold_[i], result.values[i]);
        result.peakHold = hold_;
    }
    result.averaged = averaged_; result.skippedHops = skipped_;
    result.averageFirstStep = averageStart_; result.holdFirstStep = settings.peakHold ? holdStart_ : 0;
    result.state = SpectrumResult::State::Ready;
    result.message = skipped_ ? "Analysis hops skipped; averaging/peak hold restart after each discontinuity." : QString();
    end_ = last; previous_ = result;
    return result;
}
}
