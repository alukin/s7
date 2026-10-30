// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/convolution.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace s7 {
namespace {

std::size_t required_output_size(const std::size_t frame_size, const std::size_t impulse_size,
                                 const ConvolutionMode mode) {
    if (frame_size == 0 || impulse_size == 0)
        throw std::invalid_argument("Convolution requires a nonempty frame and impulse response.");
    if (frame_size > std::numeric_limits<std::size_t>::max() - impulse_size + 1)
        throw std::length_error("Convolution output size overflows size_t.");
    if (mode != ConvolutionMode::IndependentFrames && mode != ConvolutionMode::ContinuousStream)
        throw std::invalid_argument("Unknown convolution mode.");
    return mode == ConvolutionMode::IndependentFrames ? frame_size + impulse_size - 1 : frame_size;
}

void require_frame_size(const std::size_t actual, const std::size_t expected) {
    if (actual != expected) throw std::invalid_argument("Convolution input frame size differs from the configured size.");
}

std::size_t required_history_size(const std::size_t frame_size, const std::size_t impulse_size,
                                  const ConvolutionMode mode) {
    static_cast<void>(required_output_size(frame_size, impulse_size, mode));
    return mode == ConvolutionMode::ContinuousStream ? impulse_size - 1 : 0;
}

} // namespace

FirConvolution::FirConvolution(const std::size_t frame_size, std::vector<Sample> impulse,
                               const ConvolutionMode mode)
    : frame_size_(frame_size)
    , impulse_(std::move(impulse))
    , mode_(mode)
    , history_(required_history_size(frame_size_, impulse_.size(), mode_))
    , segment_(mode_ == ConvolutionMode::ContinuousStream ? history_.size() + frame_size_ : 0)
    , output_(required_output_size(frame_size_, impulse_.size(), mode_))
{
}

std::span<const FirConvolution::Sample> FirConvolution::step(const std::span<const Sample> frame) {
    require_frame_size(frame.size(), frame_size_);
    std::fill(output_.begin(), output_.end(), Sample{});

    if (mode_ == ConvolutionMode::IndependentFrames) {
        for (std::size_t n = 0; n < output_.size(); ++n) {
            const auto first_tap = n >= frame_size_ ? n - frame_size_ + 1 : 0;
            const auto last_tap = std::min(n, impulse_.size() - 1);
            for (std::size_t k = first_tap; k <= last_tap; ++k)
                output_[n] += impulse_[k] * frame[n - k];
        }
        return output_;
    }

    std::copy(history_.begin(), history_.end(), segment_.begin());
    std::copy(frame.begin(), frame.end(), segment_.begin() + static_cast<std::ptrdiff_t>(history_.size()));
    for (std::size_t n = 0; n < frame_size_; ++n) {
        const auto centre = history_.size() + n;
        for (std::size_t k = 0; k < impulse_.size(); ++k)
            output_[n] += impulse_[k] * segment_[centre - k];
    }
    std::copy(segment_.end() - static_cast<std::ptrdiff_t>(history_.size()), segment_.end(), history_.begin());
    return output_;
}

void FirConvolution::reset() {
    std::fill(history_.begin(), history_.end(), Sample{});
    std::fill(output_.begin(), output_.end(), Sample{});
}

FftConvolution::FftConvolution(const std::size_t frame_size, std::vector<Sample> impulse,
                               const ConvolutionMode mode)
    : frame_size_(frame_size)
    , impulse_size_(impulse.size())
    , fft_size_([&] {
        static_cast<void>(required_output_size(frame_size_, impulse_size_, mode));
        const auto needed = required_output_size(frame_size_, impulse_size_, ConvolutionMode::IndependentFrames);
        if (needed > (std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1)))
            throw std::length_error("Convolution FFT size overflows size_t.");
        return std::bit_ceil(needed);
    }())
    , mode_(mode)
    , bit_reversal_(fft_size_)
    , roots_(fft_size_ / 2)
    , kernel_spectrum_(fft_size_)
    , history_(mode_ == ConvolutionMode::ContinuousStream ? impulse_size_ - 1 : 0)
    , segment_(mode_ == ConvolutionMode::ContinuousStream ? history_.size() + frame_size_ : 0)
    , work_(fft_size_)
    , output_(required_output_size(frame_size_, impulse_size_, mode_))
{
    const unsigned bits = std::bit_width(fft_size_) - 1;
    for (std::size_t i = 0; i < fft_size_; ++i) {
        std::size_t reversed{};
        for (unsigned b = 0; b < bits; ++b)
            reversed = (reversed << 1) | ((i >> b) & 1U);
        bit_reversal_[i] = reversed;
    }
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(fft_size_);
        roots_[i] = {std::cos(angle), std::sin(angle)};
    }
    std::copy(impulse.begin(), impulse.end(), kernel_spectrum_.begin());
    transform(kernel_spectrum_, false);
}

void FftConvolution::transform(std::vector<Sample>& values, const bool inverse) const {
    for (std::size_t i = 0; i < fft_size_; ++i) {
        if (i < bit_reversal_[i]) std::swap(values[i], values[bit_reversal_[i]]);
    }
    for (std::size_t length = 2; length <= fft_size_;) {
        const auto half = length / 2;
        const auto root_stride = fft_size_ / length;
        for (std::size_t base = 0; base < fft_size_; base += length) {
            for (std::size_t j = 0; j < half; ++j) {
                const Sample root = inverse ? std::conj(roots_[j * root_stride]) : roots_[j * root_stride];
                const Sample even = values[base + j];
                const Sample odd = root * values[base + j + half];
                values[base + j] = even + odd;
                values[base + j + half] = even - odd;
            }
        }
        if (length == fft_size_) break;
        length *= 2;
    }
    if (inverse) {
        const double scale = 1.0 / static_cast<double>(fft_size_);
        for (auto& value : values) value *= scale;
    }
}

std::span<const FftConvolution::Sample> FftConvolution::step(const std::span<const Sample> frame) {
    require_frame_size(frame.size(), frame_size_);
    std::fill(work_.begin(), work_.end(), Sample{});
    if (mode_ == ConvolutionMode::IndependentFrames) {
        std::copy(frame.begin(), frame.end(), work_.begin());
    } else {
        std::copy(history_.begin(), history_.end(), segment_.begin());
        std::copy(frame.begin(), frame.end(), segment_.begin() + static_cast<std::ptrdiff_t>(history_.size()));
        std::copy(segment_.begin(), segment_.end(), work_.begin());
        std::copy(segment_.end() - static_cast<std::ptrdiff_t>(history_.size()), segment_.end(), history_.begin());
    }
    transform(work_, false);
    for (std::size_t i = 0; i < fft_size_; ++i) work_[i] *= kernel_spectrum_[i];
    transform(work_, true);
    const auto offset = mode_ == ConvolutionMode::ContinuousStream ? impulse_size_ - 1 : 0;
    std::copy_n(work_.begin() + static_cast<std::ptrdiff_t>(offset), output_.size(), output_.begin());
    return output_;
}

void FftConvolution::reset() {
    std::fill(history_.begin(), history_.end(), Sample{});
    std::fill(output_.begin(), output_.end(), Sample{});
}

} // namespace s7
