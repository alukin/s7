// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once

#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <vector>

namespace s7spectral::test {

template <class Real>
std::vector<std::complex<long double>> direct_dft(std::span<const std::complex<Real>> input, bool inverse = false)
{
    std::vector<std::complex<long double>> bins(input.size());
    const auto count = static_cast<long double>(input.size());
    const auto direction = inverse ? 1.0L : -1.0L;
    for (std::size_t k = 0; k < input.size(); ++k) {
        for (std::size_t j = 0; j < input.size(); ++j) {
            const auto angle = direction * 2 * std::numbers::pi_v<long double> *
                               static_cast<long double>(j) * static_cast<long double>(k) / count;
            bins[k] += std::complex<long double>{input[j].real(), input[j].imag()} *
                       std::complex<long double>{std::cos(angle), std::sin(angle)};
        }
        if (inverse) bins[k] /= count;
    }
    return bins;
}
} // namespace s7spectral::test
