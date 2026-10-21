// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_SPECTRAL_TYPES_HPP
#define S7CORE_INCLUDE_S7_SPECTRAL_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <complex>
#include <chrono>

namespace s7 {

// Spectral data type for frequency domain analysis
// Used by FFT elements to provide waterfall visualization data

struct SpectralFrame {
    // Frequency domain data (complex values)
    std::vector<std::complex<float>> bins;

    // Metadata
    struct Metadata {
        // FFT parameters
        uint32_t fft_size;                  // FFT window size (number of samples)
        float sample_rate_hz;               // Sample rate in Hz
        uint32_t hop_size;                  // Samples between windows (overlap)

        // Window function used
        enum class WindowType : uint8_t {
            HANN,
            HAMMING,
            BLACKMAN,
            RECTANGULAR
        };
        WindowType window_type;

        // Frame timing
        std::chrono::system_clock::time_point timestamp;
        uint64_t frame_number;              // Sequential frame counter

        // For visualization
        float freq_resolution_hz;           // Hz per bin
        float nyquist_hz;                   // Maximum frequency (sample_rate/2)
    };

    Metadata metadata;

    // Helper to get frequency of bin
    [[nodiscard]] float bin_frequency(size_t bin_index) const {
        return bin_index * metadata.freq_resolution_hz;
    }

    // Helper to get magnitude of bin
    [[nodiscard]] float bin_magnitude(size_t bin_index) const {
        if (bin_index >= bins.size()) return 0.0f;
        return std::abs(bins[bin_index]);
    }

    // Helper to get magnitude in dB
    [[nodiscard]] float bin_magnitude_db(size_t bin_index, float reference = 1.0f) const {
        float mag = bin_magnitude(bin_index);
        if (mag < 1e-10f) return -200.0f;  // Floor at -200 dB
        return 20.0f * std::log10(mag / reference);
    }
};

// Streaming spectral data for waterfall display
struct WaterfallData {
    // Multiple frames for visualization
    std::vector<SpectralFrame> frames;

    // Total frames accumulated
    uint64_t total_frames;

    // Time range covered
    struct TimeRange {
        std::chrono::system_clock::time_point start;
        std::chrono::system_clock::time_point end;
    };
    TimeRange time_range;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SPECTRAL_TYPES_HPP

