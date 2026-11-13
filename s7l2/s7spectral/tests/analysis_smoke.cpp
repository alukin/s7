// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#include <iostream>
#include <limits>
#include <s7/core.hpp>
#include <s7spectral/analysis.hpp>
#include <s7spectral/fourier_element.hpp>
namespace {
void require(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
void near(std::complex<double> a, std::complex<double> b, double tolerance = 1e-10) {
    require(std::abs(a - b) < tolerance, "Numerical reference mismatch");
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid analysis input accepted");
}
} // namespace
int main() try {
    for (const std::size_t n : {2, 8, 32}) {
        std::vector<s7spectral::Complex> signal(n), result;
        for (std::size_t i = 0; i < n; ++i)
            signal[i] = {std::sin(double(i) * .7), std::cos(double(i) * .3)};
        result = signal;
        s7spectral::Transform fft(n);
        fft.apply(result);
        for (std::size_t k = 0; k < n; ++k) {
            std::complex<long double> expected{};
            for (std::size_t j = 0; j < n; ++j) {
                const auto a = -2 * std::numbers::pi_v<long double> * static_cast<long double>(j * k) /
                               static_cast<long double>(n);
                expected += std::complex<long double>(signal[j].real(), signal[j].imag()) *
                            std::complex<long double>(std::cos(a), std::sin(a));
            }
            near(result[k], {double(expected.real()), double(expected.imag())});
        }
        fft.apply(result, true);
        for (std::size_t i = 0; i < n; ++i)
            near(signal[i], result[i]);
    }
    std::vector<double> u(128), y(128);
    u[3] = 1000;
    y[10] = 2500;
    const auto response = s7spectral::impulseResponse(u, y, 1000);
    require(response.pulseIndex == 3 && response.pulseHeight == 1000 && response.responseSamples == 125,
            "Pulse provenance");
    for (std::size_t k = 0; k < response.values.size(); ++k)
        near(response.values[k], std::polar(2.5, -2 * std::numbers::pi * response.frequency[k] * 7 / 1000));
    u[4] = 1;
    rejects([&] { s7spectral::impulseResponse(u, y, 1000); });
    u[4] = 0;
    y[0] = 1;
    rejects([&] { s7spectral::impulseResponse(u, y, 1000); });
    std::vector<double> tone(1024);
    for (std::size_t n = 0; n < tone.size(); ++n)
        tone[n] = std::sin(2 * std::numbers::pi * 16 * double(n) / 1024);
    const auto map = s7spectral::spectrogram(tone, 1024, 256, 128);
    require(map.times.size() == 7 && map.frequencies.size() == 129, "STFT dimensions");
    for (std::size_t i = 0; i < 7; ++i)
        near(std::abs(map.coefficients[i * 129 + 4]), 1.0);
    double power = 0, density = 0;
    for (std::size_t k = 0; k < 129; ++k) {
        power += s7spectral::spectrogramPower(map.coefficients[k], k, 256, 1024, false);
        density += s7spectral::spectrogramPower(map.coefficients[k], k, 256, 1024, true) * 4;
    }
    near(power, .5);
    near(density, .5);
    near(s7spectral::fourierCoefficient(std::span(tone).first(64), 1024, 16), {0, -1});
    rejects([&] { s7spectral::spectrogram(tone, 1024, 255, 128); });
    rejects([&] { s7spectral::spectrogram(tone, 1024, 256, 0); });
    rejects([&] { s7spectral::spectrogram(tone, 1024, 256, 128, [] { return true; }); });
    s7::Schema schema("coefficient");
    schema.set_rate(s7::Rate::from_hz(1024));
    schema.add_element("input", [] { return std::make_unique<s7::AnalogOscillator>(1, 16, std::nullopt); });
    schema.add_element("coefficient",
                       [] { return std::make_unique<s7spectral::FourierCoefficient>(16, .0625, .03125); });
    schema.connect("input", 0, "coefficient", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for (int n = 1; n <= 128; ++n) {
        runtime.step();
        require(runtime.real_output_of("coefficient", 1) == (n >= 64 && (n - 64) % 32 == 0 ? 1 : 0),
                "Coefficient update timing");
    }
    near(runtime.complex_output_of("coefficient", 0), {0, -1});
    std::cout << "Spectral analysis checks passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
