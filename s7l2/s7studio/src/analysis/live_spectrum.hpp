// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/fft.hpp"
#include "observation/live_acquisition.hpp"

#include <optional>
#include <vector>

namespace s7studio::analysis {

// Settings of the simple live spectrum viewer (LiveSpectrum observer).
struct LiveSpectrumSettings {
    int fftSize = 4096;     // power of two; fewer retained samples use the largest power of two available
    int bars = 32;          // bar count for the bar style
    bool iq = false;        // channels 0 and 1 are I and Q: two-sided spectrum centred on 0 Hz
    double minimumHz = 20;  // lowest bar edge of a real (one-sided) spectrum
    bool logFrequency = true;  // log-spaced bars for a real spectrum; I/Q is always linear
};

// Amplitude spectrum in dB relative to full scale: a full-scale sine
// (amplitude 1) reads 0 dBFS in its bin, after Hann-window coherent-gain
// correction. Bars hold the strongest bin in their band, as hardware
// analyzers show band peaks.
struct LiveSpectrumResult {
    double sampleRate = 0;
    double binSpacing = 0;
    std::vector<double> frequencies;  // bin centres (Hz), ascending; I/Q is two-sided
    std::vector<double> decibels;     // per bin
    std::vector<double> barLow, barHigh, barDecibels;  // bar band edges (Hz) and levels
};

class LiveSpectrum {
public:
    // Uses the newest samples of channel 0 (and 1 for I/Q). Empty when the
    // frame has no rate, too few samples (< 64) or a time envelope.
    std::optional<LiveSpectrumResult> compute(const observation::LiveFrame& frame, const LiveSpectrumSettings& settings);

    // Bar band edges: a real spectrum is log spaced from minimumHz to Nyquist
    // (or linear from 0 with logFrequency off); I/Q is linear across -fs/2..fs/2.
    static void barEdges(const LiveSpectrumSettings& settings, double sampleRate, double binSpacing,
                         std::vector<double>& low, std::vector<double>& high);

private:
    Fft fft_;
    std::vector<std::complex<double>> input_;
    std::vector<double> window_;
};

} // namespace s7studio::analysis
