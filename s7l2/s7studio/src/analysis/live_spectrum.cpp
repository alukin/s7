// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/live_spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace s7studio::analysis {
namespace {
constexpr double kFloorDb = -200;

double decibels(double amplitude)
{
    return amplitude > 0 ? std::max(kFloorDb, 20 * std::log10(amplitude)) : kFloorDb;
}
} // namespace

void LiveSpectrum::barEdges(const LiveSpectrumSettings& settings, double sampleRate, double binSpacing,
                            std::vector<double>& low, std::vector<double>& high)
{
    const int bars = std::clamp(settings.bars, 1, 512);
    low.assign(static_cast<std::size_t>(bars), 0);
    high.assign(static_cast<std::size_t>(bars), 0);
    if (settings.iq || !settings.logFrequency) {
        const double start = settings.iq ? -sampleRate / 2 : 0.0;
        const double width = (sampleRate / 2 - start) / bars;
        for (int i = 0; i < bars; ++i) {
            low[static_cast<std::size_t>(i)] = start + i * width;
            high[static_cast<std::size_t>(i)] = start + (i + 1) * width;
        }
        return;
    }
    // Below a few bins the bands would hold no bin at all.
    const double nyquist = sampleRate / 2;
    const double bottom = std::min(std::max(settings.minimumHz, 1.5 * binSpacing), nyquist / 2);
    const double ratio = std::pow(nyquist / bottom, 1.0 / bars);
    for (int i = 0; i < bars; ++i) {
        low[static_cast<std::size_t>(i)] = bottom * std::pow(ratio, i);
        high[static_cast<std::size_t>(i)] = bottom * std::pow(ratio, i + 1);
    }
}

std::optional<LiveSpectrumResult> LiveSpectrum::compute(const observation::LiveFrame& frame, const LiveSpectrumSettings& settings)
{
    const auto channels = static_cast<std::size_t>(frame.channels.size());
    if (frame.timeEnvelope || channels == 0 || (settings.iq && channels < 2) || !frame.rateNumerator || !frame.rateDenominator)
        return std::nullopt;
    const std::size_t available = frame.steps.size();
    std::size_t n = 1;
    while (n * 2 <= std::min<std::size_t>(available, static_cast<std::size_t>(std::max(64, settings.fftSize)))) n *= 2;
    if (n < 64) return std::nullopt;

    LiveSpectrumResult result;
    result.sampleRate = static_cast<double>(frame.rateNumerator) / static_cast<double>(frame.rateDenominator);
    result.binSpacing = result.sampleRate / static_cast<double>(n);
    if (window_.size() != n) {
        window_.resize(n);
        for (std::size_t i = 0; i < n; ++i) window_[i] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
    }
    double gain = 0;
    for (const auto w : window_) gain += w;

    input_.resize(n);
    const std::size_t first = available - n;
    double mean = 0;
    if (!settings.iq) {
        for (std::size_t i = 0; i < n; ++i) mean += frame.values[(first + i) * channels];
        mean /= static_cast<double>(n);  // DC would otherwise leak into the lowest bars
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto row = (first + i) * channels;
        const double re = frame.values[row] - mean;
        const double im = settings.iq ? frame.values[row + 1] : 0.0;
        input_[i] = {re * window_[i], im * window_[i]};
    }
    const auto spectrum = fft_.transform(input_);

    if (settings.iq) {
        result.frequencies.resize(n);
        result.decibels.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto bin = (i + (n + 1) / 2) % n;  // fftshift: -fs/2 .. fs/2
            result.frequencies[i] = (static_cast<double>(i) - static_cast<double>(n / 2)) * result.binSpacing;
            result.decibels[i] = decibels(std::abs(spectrum[bin]) / gain);
        }
    } else {
        const std::size_t half = n / 2 + 1;
        result.frequencies.resize(half);
        result.decibels.resize(half);
        for (std::size_t i = 0; i < half; ++i) {
            result.frequencies[i] = static_cast<double>(i) * result.binSpacing;
            const double scale = i == 0 || i == n / 2 ? 1.0 : 2.0;  // one-sided
            result.decibels[i] = decibels(scale * std::abs(spectrum[i]) / gain);
        }
    }

    barEdges(settings, result.sampleRate, result.binSpacing, result.barLow, result.barHigh);
    result.barDecibels.assign(result.barLow.size(), kFloorDb);
    const double first_frequency = result.frequencies.front();
    for (std::size_t b = 0; b < result.barLow.size(); ++b) {
        double level = kFloorDb;
        bool any = false;
        for (std::size_t i = 0; i < result.frequencies.size(); ++i) {
            if (result.frequencies[i] >= result.barLow[b] && result.frequencies[i] < result.barHigh[b]) {
                level = std::max(level, result.decibels[i]);
                any = true;
            }
        }
        if (!any) {
            // A band narrower than a bin takes its nearest bin.
            const double centre = (result.barLow[b] + result.barHigh[b]) / 2;
            const auto index = static_cast<std::size_t>(std::clamp(std::lround((centre - first_frequency) / result.binSpacing), 0L,
                static_cast<long>(result.frequencies.size()) - 1));
            level = result.decibels[index];
        }
        result.barDecibels[b] = level;
    }
    return result;
}

} // namespace s7studio::analysis
