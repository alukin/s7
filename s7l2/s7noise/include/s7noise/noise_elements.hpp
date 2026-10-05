#ifndef S7NOISE_INCLUDE_S7NOISE_NOISE_ELEMENTS_HPP
#define S7NOISE_INCLUDE_S7NOISE_NOISE_ELEMENTS_HPP

#include "s7/element.hpp"

#include <random>

namespace s7noise {

// White noise generator with normal (Gaussian) distribution
// 0 inputs, 1 output (double)
class WhiteNoiseGenerator : public s7::Element {
public:
    // amplitude: Standard deviation of the normal distribution
    // seed: Random seed (0 = use system random device)
    explicit WhiteNoiseGenerator(
        double amplitude = 1.0,
        uint32_t seed = 0
    );

    void step() override;

private:
    std::mt19937 rng_;
    std::normal_distribution<double> distribution_;
    double output_value_;
};

// Pink noise generator (1/f noise, -3 dB per octave) by the Voss-McCartney
// algorithm (Gardner's form): kRows white values, where row k is renewed
// every 2^(k+1) samples, plus a fresh white value each sample. Their sum has
// a 1/f spectrum over about kRows octaves. amplitude is the output's
// standard deviation.
class PinkNoiseGenerator : public s7::Element {
public:
    static constexpr int kRows = 16;

    explicit PinkNoiseGenerator(
        double amplitude = 1.0,
        uint32_t seed = 0
    );

    void step() override;

private:
    std::mt19937 rng_;
    std::normal_distribution<double> white_dist_{0.0, 1.0};
    double scale_;

    double rows_[kRows]{};
    double sum_{0.0};
    std::uint64_t counter_{0};

    double output_value_;
};

} // namespace s7noise

#endif // S7NOISE_INCLUDE_S7NOISE_NOISE_ELEMENTS_HPP

