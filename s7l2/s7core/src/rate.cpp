#include "s7/rate.hpp"

#include <cmath>
#include <format>
#include <numeric>
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
    const auto ns = std::llround(dt_seconds * 1e9);
    if (ns < 1 || static_cast<double>(ns) > 9e18) {
        throw std::invalid_argument("A model-time step must be between 1 ns and 9e9 s.");
    }
    return reduced(1'000'000'000ull, static_cast<std::uint64_t>(ns));
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
