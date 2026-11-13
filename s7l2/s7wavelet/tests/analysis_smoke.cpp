// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#include <iostream>
#include <s7/core.hpp>
#include <s7wavelet/coefficient_element.hpp>
namespace {
void require(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
} // namespace
int main() try {
    constexpr double fs = 128, f = 16;
    const std::vector<double> frequencies{8, 16, 24};
    std::vector<double> input(256);
    for (std::size_t n = 0; n < input.size(); ++n)
        input[n] = std::sin(2 * std::numbers::pi * f * double(n) / fs) + .1 * std::cos(.35 * double(n));
    const auto result = s7wavelet::cwt(input, fs, frequencies);
    // Independent long-double direct sum, with independently calculated kernels.
    for (std::size_t s = 0; s < frequencies.size(); ++s) {
        const long double sigma = 6 / (2 * std::numbers::pi_v<long double> * frequencies[s]);
        const auto half = std::size_t(std::ceil(4 * sigma * fs));
        std::vector<std::complex<long double>> kernel(2 * half + 1);
        long double energy = 0;
        for (std::size_t j = 0; j < kernel.size(); ++j) {
            const long double t = (static_cast<long double>(half) - j) / fs, u = t / sigma,
                              a = -2 * std::numbers::pi_v<long double> * frequencies[s] * t;
            kernel[j] =
                std::exp(-u * u / 2) * std::complex<long double>(std::cos(a) - std::exp(-18.0L), std::sin(a));
            energy += std::norm(kernel[j]);
        }
        for (std::size_t n = 0; n < input.size(); ++n) {
            std::complex<long double> expected{};
            for (std::size_t j = 0; j < kernel.size(); ++j) {
                const auto index = std::int64_t(n + half) - std::int64_t(j);
                if (index >= 0 && std::size_t(index) < input.size())
                    expected +=
                        static_cast<long double>(input[std::size_t(index)]) * kernel[j] / std::sqrt(energy);
            }
            const auto actual = result.coefficients[n * frequencies.size() + s];
            require(std::abs(actual -
                             std::complex<double>(double(expected.real()), double(expected.imag()))) < 1e-10,
                    "CWT/direct reference mismatch");
            require(result.valid[n * frequencies.size() + s] == (n >= half && n + half < input.size()),
                    "Support/cone mask mismatch");
        }
    }
    // A chirp supplies an independent physical frequency reference away from edges.
    std::vector<double> chirp(1024), frequenciesGrid;
    for (std::size_t n = 0; n < chirp.size(); ++n) {
        const double t = double(n) / 128;
        chirp[n] = std::sin(2 * std::numbers::pi * (5 * t + 35 * t * t / 16));
    }
    for (int i = 0; i < 64; ++i)
        frequenciesGrid.push_back(4 * std::pow(12.0, double(i) / 63));
    const auto chirpMap = s7wavelet::cwt(chirp, 128, frequenciesGrid);
    for (std::size_t n : {256, 512, 768}) {
        std::size_t peak = 0;
        double amplitude = -1;
        for (std::size_t f = 0; f < frequenciesGrid.size(); ++f)
            if (chirpMap.valid[n * frequenciesGrid.size() + f] &&
                std::abs(chirpMap.coefficients[n * frequenciesGrid.size() + f]) > amplitude) {
                amplitude = std::abs(chirpMap.coefficients[n * frequenciesGrid.size() + f]);
                peak = f;
            }
        const double expected = 5 + 35 * double(n) / 1024;
        require(std::abs(frequenciesGrid[peak] - expected) < .1 * expected, "Chirp ridge frequency");
    }
    bool cancelled = false;
    try {
        s7wavelet::cwt(input, fs, frequencies, 6, [] { return true; });
    } catch (const std::runtime_error &) {
        cancelled = true;
    }
    require(cancelled, "CWT cancellation");
    s7::Schema schema("wavelet");
    schema.set_rate(s7::Rate::from_hz(fs));
    schema.add_element("input", [f] { return std::make_unique<s7::AnalogOscillator>(1, f, std::nullopt); });
    schema.add_element("wavelet", [f] { return std::make_unique<s7wavelet::WaveletCoefficient>(f); });
    schema.connect("input", 0, "wavelet", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    const auto kernel = s7wavelet::morlet(fs, f);
    std::vector<double> sine(256);
    for (std::size_t n = 0; n < sine.size(); ++n)
        sine[n] = std::sin(2 * std::numbers::pi * f * double(n) / fs);
    const auto offline = s7wavelet::cwt(sine, fs, std::vector<double>{f});
    for (std::size_t n = 0; n < sine.size(); ++n) {
        runtime.step();
        const auto coefficient = runtime.complex_output_of("wavelet", 0);
        require(runtime.real_output_of("wavelet", 2) == double(kernel.halfSupport), "Explicit causal delay");
        if (n >= kernel.taps.size() - 1) {
            require(runtime.real_output_of("wavelet", 1) == 1, "Full-support valid flag");
            require(std::abs(coefficient - offline.coefficients[n - kernel.halfSupport]) < 1e-10,
                    "Causal/offline delay alignment");
        } else
            require(runtime.real_output_of("wavelet", 1) == 0, "Premature validity");
    }
    bool rejected = false;
    try {
        s7wavelet::morlet(fs, fs / 2);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "Nyquist wavelet accepted");
    std::cout << "Wavelet analysis checks passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
