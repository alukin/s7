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

// Pink noise generator (1/f noise)
// Uses Voss-McCartney algorithm for efficiency
class PinkNoiseGenerator : public s7::Element {
public:
    explicit PinkNoiseGenerator(
        double amplitude = 1.0,
        uint32_t seed = 0
    );

    void step() override;

private:
    std::mt19937 rng_;
    std::normal_distribution<double> white_dist_;

    // Voss-McCartney state
    double pink_[7];
    int pink_index_;

    double output_value_;

    void update_pink();
};

} // namespace s7noise

#endif // S7NOISE_INCLUDE_S7NOISE_NOISE_ELEMENTS_HPP

