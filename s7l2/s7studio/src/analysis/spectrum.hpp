// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/fft.hpp"
#include "observation/live_acquisition.hpp"
#include <functional>

namespace s7studio::analysis {
enum class SpectrumWindow { Rectangular, Hann, Hamming, Blackman, BlackmanHarris };
enum class SpectrumQuantity { Amplitude, Power, Psd };
enum class SpectrumAverage { None, Block, Exponential };
struct SpectrumSettings {
    std::size_t size = 4096, channel = 0, quadrature = 1;
    bool iq = false, removeMean = false, peakHold = false;
    QString declaredUnit;
    SpectrumWindow window = SpectrumWindow::Hann;
    SpectrumQuantity quantity = SpectrumQuantity::Amplitude;
    SpectrumAverage average = SpectrumAverage::None;
    std::size_t averageCount = 8;
    double alpha = .2;
    bool operator==(const SpectrumSettings&) const = default;
};
struct SpectrumResult {
    enum class State { Ready, Filling, Unavailable, Cancelled } state = State::Unavailable;
    QString message, unit;
    std::uint64_t runId = 0, generation = 0, revision = 0, skippedHops = 0;
    std::int64_t firstStep = 0, lastStep = 0;
    std::int64_t averageFirstStep = 0, holdFirstStep = 0;
    std::size_t size = 0, averaged = 0;
    double sampleRate = 0, binSpacing = 0, coherentGain = 0, enbw = 0;
    bool reused = false; // No new model hop; retain the preceding result's raw owner.
    SpectrumSettings settings;
    std::shared_ptr<const observation::LiveFrame> frame; // Worker result owns its exact bounded input.
    std::vector<double> frequencies, values, peakHold;
};
std::vector<double> spectrumWindow(SpectrumWindow window, std::size_t size);
// Ref is in the chosen linear quantity's units. Zero maps to -infinity;
// flooring/offset belong exclusively to presentation.
double spectrumDecibels(double value, SpectrumQuantity quantity, double reference);

// Called serially on an analysis worker. Keeps bounded O(N) state; no recording,
// acquisition mutation, GUI objects or FFTW state shared with runtime plugins.
class SpectrumAnalyzer {
public:
    SpectrumResult analyze(const observation::LiveFrame& frame, const SpectrumSettings& settings,
                           const std::function<bool()>& cancelled = {});
    void reset();
private:
    Fft fft_;
    std::optional<SpectrumSettings> settings_;
    std::uint64_t run_ = 0, generation_ = 0, skipped_ = 0;
    std::int64_t end_ = 0;
    std::int64_t averageStart_ = 0, holdStart_ = 0;
    std::uint64_t rateNumerator_ = 0, rateDenominator_ = 0;
    std::size_t averaged_ = 0;
    std::vector<double> average_, hold_;
    std::optional<SpectrumResult> previous_;
};
}
