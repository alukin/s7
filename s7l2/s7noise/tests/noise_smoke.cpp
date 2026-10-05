// Noise generators: level and spectral slope (pink: -3 dB per octave).
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "s7noise/noise_elements.hpp"

namespace {

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

void fft(std::vector<std::complex<double>>& data)
{
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const auto w = std::polar(1.0, -2.0 * std::numbers::pi / static_cast<double>(length));
        for (std::size_t start = 0; start < n; start += length) {
            std::complex<double> factor(1.0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * factor;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                factor *= w;
            }
        }
    }
}

struct Measured {
    double stddev{};
    double slope_db_per_octave{};
};

template <typename Generator>
Measured measure(Generator& generator)
{
    constexpr std::size_t kSegment = 4096;
    constexpr std::size_t kSegments = 64;
    std::vector<double> power(kSegment / 2, 0.0);
    double sum = 0.0;
    double sum2 = 0.0;
    for (std::size_t segment = 0; segment < kSegments; ++segment) {
        std::vector<std::complex<double>> data(kSegment);
        for (auto& value : data) {
            generator.step();
            const double sample = *static_cast<const double*>(generator.outputs().front());
            sum += sample;
            sum2 += sample * sample;
            value = sample;
        }
        fft(data);
        for (std::size_t bin = 1; bin < kSegment / 2; ++bin) {
            power[bin] += std::norm(data[bin]);
        }
    }
    const double count = static_cast<double>(kSegment * kSegments);
    Measured result;
    result.stddev = std::sqrt(sum2 / count - (sum / count) * (sum / count));
    // Mean power per bin in octaves [2^k, 2^(k+1)), k = 3..10; least-squares slope.
    std::vector<double> xs;
    std::vector<double> ys;
    for (int octave = 3; octave <= 10; ++octave) {
        double band = 0.0;
        const std::size_t lo = std::size_t{1} << octave;
        const std::size_t hi = std::size_t{1} << (octave + 1);
        for (std::size_t bin = lo; bin < hi; ++bin) {
            band += power[bin];
        }
        xs.push_back(octave);
        ys.push_back(10.0 * std::log10(band / static_cast<double>(hi - lo)));
    }
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        mx += xs[i] / static_cast<double>(xs.size());
        my += ys[i] / static_cast<double>(xs.size());
    }
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        num += (xs[i] - mx) * (ys[i] - my);
        den += (xs[i] - mx) * (xs[i] - mx);
    }
    result.slope_db_per_octave = num / den;
    return result;
}

} // namespace

int main()
try {
    s7noise::PinkNoiseGenerator pink(0.5, 1234);
    const auto p = measure(pink);
    std::cout << "pink: stddev=" << p.stddev << " slope=" << p.slope_db_per_octave << " dB/octave\n";
    expect(std::abs(p.slope_db_per_octave + 3.0) < 0.6, "pink noise falls 3 dB per octave");
    expect(std::abs(p.stddev - 0.5) < 0.05, "pink noise standard deviation is the amplitude");

    s7noise::WhiteNoiseGenerator white(0.5, 1234);
    const auto w = measure(white);
    std::cout << "white: stddev=" << w.stddev << " slope=" << w.slope_db_per_octave << " dB/octave\n";
    expect(std::abs(w.slope_db_per_octave) < 0.3, "white noise is flat");
    expect(std::abs(w.stddev - 0.5) < 0.02, "white noise standard deviation is the amplitude");
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7noise smoke: " << ex.what() << '\n';
    return 1;
}
