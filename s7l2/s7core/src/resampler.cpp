// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/resampler.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <cmath>
#include <limits>
#include <numbers>

namespace s7::resampling {
namespace {
using boost::multiprecision::uint256_t;

long double hz(const Rate& rate)
{
    return static_cast<long double>(rate.numerator) / static_cast<long double>(rate.denominator);
}

// Modified Bessel function of the first kind, order zero, by its power series.
long double bessel_i0(const long double x)
{
    long double sum = 1, term = 1;
    const long double half = x / 2;
    for (int k = 1; k < 1000; ++k) {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < sum * 1e-21L) break;
    }
    return sum;
}
} // namespace

std::vector<long double> design_anti_aliasing(const Rate& input, const Rate& output, const double attenuation_db,
                                              const std::size_t max_taps)
{
    const long double fs_in = hz(input), fs_out = hz(output);
    const long double cutoff = 0.45L * fs_out;      // midway between 0.4 and 0.5 of the output rate
    const long double transition = 0.1L * fs_out;   // passband edge to stopband edge
    const long double a = attenuation_db;
    const long double omega = 2 * std::numbers::pi_v<long double> * transition / fs_in;
    const long double beta = a > 50 ? 0.1102L * (a - 8.7L) : a >= 21 ? 0.5842L * std::pow(a - 21, 0.4L) + 0.07886L * (a - 21) : 0;
    const long double estimate = std::ceil((a - 8) / (2.285L * omega)) + 1;
    if (!(estimate <= static_cast<long double>(max_taps))) {
        throw std::invalid_argument("Resampler anti-aliasing filter from " + input.to_string() + " to " + output.to_string() +
                                    " would need more than " + std::to_string(max_taps) + " taps.");
    }
    auto count = static_cast<std::size_t>(estimate);
    if (count % 2 == 0) ++count; // odd: an integer group delay
    const auto middle = static_cast<long double>((count - 1) / 2);
    const long double band = 2 * cutoff / fs_in;
    std::vector<long double> taps(count);
    long double sum = 0;
    for (std::size_t n = 0; n < count; ++n) {
        const long double offset = static_cast<long double>(n) - middle;
        const long double x = band * offset;
        const long double sinc = x == 0 ? 1 : std::sin(std::numbers::pi_v<long double> * x) / (std::numbers::pi_v<long double> * x);
        const long double ratio = middle == 0 ? 0 : offset / middle;
        const long double window = bessel_i0(beta * std::sqrt(std::max(0.0L, 1 - ratio * ratio))) / bessel_i0(beta);
        taps[n] = band * sinc * window;
        sum += taps[n];
    }
    for (auto& tap : taps) tap /= sum; // unit gain at DC
    return taps;
}

Position input_position(const std::uint64_t step, const Rate& input, const Rate& output)
{
    const uint256_t numerator = uint256_t(step) * input.numerator * output.denominator;
    const uint256_t denominator = uint256_t(input.denominator) * output.numerator;
    const uint256_t index = numerator / denominator;
    if (index > std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Resampler input index overflow.");
    const uint256_t remainder = numerator % denominator;
    return {index.convert_to<std::uint64_t>(), remainder.convert_to<long double>() / denominator.convert_to<long double>()};
}

std::uint64_t samples_through(const std::uint64_t step, const Rate& rate, const Rate& model)
{
    const uint256_t count = uint256_t(step) * rate.numerator * model.denominator / (uint256_t(rate.denominator) * model.numerator);
    if (count >= std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Rate domain sample count overflow.");
    return count.convert_to<std::uint64_t>() + 1;
}

} // namespace s7::resampling
