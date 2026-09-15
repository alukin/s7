#include "s7noise/noise_elements.hpp"
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
    register_output(0, "out", s7::SignalType::DOUBLE, &output_value_);
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
      white_dist_(0.0, amplitude),
      pink_index_(0),
      output_value_(0.0)
{
    if (amplitude < 0.0) {
        throw std::invalid_argument("Amplitude must be non-negative");
    }

    // Initialize pink noise state
    for (int i = 0; i < 7; ++i) {
        pink_[i] = 0.0;
    }

    // Register output port
    register_output(0, "out", s7::SignalType::DOUBLE, &output_value_);
}

void PinkNoiseGenerator::step() {
    // Generate white noise sample
    double white = white_dist_(rng_);

    // Update pink noise using Voss-McCartney algorithm
    pink_[pink_index_] = white;
    pink_index_ = (pink_index_ + 1) % 7;

    // Sum of the array gives pink noise
    // Divide by 7 to normalize amplitude
    double pink = 0.0;
    for (int i = 0; i < 7; ++i) {
        pink += pink_[i];
    }

    output_value_ = pink / 7.0;
}

} // namespace s7noise

