// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0

#include "direct_dft.hpp"
#include "s7spectral/vector_psd.hpp"

#include <s7/core.hpp>

#include <array>
#include <iostream>
#include <numeric>
#include <string_view>

namespace {
using namespace s7;
using C32 = signal_storage_t<SignalType::C32>;
constexpr std::size_t length = 16;
const ArrayDescriptor shape(SignalType::C32, std::array<std::int64_t, 1>{length});
const auto rate = Rate::from_hz(1'600);

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class Source final : public ArrayElement {
public:
    Source(const ArrayValue& value)
        : ArrayElement("PsdFixtureSource"), value_(value), out_(register_array_output("out", shape))
    {}

    void step() override { static_cast<void>(out_.exchange(ArrayValue(value_))); }

private:
    const ArrayValue& value_;
    ArrayOutputSlot& out_;
};

void check_psd(SpectrumUnits units, int pattern)
{
    ArrayPool source_pool(shape, 1, 0, std::make_shared<ArrayMemoryBudget>(shape.byte_size()));
    auto writer = source_pool.try_acquire().value();
    auto samples = writer.values<SignalType::C32>();
    std::array<std::complex<long double>, length> windowed{};
    long double energy = 0;
    long double weighted_square_sum = 0;
    for (std::size_t index = 0; index < length; ++index) {
        const auto angle = 2 * std::numbers::pi_v<long double> * static_cast<long double>(index) / length;
        const auto tone = std::polar(1.0L, 3 * angle) + std::polar(0.5L, -4 * angle);
        samples[index] = pattern == 0 ? C32{} : pattern == 1 ? C32{1.5f, -0.25f} : C32(tone);
        const auto weight = 0.5L - 0.5L * std::cos(angle);
        windowed[index] = weight * std::complex<long double>(samples[index]);
        energy += weight * weight;
        weighted_square_sum += std::norm(windowed[index]);
    }
    const std::optional<Rate> physical_rate = units == SpectrumUnits::PowerPerHz ? std::optional{rate} : std::nullopt;
    const ArraySourceOccurrence origin{9, 1, 42};
    ArrayValue input{std::move(writer).seal(), origin, ArraySampleWindow{physical_rate, 160, length, true, false}};
    Schema schema("periodogram");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("source", [&] { return std::make_unique<Source>(input); });
    schema.add_element("psd", [=] { return std::make_unique<s7spectral::VectorPSD>(length, units, physical_rate, 2); });
    schema.connect("source", 0, "psd", 0);
    auto graph = Compiler::compile_experimental_arrays(schema);
    auto admission = static_cast<s7spectral::VectorPSD&>(*graph.elements.at("psd")).make_retention();
    NodeRuntime runtime(std::move(graph));
    runtime.step();
    {
        const auto output = runtime.try_retain_array_output("psd", 0, admission).value();
        require(output.value.origin == origin && output.value.sample_window == input.sample_window &&
                    output.value.spectrum && output.value.spectrum->units == units &&
                    output.value.spectrum->sample_rate == physical_rate &&
                    output.value.spectrum->window == SpectrumWindow::PeriodicHann,
                "PSD lost axis metadata or source provenance");
        const auto oracle = s7spectral::test::direct_dft<long double>(windowed);
        const auto bins = output.value.lease.values<SignalType::F32>();
        const auto unit_scale = physical_rate ? physical_rate->to_hz() : static_cast<double>(length);
        for (std::size_t bin = 0; bin < length; ++bin) {
            const auto expected = std::norm(oracle[bin]) / (energy * unit_scale);
            require(std::abs(bins[bin] - expected) < 2e-6L, "PSD differs from the independent DFT");
            if (pattern == 0) require(bins[bin] == 0, "zero input did not produce exact zero");
        }
        const auto spacing = physical_rate ? physical_rate->to_hz() / length : 1.0;
        const auto integrated = std::accumulate(bins.begin(), bins.end(), 0.0L) * spacing;
        require(std::abs(integrated - weighted_square_sum / energy) < 2e-6L,
                "integrated PSD differs from window-weighted time-domain power");
    }

    const auto original_window = *input.sample_window;
    for (int invalid_case = 0; invalid_case < 3; ++invalid_case) {
        if (invalid_case == 2 && !physical_rate) continue;
        input.sample_window = original_window;
        const auto* expected = "complete sample block";
        if (invalid_case == 0) input.sample_window->missing_samples = true;
        if (invalid_case == 1) input.sample_window->valid_sample_count = length - 1;
        if (invalid_case == 2) {
            input.sample_window->sample_rate = Rate::from_hz(3'200);
            expected = "differs from the source rate";
        }
        bool failed = false;
        try { runtime.step(); }
        catch (const std::invalid_argument& error) { failed = std::string_view(error.what()).contains(expected); }
        require(failed && runtime.step_count() == 1, "invalid PSD input advanced publication");
        const auto held = runtime.try_retain_array_output("psd", 0, admission).value();
        require(held.domain_step == 1 && held.value.sample_window == original_window,
                "invalid PSD input replaced committed provenance");
    }
}

void check_incomplete_configuration()
{
    ArrayPool pool(shape, 1, 0, std::make_shared<ArrayMemoryBudget>(shape.byte_size()));
    auto writer = pool.try_acquire().value();
    std::ranges::fill(writer.values<SignalType::C32>(), C32{});
    const ArrayValue input{std::move(writer).seal()};
    Schema schema("psd_requires_physical_rate");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("source", [&] { return std::make_unique<Source>(input); });
    schema.add_element("missing_rate", [] {
        return std::make_unique<s7spectral::VectorPSD>(length, SpectrumUnits::PowerPerHz, std::nullopt, 2);
    });
    schema.connect("source", 0, "missing_rate", 0);
    NodeRuntime runtime(Compiler::compile_experimental_arrays(schema));
    bool rejected = false;
    try { runtime.step(); }
    catch (const std::invalid_argument& error) {
        rejected = std::string_view(error.what()).contains("require a physical sample rate");
    }
    require(rejected && runtime.step_count() == 0, "per-Hz output used graph cadence as a physical sample rate");
}
} // namespace

int main() try {
    for (const auto units : {SpectrumUnits::PowerPerBin, SpectrumUnits::PowerPerHz}) {
        for (int pattern = 0; pattern < 3; ++pattern) check_psd(units, pattern);
    }
    check_incomplete_configuration();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
