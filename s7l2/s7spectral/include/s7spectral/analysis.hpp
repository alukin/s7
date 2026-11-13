// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once
#include <algorithm>
#include <complex>
#include <functional>
#include <numbers>
#include <s7/float_types.hpp>
#include <span>
#include <stdexcept>
#include <vector>

namespace s7spectral {
using Complex = std::complex<double>;
using Cancel = std::function<bool()>;
inline bool finite(Complex v) { return std::isfinite(v.real()) && std::isfinite(v.imag()); }
inline void cancelled(const Cancel &cancel) {
    if (cancel && cancel())
        throw std::runtime_error("Analysis cancelled");
}
// Bounded, portable radix-2 transform shared by headless processors and plugin
// elements. No global FFT planner, Qt, wall clock or runtime dependency.
class Transform {
  public:
    static constexpr std::size_t maxSize = 1U << 20;
    explicit Transform(std::size_t size) : size_(size) {
        if (size < 2 || size > maxSize || (size & (size - 1)))
            throw std::invalid_argument("FFT size must be a power of two in [2, 1048576]");
        roots_.resize(size / 2);
        for (std::size_t k = 0; k < size / 2; ++k) {
            const double a = -2 * std::numbers::pi * double(k) / double(size);
            roots_[k] = {s7::math::cos(a), s7::math::sin(a)};
        }
    }
    void apply(std::span<Complex> data, bool inverse = false, const Cancel &cancel = {}) const {
        if (data.size() != size_)
            throw std::invalid_argument("FFT input size mismatch");
        for (std::size_t i = 1, j = 0; i < size_; ++i) {
            std::size_t bit = size_ >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(data[i], data[j]);
        }
        for (std::size_t length = 2; length <= size_; length *= 2) {
            cancelled(cancel);
            for (std::size_t base = 0; base < size_; base += length)
                for (std::size_t k = 0; k < length / 2; ++k) {
                    auto w = roots_[k * size_ / length];
                    if (inverse)
                        w = {w.real(), -w.imag()};
                    const auto u = data[base + k], v = w * data[base + k + length / 2];
                    data[base + k] = u + v;
                    data[base + k + length / 2] = u - v;
                }
        }
        if (inverse)
            for (auto &v : data)
                v /= double(size_);
    }
    static std::size_t padded(std::size_t samples) {
        if (samples > maxSize)
            throw std::invalid_argument("Transform exceeds the configured bound");
        std::size_t n = 2;
        while (n < samples)
            n *= 2;
        return n;
    }

  private:
    std::size_t size_;
    std::vector<Complex> roots_;
};
struct Response {
    std::vector<double> frequency;
    std::vector<Complex> values;
    std::size_t pulseIndex = 0, responseSamples = 0;
    double pulseHeight = 0;
};
inline Response impulseResponse(std::span<const double> input, std::span<const double> output, double rate,
                                const Cancel &cancel = {}) {
    if (input.size() != output.size() || input.size() < 2 || input.size() > 65536 || !std::isfinite(rate) ||
        rate <= 0)
        throw std::invalid_argument(
            "Impulse analysis needs 2–65536 paired samples and a positive finite rate");
    Response result;
    std::size_t pulses = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (!std::isfinite(input[i]) || !std::isfinite(output[i]))
            throw std::invalid_argument("Nonfinite impulse capture");
        if (input[i] != 0) {
            ++pulses;
            result.pulseIndex = i;
            result.pulseHeight = input[i];
        }
    }
    if (pulses != 1 || result.pulseIndex + 1 >= input.size())
        throw std::invalid_argument("Select exactly one nonzero input sample and a post-pulse response");
    for (std::size_t i = 0; i < result.pulseIndex; ++i)
        if (output[i] != 0)
            throw std::invalid_argument(
                "Impulse response requires zero initial output; select/subtract an explicit baseline first");
    result.responseSamples = input.size() - result.pulseIndex;
    const auto n = Transform::padded(result.responseSamples);
    std::vector<Complex> data(n);
    for (std::size_t k = 0; k < result.responseSamples; ++k) {
        data[k] = output[result.pulseIndex + k] / result.pulseHeight;
        if (!finite(data[k]))
            throw std::invalid_argument("Impulse normalization overflow");
    }
    Transform(n).apply(data, false, cancel);
    for (std::size_t k = 0; k <= n / 2; ++k) {
        if (!finite(data[k]))
            throw std::invalid_argument("Response transform overflow");
        result.frequency.push_back(rate * double(k) / double(n));
        result.values.push_back(data[k]);
    }
    return result;
}
struct TimeFrequency {
    std::vector<double> times, frequencies;
    // Time-major, frequency-fast. Complex coefficients remain authoritative.
    std::vector<Complex> coefficients;
    std::vector<bool> valid;
};
inline TimeFrequency spectrogram(std::span<const double> samples, double rate, std::size_t size = 256,
                                 std::size_t hop = 128, const Cancel &cancel = {}) {
    if (!std::isfinite(rate) || rate <= 0 || samples.size() > 65536 || size < 8 || size > 65536 ||
        (size & (size - 1)) || !hop || hop > size || samples.size() < size)
        throw std::invalid_argument("Invalid spectrogram rate/window/hop or insufficient samples");
    for (auto v : samples)
        if (!std::isfinite(v))
            throw std::invalid_argument("Spectrogram input must be finite and gap-free");
    const auto frames = 1 + (samples.size() - size) / hop, bins = size / 2 + 1;
    if (frames > 4'194'304 / bins)
        throw std::invalid_argument("Spectrogram exceeds 4194304 coefficient budget");
    TimeFrequency result;
    result.coefficients.reserve(frames * bins);
    result.valid.reserve(frames * bins);
    for (std::size_t k = 0; k < bins; ++k)
        result.frequencies.push_back(rate * double(k) / double(size));
    Transform fft(size);
    std::vector<Complex> data(size);
    std::vector<double> window(size);
    double sum = 0;
    for (std::size_t n = 0; n < size; ++n) {
        window[n] = .5 - .5 * s7::math::cos(2 * std::numbers::pi * double(n) / double(size));
        sum += window[n];
    }
    for (std::size_t start = 0; start + size <= samples.size(); start += hop) {
        cancelled(cancel);
        for (std::size_t n = 0; n < size; ++n)
            data[n] = samples[start + n] * window[n];
        fft.apply(data, false, cancel);
        result.times.push_back((double(start) + double(size - 1) / 2) / rate);
        for (std::size_t k = 0; k < bins; ++k) {
            const auto coefficient = data[k] * ((k == 0 || k == size / 2) ? 1 / sum : 2 / sum);
            if (!finite(coefficient))
                throw std::invalid_argument("Spectrogram overflow");
            result.coefficients.push_back(coefficient);
            result.valid.push_back(true);
        }
    }
    return result;
}
// Convert a periodic-Hann, peak-amplitude STFT coefficient to the real
// one-sided mean-square power per bin or PSD. DC/Nyquist are not doubled.
inline double spectrogramPower(Complex coefficient, std::size_t bin, std::size_t size, double rate,
                               bool density) {
    if (size < 8 || (size & (size - 1)) || bin > size / 2 || !std::isfinite(rate) || rate <= 0)
        throw std::invalid_argument("Invalid STFT power normalization");
    const double factor = bin == 0 || bin == size / 2 ? 1.0 : 2.0, sum = double(size) / 2,
                 energy = 3 * double(size) / 8;
    const double value = s7::math::abs(coefficient) *
                         (sum / s7::math::sqrt(factor * (density ? rate : double(size)) * energy));
    return value * value;
}
// Coherent-tone amplitude coefficient. No interpolation, hidden mean removal or
// PSD interpretation. Phase is relative to the first sample of the window.
inline Complex fourierCoefficient(std::span<const double> samples, double rate, double frequency) {
    if (samples.empty() || !std::isfinite(rate) || rate <= 0 || !std::isfinite(frequency) || frequency < 0 ||
        frequency > rate / 2)
        throw std::invalid_argument("Invalid Fourier coefficient configuration");
    Complex sum{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (!std::isfinite(samples[i]))
            throw std::invalid_argument("Fourier input must be finite");
        const double a = -2 * std::numbers::pi * (frequency / rate) * double(i);
        sum += samples[i] * Complex{s7::math::cos(a), s7::math::sin(a)};
    }
    sum *= ((frequency == 0 || frequency == rate / 2) ? 1.0 : 2.0) / double(samples.size());
    if (!finite(sum))
        throw std::invalid_argument("Fourier coefficient overflow");
    return sum;
}
} // namespace s7spectral
