// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Opt-in buffered workload for evaluating branch scheduling. S7 ports do not
// yet carry frames, so this is an algorithm prototype, not a runtime element.

#include <fftw3.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

class BufferedFftConvolution {
public:
    using Complex = std::complex<double>;

    BufferedFftConvolution(std::size_t frame_size, std::span<const Complex> impulse)
        : frame_size_(frame_size)
    {
        constexpr std::size_t max_fft_size = 1U << 30;
        if (frame_size == 0 || impulse.empty() || frame_size > max_fft_size ||
            impulse.size() > max_fft_size || frame_size + impulse.size() - 1 > max_fft_size)
            throw std::invalid_argument("FFT convolution needs nonempty buffers within FFTW's size limit.");
        output_.resize(frame_size + impulse.size() - 1);
        fft_size_ = std::bit_ceil(output_.size());
        try {
            input_ = fftw_alloc_complex(fft_size_);
            spectrum_ = fftw_alloc_complex(fft_size_);
            product_ = fftw_alloc_complex(fft_size_);
            inverse_output_ = fftw_alloc_complex(fft_size_);
            if (!input_ || !spectrum_ || !product_ || !inverse_output_) throw std::bad_alloc();
            // FFTW plans are created on the caller thread before workers run.
            forward_ = fftw_plan_dft_1d(static_cast<int>(fft_size_), input_, spectrum_, FFTW_FORWARD, FFTW_ESTIMATE);
            inverse_ = fftw_plan_dft_1d(static_cast<int>(fft_size_), product_, inverse_output_, FFTW_BACKWARD, FFTW_ESTIMATE);
            if (!forward_ || !inverse_) throw std::runtime_error("FFTW could not plan convolution.");
            clear_input();
            for (std::size_t i = 0; i < impulse.size(); ++i) {
                input_[i][0] = impulse[i].real();
                input_[i][1] = impulse[i].imag();
            }
            fftw_execute(forward_);
            kernel_spectrum_.reserve(fft_size_);
            for (std::size_t i = 0; i < fft_size_; ++i)
                kernel_spectrum_.emplace_back(spectrum_[i][0], spectrum_[i][1]);
        } catch (...) {
            release();
            throw;
        }
    }

    ~BufferedFftConvolution() { release(); }
    BufferedFftConvolution(const BufferedFftConvolution&) = delete;
    BufferedFftConvolution& operator=(const BufferedFftConvolution&) = delete;

    [[nodiscard]] std::span<const Complex> step(std::span<const Complex> frame) {
        if (frame.size() != frame_size_) throw std::invalid_argument("Convolution frame size changed.");
        clear_input();
        for (std::size_t i = 0; i < frame_size_; ++i) {
            input_[i][0] = frame[i].real();
            input_[i][1] = frame[i].imag();
        }
        fftw_execute(forward_);
        for (std::size_t i = 0; i < fft_size_; ++i) {
            const auto [hr, hi] = kernel_spectrum_[i];
            const double xr = spectrum_[i][0];
            const double xi = spectrum_[i][1];
            product_[i][0] = xr * hr - xi * hi;
            product_[i][1] = xr * hi + xi * hr;
        }
        fftw_execute(inverse_);
        const double scale = 1.0 / static_cast<double>(fft_size_);
        for (std::size_t i = 0; i < output_.size(); ++i)
            output_[i] = {inverse_output_[i][0] * scale, inverse_output_[i][1] * scale};
        return output_;
    }

private:
    void clear_input() {
        for (std::size_t i = 0; i < fft_size_; ++i) {
            input_[i][0] = 0.0;
            input_[i][1] = 0.0;
        }
    }

    void release() noexcept {
        if (forward_) fftw_destroy_plan(forward_);
        if (inverse_) fftw_destroy_plan(inverse_);
        if (input_) fftw_free(input_);
        if (spectrum_) fftw_free(spectrum_);
        if (product_) fftw_free(product_);
        if (inverse_output_) fftw_free(inverse_output_);
    }

    std::size_t frame_size_{};
    std::size_t fft_size_{};
    std::vector<Complex> output_;
    std::vector<Complex> kernel_spectrum_;
    fftw_complex* input_{};
    fftw_complex* spectrum_{};
    fftw_complex* product_{};
    fftw_complex* inverse_output_{};
    fftw_plan forward_{};
    fftw_plan inverse_{};
};

using Complex = std::complex<double>;

void require_close(Complex actual, Complex expected, const char* message) {
    if (std::abs(actual - expected) > 1e-9 * std::max({1.0, std::abs(actual), std::abs(expected)}))
        throw std::runtime_error(message);
}

void verify_convolution() {
    const std::vector<Complex> frame{{1.0, 0.5}, {-0.25, 0.2}, {0.75, -0.1}, {0.0, 0.3}, {0.4, 0.0}};
    const std::vector<Complex> impulse{{0.5, -0.2}, {-0.1, 0.4}, {0.3, 0.0}};
    BufferedFftConvolution fft{frame.size(), impulse};
    const auto actual = fft.step(frame);
    for (std::size_t n = 0; n < actual.size(); ++n) {
        Complex expected{};
        for (std::size_t k = 0; k < impulse.size(); ++k)
            if (n >= k && n - k < frame.size()) expected += impulse[k] * frame[n - k];
        require_close(actual[n], expected, "Complex FFT convolution differs from direct reference.");
    }

    // Matched-filter pulse compression: a delayed copy of the transmit pulse
    // peaks at delay + pulse_length - 1 after convolution with its reversed
    // conjugate. The test uses one isolated echo.
    const std::vector<Complex> pulse{{1.0, 0.2}, {0.1, 0.8}, {-0.7, 0.1}, {0.4, -0.5}};
    std::vector<Complex> echo(32);
    constexpr std::size_t delay = 9;
    std::copy(pulse.begin(), pulse.end(), echo.begin() + delay);
    std::vector<Complex> matched(pulse.size());
    for (std::size_t i = 0; i < pulse.size(); ++i)
        matched[i] = std::conj(pulse[pulse.size() - 1 - i]);
    BufferedFftConvolution matched_filter{echo.size(), matched};
    const auto compressed = matched_filter.step(echo);
    const auto peak = std::max_element(compressed.begin(), compressed.end(),
                                       [](Complex a, Complex b) { return std::norm(a) < std::norm(b); });
    if (static_cast<std::size_t>(peak - compressed.begin()) != delay + pulse.size() - 1)
        throw std::runtime_error("Matched-filter peak is at the wrong delay.");
}

std::vector<Complex> make_frame(std::size_t size) {
    std::vector<Complex> frame(size);
    for (std::size_t i = 0; i < size; ++i)
        frame[i] = {std::sin(0.017 * static_cast<double>(i)), std::cos(0.031 * static_cast<double>(i))};
    return frame;
}

std::vector<Complex> make_impulse(std::size_t size, double phase) {
    std::vector<Complex> impulse(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double x = static_cast<double>(i);
        impulse[i] = std::exp(-0.0004 * x) * Complex{std::cos(phase * x), std::sin(phase * x)};
    }
    return impulse;
}

struct TimedOutputs {
    double ns_per_frame{};
    std::vector<std::pair<Complex, Complex>> probes;
};

TimedOutputs run_serial(std::size_t frame_size, std::span<const Complex> h1,
                        std::span<const Complex> h2, std::size_t steps) {
    auto frame = make_frame(frame_size);
    BufferedFftConvolution left{frame_size, h1};
    BufferedFftConvolution right{frame_size, h2};
    TimedOutputs result;
    result.probes.reserve(steps);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < steps; ++i) {
        frame[0] = {static_cast<double>(i) / steps, -static_cast<double>(i) / steps};
        const auto a = left.step(frame);
        const auto b = right.step(frame);
        result.probes.emplace_back(a[frame_size / 2], b[frame_size / 2]);
    }
    result.ns_per_frame = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / steps;
    return result;
}

TimedOutputs run_parallel(std::size_t frame_size, std::span<const Complex> h1,
                          std::span<const Complex> h2, std::size_t steps) {
    auto frame = make_frame(frame_size);
    BufferedFftConvolution left{frame_size, h1};
    BufferedFftConvolution right{frame_size, h2};
    std::barrier start_gate{2};
    std::barrier done_gate{2};
    std::atomic<bool> stopping{false};
    std::jthread worker([&] {
        for (;;) {
            start_gate.arrive_and_wait();
            if (stopping.load()) break;
            static_cast<void>(right.step(frame));
            done_gate.arrive_and_wait();
        }
    });
    TimedOutputs result;
    result.probes.reserve(steps);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < steps; ++i) {
        frame[0] = {static_cast<double>(i) / steps, -static_cast<double>(i) / steps};
        start_gate.arrive_and_wait();
        const auto a = left.step(frame);
        done_gate.arrive_and_wait();
        const auto b = right.step(frame);
        result.probes.emplace_back(a[frame_size / 2], b[frame_size / 2]);
    }
    result.ns_per_frame = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / steps;
    stopping.store(true);
    start_gate.arrive_and_wait();
    return result;
}

void benchmark(std::size_t frame_size, std::size_t impulse_size, std::size_t steps) {
    const auto h1 = make_impulse(impulse_size, 0.021);
    const auto h2 = make_impulse(impulse_size, 0.043);
    const auto serial = run_serial(frame_size, h1, h2, steps);
    const auto parallel = run_parallel(frame_size, h1, h2, steps);
    for (std::size_t i = 0; i < steps; ++i) {
        require_close(parallel.probes[i].first, serial.probes[i].first, "First parallel FFT branch differs from serial.");
        require_close(parallel.probes[i].second, serial.probes[i].second, "Second parallel FFT branch differs from serial.");
    }
    std::cout << "frame=" << frame_size << " impulse=" << impulse_size << " steps=" << steps
              << " serial_ns_per_frame=" << serial.ns_per_frame
              << " parallel_ns_per_frame=" << parallel.ns_per_frame
              << " speedup=" << serial.ns_per_frame / parallel.ns_per_frame << '\n';
}

} // namespace

int main() {
    try {
        verify_convolution();
        std::cout << "complex convolution and matched-filter checks passed\n";
        benchmark(2048, 1024, 128);
        benchmark(8192, 4096, 96);
        benchmark(16384, 8192, 64);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
