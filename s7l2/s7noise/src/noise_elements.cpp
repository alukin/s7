// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7noise/noise_elements.hpp"
#include <bit>
#include <cmath>
#include <random>
#include <stdexcept>

namespace s7noise {

// ============================================================================
// WhiteNoiseGenerator Implementation
// ============================================================================

WhiteNoiseGenerator::WhiteNoiseGenerator(double amplitude, uint32_t seed)
    : s7::Element("WhiteNoiseGenerator", true),
      rng_(seed == 0 ? std::random_device{}() : seed),
      distribution_(0.0, amplitude),
      output_value_(0.0)
{
    if (amplitude < 0.0) {
        throw std::invalid_argument("Amplitude must be non-negative");
    }

    // Register output port
    register_output(0, "out", s7::SignalType::F64, &output_value_);
}

void WhiteNoiseGenerator::step() {
    output_value_ = distribution_(rng_);
}

// ============================================================================
// PinkNoiseGenerator Implementation
// ============================================================================

PinkNoiseGenerator::PinkNoiseGenerator(double amplitude, uint32_t seed)
    : s7::Element("PinkNoiseGenerator", true),
      rng_(seed == 0 ? std::random_device{}() : seed),
      scale_(amplitude / std::sqrt(static_cast<double>(kRows + 1))),
      output_value_(0.0)
{
    if (amplitude < 0.0) {
        throw std::invalid_argument("Amplitude must be non-negative");
    }
    for (auto& row : rows_) {
        row = white_dist_(rng_);
        sum_ += row;
    }

    // Register output port
    register_output(0, "out", s7::SignalType::F64, &output_value_);
}

void PinkNoiseGenerator::step() {
    // Renew the row given by the number of trailing zeros of the counter:
    // row k changes every 2^(k+1) samples (counter 0 renews nothing).
    ++counter_;
    const int row = std::countr_zero(counter_);
    if (row < kRows) {
        const double fresh = white_dist_(rng_);
        sum_ += fresh - rows_[row];
        rows_[row] = fresh;
    }
    if ((counter_ & 0xFFFF) == 0) {
        // Re-sum now and then so rounding in the running sum cannot drift.
        sum_ = 0.0;
        for (const auto value : rows_) {
            sum_ += value;
        }
    }
    output_value_ = scale_ * (sum_ + white_dist_(rng_));
}

} // namespace s7noise

