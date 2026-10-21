#include "s7/rate.hpp"

#include <cmath>
#include <format>
#include <numeric>
#include <limits>
#include <stdexcept>

namespace s7 {

Rate Rate::reduced(const std::uint64_t numerator, const std::uint64_t denominator)
{
    if (numerator == 0 || denominator == 0) {
        throw std::invalid_argument("A rate must be a positive number of samples per second.");
    }
    const auto divisor = std::gcd(numerator, denominator);
    return Rate{numerator / divisor, denominator / divisor};
}

Rate Rate::from_seconds(const double dt_seconds)
{
    if (!(dt_seconds > 0.0) || !std::isfinite(dt_seconds)) {
        throw std::invalid_argument("A model-time step must be a positive, finite number of seconds.");
    }
    if (dt_seconds < 1e-9 || dt_seconds > 9e9) {
        throw std::invalid_argument("A model-time step must be between 1 ns and 9e9 s.");
    }
    // Recover a simple rational period from the configured binary64 value.
    // Stop within binary64 rounding precision, rather than quantizing each
    // period to nanoseconds (which shifts e.g. 44.1 kHz to another rate).
    // Normalize the binary64 reciprocal round trip first: dt and a configured
    // Hz value of 1/dt must recover the same rational despite a one-ulp round
    // trip difference. This does not round to a fixed time unit.
    const long double period = 1.0 / (1.0 / dt_seconds);
    long double remainder = period;
    std::uint64_t p0 = 0, p1 = 1, q0 = 1, q1 = 0;
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    for (;;) {
        const auto whole = std::floor(remainder);
        if (whole >= static_cast<long double>(max)) break;
        const auto a = static_cast<std::uint64_t>(whole);
        if ((p1 && a > (max - p0) / p1) || (q1 && a > (max - q0) / q1)) break;
        const auto p = a * p1 + p0, q = a * q1 + q0;
        if (p && q && std::abs(static_cast<long double>(p) / q - period)
                <= period * 2 * std::numeric_limits<double>::epsilon())
            return reduced(q, p);
        p0 = p1; p1 = p; q0 = q1; q1 = q;
        const auto fraction = remainder - whole;
        if (fraction == 0) break;
        remainder = 1 / fraction;
    }
    throw std::invalid_argument("Model-time step cannot be represented as a rational rate.");
}

Rate Rate::from_hz(const double hz)
{
    if (!(hz > 0.0) || !std::isfinite(hz)) {
        throw std::invalid_argument("A sample rate must be a positive, finite number of Hz.");
    }
    return from_seconds(1.0 / hz);
}

std::string Rate::to_string() const
{
    if (denominator == 1) {
        return std::to_string(numerator) + " Hz";
    }
    return std::format("{:g} Hz ({}/{})", to_hz(), numerator, denominator);
}

} // namespace s7
