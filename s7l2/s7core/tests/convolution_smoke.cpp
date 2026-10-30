// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/convolution.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Complex = std::complex<double>;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void require_close(std::span<const Complex> actual, std::span<const Complex> expected,
                   std::string_view context) {
    require(actual.size() == expected.size(), "Convolution output size differs from reference.");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const double tolerance = 2e-10 * std::max({1.0, std::abs(actual[i]), std::abs(expected[i])});
        if (std::abs(actual[i] - expected[i]) > tolerance) {
            throw std::runtime_error(std::string(context) + " sample " + std::to_string(i));
        }
    }
}

std::vector<Complex> direct(std::span<const Complex> input, std::span<const Complex> impulse) {
    std::vector<Complex> output(input.size() + impulse.size() - 1);
    for (std::size_t n = 0; n < output.size(); ++n) {
        for (std::size_t k = 0; k < impulse.size(); ++k) {
            if (n >= k && n - k < input.size()) output[n] += impulse[k] * input[n - k];
        }
    }
    return output;
}

std::vector<Complex> values(std::size_t count, double phase) {
    std::vector<Complex> result(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double x = static_cast<double>(i);
        result[i] = {std::sin(phase * x) + 0.11 * x, std::cos((phase + 0.07) * x) - 0.02 * x};
    }
    return result;
}

void frame_cases() {
    for (const std::size_t frame_size : {1U, 2U, 7U, 64U}) {
        for (const std::size_t impulse_size : {1U, 3U, 17U, 129U}) {
            const auto impulse = values(impulse_size, 0.031);
            s7::FirConvolution fir{frame_size, impulse};
            s7::FftConvolution fft{frame_size, impulse};
            require(fir.output_size() == frame_size + impulse_size - 1, "FIR full output size.");
            require(fft.output_size() == fir.output_size(), "FFT full output size.");
            for (std::size_t frame_index = 0; frame_index < 3; ++frame_index) {
                auto frame = values(frame_size, 0.1 + 0.03 * static_cast<double>(frame_index));
                const auto expected = direct(frame, impulse);
                require_close(fir.step(frame), expected, "FIR independent frame");
                require_close(fft.step(frame), expected, "FFT independent frame");
            }
        }
    }
}

void stream_cases() {
    for (const std::size_t frame_size : {1U, 3U, 8U, 64U}) {
        for (const std::size_t impulse_size : {1U, 5U, 19U, 129U}) {
            const auto impulse = values(impulse_size, 0.047);
            s7::FirConvolution fir{frame_size, impulse, s7::ConvolutionMode::ContinuousStream};
            s7::FftConvolution fft{frame_size, impulse, s7::ConvolutionMode::ContinuousStream};
            require(fir.output_size() == frame_size, "FIR continuous output size.");
            require(fft.output_size() == frame_size, "FFT continuous output size.");
            std::vector<Complex> accumulated;
            for (std::size_t frame_index = 0; frame_index < 4; ++frame_index) {
                const auto frame = values(frame_size, 0.08 + 0.05 * static_cast<double>(frame_index));
                accumulated.insert(accumulated.end(), frame.begin(), frame.end());
                const auto all = direct(accumulated, impulse);
                const auto begin = all.begin() + static_cast<std::ptrdiff_t>(frame_index * frame_size);
                const std::span<const Complex> expected{begin, frame_size};
                require_close(fir.step(frame), expected, "FIR continuous frame");
                require_close(fft.step(frame), expected, "FFT continuous frame");
            }
            const auto first = values(frame_size, 0.19);
            fir.reset();
            fft.reset();
            const auto expected = direct(first, impulse);
            require_close(fir.step(first), std::span<const Complex>{expected.data(), frame_size}, "FIR reset");
            require_close(fft.step(first), std::span<const Complex>{expected.data(), frame_size}, "FFT reset");
        }
    }
}

void matched_filter() {
    const std::vector<Complex> pulse{{1.0, 0.2}, {0.1, 0.8}, {-0.7, 0.1}, {0.4, -0.5}};
    std::vector<Complex> frame(32);
    constexpr std::size_t delay = 9;
    std::copy(pulse.begin(), pulse.end(), frame.begin() + delay);
    std::vector<Complex> impulse(pulse.size());
    for (std::size_t i = 0; i < pulse.size(); ++i)
        impulse[i] = std::conj(pulse[pulse.size() - 1 - i]);
    s7::FirConvolution fir{frame.size(), impulse};
    s7::FftConvolution fft{frame.size(), impulse};
    for (const auto output : {fir.step(frame), fft.step(frame)}) {
        const auto peak = std::max_element(output.begin(), output.end(),
                                           [](Complex a, Complex b) { return std::norm(a) < std::norm(b); });
        require(static_cast<std::size_t>(peak - output.begin()) == delay + pulse.size() - 1,
                "Matched-filter peak has wrong index.");
    }
}

void buffered_radar_case() {
    constexpr std::size_t frame_size = 2048;
    constexpr std::size_t impulse_size = 1025;
    std::vector<Complex> impulse(impulse_size);
    std::vector<Complex> frame(frame_size);
    for (std::size_t i = 0; i < impulse_size; ++i) {
        const double index = static_cast<double>(i);
        impulse[i] = {std::sin(0.013 * index), std::cos(0.019 * index)};
    }
    for (std::size_t i = 0; i < frame_size; ++i) {
        const double index = static_cast<double>(i);
        frame[i] = {std::sin(0.037 * index), std::cos(0.041 * index)};
    }
    const auto expected = direct(frame, impulse);
    s7::FirConvolution fir{frame_size, impulse};
    s7::FftConvolution fft{frame_size, impulse};
    require_close(fir.step(frame), expected, "FIR buffered frame");
    require_close(fft.step(frame), expected, "FFT buffered frame");

    s7::FirConvolution stream_fir{frame_size, impulse, s7::ConvolutionMode::ContinuousStream};
    s7::FftConvolution stream_fft{frame_size, impulse, s7::ConvolutionMode::ContinuousStream};
    require_close(stream_fir.step(frame), std::span<const Complex>{expected.data(), frame_size},
                  "FIR buffered stream");
    require_close(stream_fft.step(frame), std::span<const Complex>{expected.data(), frame_size},
                  "FFT buffered stream");
}

void invalid_inputs() {
    const auto impulse = values(3, 0.1);
    for (const bool fft : {false, true}) {
        bool rejected = false;
        try {
            if (fft) s7::FftConvolution invalid{0, impulse};
            else s7::FirConvolution invalid{0, impulse};
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Zero frame size rejected.");
        rejected = false;
        try {
            if (fft) s7::FftConvolution invalid{4, {}};
            else s7::FirConvolution invalid{4, {}};
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Empty impulse rejected.");
        rejected = false;
        try {
            const auto invalid_mode = static_cast<s7::ConvolutionMode>(255);
            if (fft) s7::FftConvolution invalid{4, impulse, invalid_mode};
            else s7::FirConvolution invalid{4, impulse, invalid_mode};
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Unknown mode rejected.");
        rejected = false;
        try {
            if (fft) {
                s7::FftConvolution valid{4, impulse};
                static_cast<void>(valid.step(values(3, 0.1)));
            } else {
                s7::FirConvolution valid{4, impulse};
                static_cast<void>(valid.step(values(3, 0.1)));
            }
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Wrong input frame size rejected.");
    }
}

} // namespace

int main() {
    try {
        frame_cases();
        stream_cases();
        matched_filter();
        buffered_radar_case();
        invalid_inputs();
        std::cout << "FIR and FFT convolution: independent/continuous frames, reset, matched filter and invalid inputs passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
