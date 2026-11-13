// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once
#include <cmath>
#include <numbers>
#include <s7spectral/analysis.hpp>
#include <span>
#include <vector>

namespace s7wavelet {
// DC-corrected complex Morlet, omega0=cycles, truncated at ±4 sigma.
// Discrete L2 normalization: sum |kernel|² = 1. The stored kernel is in
// causal convolution order; coefficient n represents center n-halfSupport.
struct Kernel {
    std::vector<s7spectral::Complex> taps;
    std::size_t halfSupport = 0;
};
inline Kernel morlet(double rate, double frequency, double cycles = 6) {
    if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(frequency) || frequency <= 0 ||
        frequency >= rate / 2 || !std::isfinite(cycles) || cycles < 5 || cycles > 16)
        throw std::invalid_argument("Morlet needs 0 < frequency < Nyquist and cycles in [5,16]");
    const double sigma = cycles / (2 * std::numbers::pi * frequency), half = std::ceil(4 * sigma * rate);
    if (!std::isfinite(half) || half < 1 || half > 32767)
        throw std::invalid_argument("Morlet kernel exceeds 65535 taps; increase frequency or reduce rate");
    Kernel result;
    result.halfSupport = std::size_t(half);
    result.taps.resize(2 * result.halfSupport + 1);
    const double correction = s7::math::exp(-.5 * cycles * cycles);
    double energy = 0;
    for (std::size_t j = 0; j < result.taps.size(); ++j) {
        const double t = (double(result.halfSupport) - double(j)) / rate, u = t / sigma,
                     a = -2 * std::numbers::pi * frequency * t;
        const auto v =
            s7::math::exp(-.5 * u * u) * s7spectral::Complex{s7::math::cos(a) - correction, s7::math::sin(a)};
        result.taps[j] = v;
        energy += v.real() * v.real() + v.imag() * v.imag();
    }
    if (!(energy > 0) || !std::isfinite(energy))
        throw std::invalid_argument("Morlet normalization unavailable");
    const double norm = s7::math::sqrt(energy);
    for (auto &v : result.taps)
        v /= norm;
    return result;
}
inline s7spectral::TimeFrequency cwt(std::span<const double> samples, double rate,
                                     std::span<const double> frequencies, double cycles = 6,
                                     const s7spectral::Cancel &cancel = {}) {
    if (samples.size() < 2 || samples.size() > 65536 || frequencies.empty() || frequencies.size() > 128)
        throw std::invalid_argument("CWT supports 2–65536 samples and 1–128 frequencies");
    for (auto x : samples)
        if (!std::isfinite(x))
            throw std::invalid_argument("CWT input must be finite and gap-free");
    std::vector<Kernel> kernels;
    std::size_t maximum = 0, kernelBytes = 0;
    double previous = 0;
    for (auto frequency : frequencies) {
        if (frequency <= previous)
            throw std::invalid_argument("CWT frequencies must increase");
        auto k = morlet(rate, frequency, cycles);
        maximum = std::max(maximum, k.taps.size());
        kernelBytes += k.taps.size() * sizeof(s7spectral::Complex);
        if (kernelBytes > 64U * 1024 * 1024)
            throw std::invalid_argument("CWT kernel bank exceeds 64 MiB");
        kernels.push_back(std::move(k));
        previous = frequency;
    }
    const auto size = s7spectral::Transform::padded(samples.size() + maximum - 1);
    const auto bytes = kernelBytes + samples.size() * frequencies.size() * (sizeof(s7spectral::Complex) + 1) +
                       3 * size * sizeof(s7spectral::Complex);
    if (bytes > 192U * 1024 * 1024)
        throw std::invalid_argument("CWT job exceeds 192 MiB budget");
    s7spectral::TimeFrequency result;
    result.frequencies.assign(frequencies.begin(), frequencies.end());
    result.coefficients.resize(samples.size() * frequencies.size());
    result.valid.resize(result.coefficients.size(), false);
    for (std::size_t n = 0; n < samples.size(); ++n)
        result.times.push_back(double(n) / rate);
    s7spectral::Transform fft(size);
    std::vector<s7spectral::Complex> input(size), work(size);
    for (std::size_t n = 0; n < samples.size(); ++n)
        input[n] = samples[n];
    fft.apply(input, false, cancel);
    for (std::size_t s = 0; s < kernels.size(); ++s) {
        s7spectral::cancelled(cancel);
        std::fill(work.begin(), work.end(), s7spectral::Complex{});
        const auto &kernel = kernels[s];
        std::copy(kernel.taps.begin(), kernel.taps.end(), work.begin());
        fft.apply(work, false, cancel);
        for (std::size_t k = 0; k < size; ++k)
            work[k] *= input[k];
        fft.apply(work, true, cancel);
        for (std::size_t n = 0; n < samples.size(); ++n) {
            const auto index = n * frequencies.size() + s;
            result.coefficients[index] = work[n + kernel.halfSupport];
            if (!s7spectral::finite(result.coefficients[index]))
                throw std::invalid_argument("CWT overflow");
            result.valid[index] = n >= kernel.halfSupport && n + kernel.halfSupport < samples.size();
        }
    }
    return result;
}
} // namespace s7wavelet
