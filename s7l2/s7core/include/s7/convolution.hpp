// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_CONVOLUTION_HPP
#define S7CORE_INCLUDE_S7_CONVOLUTION_HPP

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace s7 {

// IndependentFrames: each frame starts with zero history and produces the full
// linear convolution (frame_size + impulse_size - 1 samples).
// ContinuousStream: frames are consecutive parts of one signal; each step
// produces frame_size samples aligned with that input frame.
enum class ConvolutionMode : std::uint8_t { IndependentFrames, ContinuousStream };

// Reusable C64 frame processors. Each step consumes one fixed-size frame and
// returns a view of processor-owned output valid until the next step/reset or
// destruction. Distinct instances may run concurrently; one instance must not
// be stepped concurrently. These processors can be wrapped by graph Elements
// when S7 has a frame signal type; they do not change Element's scalar contract.
class FirConvolution final {
public:
    using Sample = std::complex<double>;

    FirConvolution(std::size_t frame_size, std::vector<Sample> impulse,
                   ConvolutionMode mode = ConvolutionMode::IndependentFrames);

    [[nodiscard]] std::span<const Sample> step(std::span<const Sample> frame);
    void reset();

    [[nodiscard]] std::size_t frame_size() const noexcept { return frame_size_; }
    [[nodiscard]] std::size_t impulse_size() const noexcept { return impulse_.size(); }
    [[nodiscard]] std::size_t output_size() const noexcept { return output_.size(); }
    [[nodiscard]] ConvolutionMode mode() const noexcept { return mode_; }

private:
    std::size_t frame_size_;
    std::vector<Sample> impulse_;
    ConvolutionMode mode_;
    std::vector<Sample> history_;
    std::vector<Sample> segment_;
    std::vector<Sample> output_;
};

// Portable radix-2 FFT implementation with the same frame and history
// semantics as FirConvolution. Impulse transform, bit reversal and twiddle
// factors are prepared once; step() performs no allocations.
class FftConvolution final {
public:
    using Sample = std::complex<double>;

    FftConvolution(std::size_t frame_size, std::vector<Sample> impulse,
                   ConvolutionMode mode = ConvolutionMode::IndependentFrames);

    [[nodiscard]] std::span<const Sample> step(std::span<const Sample> frame);
    void reset();

    [[nodiscard]] std::size_t frame_size() const noexcept { return frame_size_; }
    [[nodiscard]] std::size_t impulse_size() const noexcept { return impulse_size_; }
    [[nodiscard]] std::size_t output_size() const noexcept { return output_.size(); }
    [[nodiscard]] ConvolutionMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::size_t fft_size() const noexcept { return fft_size_; }

private:
    void transform(std::vector<Sample>& values, bool inverse) const;

    std::size_t frame_size_;
    std::size_t impulse_size_;
    std::size_t fft_size_;
    ConvolutionMode mode_;
    std::vector<std::size_t> bit_reversal_;
    std::vector<Sample> roots_;
    std::vector<Sample> kernel_spectrum_;
    std::vector<Sample> history_;
    std::vector<Sample> segment_;
    std::vector<Sample> work_;
    std::vector<Sample> output_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_CONVOLUTION_HPP
