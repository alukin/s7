// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#include "direct_dft.hpp"

#include <cstdlib>
#include <iostream>
#include <new>
#include <s7spectral/fft_element.hpp>
#include <limits>
#include <s7/core.hpp>
#include <s7spectral/analysis.hpp>
#include <s7spectral/fourier_element.hpp>
#include <s7spectral/vector_fft.hpp>
#if defined(__ELF__)
namespace {
thread_local bool measuring_fft{};
thread_local std::size_t fft_allocations{};

void* checked_allocation(void* pointer)
{
    if (!pointer) throw std::bad_alloc();
    if (measuring_fft) ++fft_allocations;
    return pointer;
}
} // namespace

void* operator new(std::size_t size) { return checked_allocation(std::malloc(size == 0 ? 1 : size)); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) != 0) throw std::bad_alloc();
    return checked_allocation(pointer);
}

void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
#endif

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
void check_vector_fft()
{
    using s7::SignalType;
    using C32 = std::complex<float>;
    static constexpr std::size_t size = 16;
    constexpr long double epsilon = std::numeric_limits<float>::epsilon();
    for (const auto pattern : {0, 1, 2, 3, 4}) {
        std::array<C32, size> values{};
        for (std::size_t index = 0; index < size; ++index) {
            const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) / size;
            const auto positive = std::polar(1.0, 3 * phase);
            const auto negative = std::polar(0.5, -2 * phase);
            const std::array<C32, 5> cases{
                index == 3 ? C32{1, 1} : C32{},
                C32{0.8f, -0.3f},
                C32(positive),
                C32(negative),
                C32(positive + negative)
            };
            values[index] = cases[static_cast<std::size_t>(pattern)];
        }
        const auto expected = s7spectral::test::direct_dft<float>(values);
        long double input_sum = 0;
        long double input_max = 0;
        for (const auto value : values) {
            const auto magnitude = static_cast<long double>(std::abs(value));
            input_sum += magnitude;
            input_max = std::max(input_max, magnitude);
        }
        const auto forward_tolerance = 8 * epsilon * 4 * std::max(1.0L, input_sum);
        const auto inverse_tolerance = 16 * epsilon * 4 * std::max(1.0L, input_max);

        s7::Schema schema("vector_fft_oracle");
        schema.set_rate(s7::Rate::from_hz(100));
        schema.add_element("source", [&] {
            return std::make_unique<s7::VectorConstantT<SignalType::C32>>(values, 0,
                std::make_shared<s7::ArrayMemoryBudget>(size * sizeof(C32)));
        });
        schema.add_element("forward", [] { return std::make_unique<s7spectral::VectorFFT>(size, false, 2); });
        schema.add_element("inverse", [] { return std::make_unique<s7spectral::VectorFFT>(size, true, 2); });
        schema.connect("source", 0, "forward", 0);
        schema.connect("forward", 0, "inverse", 0);
#if defined(__ELF__)
        fft_allocations = 0;
        measuring_fft = true;
#endif
        auto graph = s7::Compiler::compile_experimental_arrays(schema);
#if defined(__ELF__)
        measuring_fft = false;
        require(fft_allocations > 0, "FFT allocation probe did not detect setup");
#endif
        const auto& forward = static_cast<const s7spectral::VectorFFT&>(*graph.elements.at("forward")).array_output(0);
        const auto& inverse = static_cast<const s7spectral::VectorFFT&>(*graph.elements.at("inverse")).array_output(0);
        const auto& source = static_cast<const s7::VectorConstantT<SignalType::C32>&>(*graph.elements.at("source")).array_output(0);
        s7::NodeRuntime runtime(std::move(graph));
        runtime.step();
        for (int step = 0; step < 64; ++step) {
#if defined(__ELF__)
            fft_allocations = 0;
            measuring_fft = true;
#endif
            runtime.step();
#if defined(__ELF__)
            measuring_fft = false;
            require(fft_allocations == 0, "whole-array FFT step made a C++ allocation");
#endif
            const auto* input = reinterpret_cast<const C32*>(source.view().bytes().data());
            const auto* bins = reinterpret_cast<const C32*>(forward.view().bytes().data());
            const auto* reconstruction = reinterpret_cast<const C32*>(inverse.view().bytes().data());
            require(input != bins && bins != reconstruction, "FFT aliased immutable input storage");
            for (std::size_t index = 0; index < size; ++index) {
                require(input[index] == values[index], "FFT modified its input");
                require(std::abs(std::complex<long double>(bins[index]) - expected[index]) <= forward_tolerance,
                        "C32 forward FFT differs from the direct DFT");
                require(std::abs(std::complex<long double>(reconstruction[index]) -
                                std::complex<long double>(values[index])) <= inverse_tolerance,
                        "C32 inverse reconstruction mismatch");
            }
        }
    }
    rejects([] { s7spectral::VectorFFT fft(15, false, 2); });
    rejects([] { s7spectral::VectorFFT fft(16, false, 1); });
}
} // namespace
int main() try {
    check_vector_fft();
    for (double rate : {1024., 2048.}) {
        s7::Schema mismatch("mismatched-fft"); mismatch.set_rate(s7::Rate::from_hz(rate));
        mismatch.add_element("source", [rate] { return std::make_unique<s7::AnalogOscillator>(1, 32, rate); });
        mismatch.add_element("fft", [] { return std::make_unique<s7spectral::WindowedFFT>(64, 64, 44100); });
        mismatch.connect("source", 0, "fft", 0);
        rejects([&] { (void)s7::Compiler::compile(mismatch); });
        for (const auto window : {s7spectral::WindowType::RECTANGULAR, s7spectral::WindowType::HANN}) {
            s7spectral::WindowedFFT fft(64, 64, rate, window);
            double value = 0; const s7::InputBinding binding = &value; fft.bind_inputs({&binding, 1});
            for (int n = 0; n < 64; ++n) { value = n % 2 ? -1 : 1; fft.step(); }
            near(*static_cast<const double*>(fft.outputs()[0]), rate / 2);
            near(*static_cast<const double*>(fft.outputs()[1]), 1., 1e-6);
        }
    }
    for (const std::size_t n : {2, 8, 32}) {
        std::vector<s7spectral::Complex> signal(n), result;
        for (std::size_t i = 0; i < n; ++i)
            signal[i] = {std::sin(double(i) * .7), std::cos(double(i) * .3)};
        result = signal;
        s7spectral::Transform fft(n);
        fft.apply(result);
        const auto expected = s7spectral::test::direct_dft<double>(signal);
        for (std::size_t k = 0; k < n; ++k) {
            near(result[k], {double(expected[k].real()), double(expected[k].imag())});
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
