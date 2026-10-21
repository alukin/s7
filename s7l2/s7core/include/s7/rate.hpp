// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_RATE_HPP
#define S7CORE_INCLUDE_S7_RATE_HPP

// Samples per second, exact (doc/S7_Sample_Rate_Model.md, section 3): the
// same {numerator, denominator} shape the wire protocol already uses for a
// stream's sample_rate (s7transport::stream::SampleRate), reused here for a
// purely local rate so the two never drift apart in representation.

#include <cstdint>
#include <string>

namespace s7 {

struct Rate {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};

    // Reduces numerator/denominator to lowest terms. Throws std::invalid_argument
    // if either is zero (a rate must be a positive number of samples per second).
    [[nodiscard]] static Rate reduced(std::uint64_t numerator, std::uint64_t denominator);
    // From a model-time step in seconds (e.g. an integrator's "dt").
    // Recovers a reduced rational period within binary64 rounding precision,
    // without quantizing the period to whole nanoseconds. Range: 1 ns..9e9 s.
    [[nodiscard]] static Rate from_seconds(double dt_seconds);
    // From samples per second (e.g. an oscillator's "sample_rate_hz"), via the
    // same rational-period conversion as from_seconds.
    [[nodiscard]] static Rate from_hz(double hz);

    [[nodiscard]] double to_hz() const { return static_cast<double>(numerator) / static_cast<double>(denominator); }
    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const Rate&, const Rate&) = default;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RATE_HPP
