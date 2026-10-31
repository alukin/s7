// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Exception, class F> void rejects(F action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected timing error was not raised");
}
// A graph seeded at `rate`: its model time must run at the same rate.
s7::Schema schema(const s7::Rate rate = s7::Rate::from_hz(1)) {
    s7::Schema result{"model_time"};
    result.set_rate(rate);
    result.add_element("constant", [] { return std::make_unique<s7::Constant>(1); });
    return result;
}
void fractional_rates() {
    for (const auto hz : {44100., 48000., 2.5, 1e9}) {
        const auto rate = s7::Rate::from_hz(hz);
        require(rate == s7::Rate::from_seconds(1 / hz), "Hz and dt disagree");
        if (hz == 44100 || hz == 48000) {
            require(rate.numerator == static_cast<std::uint64_t>(hz) && rate.denominator == 1,
                    "Audio rate was quantized");
        }
        s7::NodeRuntime runtime{s7::Compiler::compile(schema(rate))};
        require(!runtime.model_time_ns() && !runtime.model_time_seconds(), "Untimed graph has time");
        runtime.set_model_time_rate(rate);
        require(runtime.model_time_ns() == 0 && runtime.model_time_seconds() == 0, "Initial time is not zero");
        std::int64_t previous = 0;
        for (int step = 1; step <= 48000; ++step) {
            runtime.step();
            const long double expected = static_cast<long double>(step) / hz;
            require(runtime.model_time_ns() == std::llround(expected * 1e9L), "Absolute timestamp drift");
            require(std::abs(*runtime.model_time_seconds() - expected) <= expected * 3e-16L, "Seconds drift");
            require(*runtime.model_time_ns() > previous, "Non-monotonic model time");
            previous = *runtime.model_time_ns();
        }
        runtime.set_model_time_rate(std::nullopt);
        require(!runtime.model_time_ns() && !runtime.model_time_seconds(), "Cannot disable time");
    }
    // Periods that previously collapsed to the same integer-ns rate stay distinct.
    require(s7::Rate::from_seconds(.0010000001) != s7::Rate::from_seconds(.0010000002), "Distinct dt values merged");
    const auto fractional = s7::Rate::from_seconds(1.5e-9);
    require(fractional == s7::Rate::reduced(2'000'000'000, 3), "Fractional nanosecond lost");
    s7::NodeRuntime runtime{s7::Compiler::compile(schema(fractional))};
    runtime.set_model_time_rate(fractional);
    for (int step = 1; step <= 4; ++step) {
        runtime.step();
        require(runtime.model_time_ns() == (3 * step + 1) / 2, "Fractional-ns ties must round at absolute time");
    }
    for (double dt : {0., -1., 1e-10, 1e10, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects<std::invalid_argument>([&] { static_cast<void>(s7::Rate::from_seconds(dt)); });
    // Discretization writes both dt and 1/dt into different element types.
    // Their binary64 reciprocal round trip must not create false conflicts.
    for (int i = 1; i <= 1000; ++i) {
        const double dt = (i + std::sqrt(2.)) * 1e-7;
        require(s7::Rate::from_seconds(dt) == s7::Rate::from_hz(1 / dt), "Reciprocal rate conversion disagrees");
    }
}
void overflow_is_atomic() {
    s7::NodeRuntime runtime{s7::Compiler::compile(schema(s7::Rate::reduced(1'000'000'000, std::numeric_limits<std::int64_t>::max())))};
    rejects<std::invalid_argument>([&] { runtime.set_model_time_rate(s7::Rate{0, 1}); });
    rejects<std::invalid_argument>([&] { runtime.set_model_time_step_ns(0); });
    runtime.set_model_time_step_ns(std::numeric_limits<std::int64_t>::max());
    runtime.step();
    require(runtime.model_time_ns() == std::numeric_limits<std::int64_t>::max(), "Exact integer timestamp changed");
    rejects<std::overflow_error>([&] { runtime.step(); });
    require(runtime.step_count() == 1 && runtime.real_output_of("constant") == 1, "Overflow mutated graph");
}
void rate_mismatch_preserves_timebase() {
    auto model = schema();
    model.add_element("integral", [] { return std::make_unique<s7::EulerIntegrator>(.001); });
    model.connect("constant", 0, "integral", 0);
    s7::NodeRuntime runtime{s7::Compiler::compile(model)};
    runtime.set_model_time_rate(s7::Rate::from_hz(1000));
    rejects<std::invalid_argument>([&] { runtime.set_model_time_rate(s7::Rate::from_hz(2000)); });
    runtime.step();
    require(runtime.model_time_ns() == 1'000'000, "Rejected rate replaced the valid timebase");
}
}
// Seeded and inferred rates bind model time as much as declared ones.
void resolved_rates_bind_model_time() {
    s7::NodeRuntime seeded{s7::Compiler::compile(schema(s7::Rate::from_hz(1000)))};
    require(seeded.graph_rate() == s7::Rate::from_hz(1000), "Graph rate not reported");
    std::string error;
    try { seeded.set_model_time_rate(s7::Rate::from_hz(1)); } catch (const std::invalid_argument& e) { error = e.what(); }
    require(error.find("'constant'") != std::string::npos && error.find("Resampler") != std::string::npos,
            "Seeded rate conflict not reported with the element and a Resampler hint");
    require(!seeded.model_time_ns(), "Rejected rate became the time base");
    seeded.set_model_time_rate(s7::Rate{2000, 2}); // reduced before comparing
    // Two unconnected parts at different rates have no single model time.
    auto mixed = schema(s7::Rate::from_hz(1000));
    mixed.add_element("slow", [] { return std::make_unique<s7::EulerIntegrator>(.01); });
    mixed.add_element("zero", [] { return std::make_unique<s7::Constant>(0); });
    mixed.connect("zero", 0, "slow", 0);
    s7::NodeRuntime runtime{s7::Compiler::compile(mixed)};
    require(!runtime.graph_rate(), "Mixed rates reported as one");
    rejects<std::invalid_argument>([&] { runtime.set_model_time_rate(s7::Rate::from_hz(1000)); });
    rejects<std::invalid_argument>([&] { runtime.set_model_time_rate(s7::Rate::from_hz(100)); });
}
// Step times are rounded once, at absolute time, for model time and pacing alike.
void step_times() {
    const auto audio = s7::Rate::from_hz(44100);
    for (std::uint64_t step : {0ull, 1ull, 44100ull, 123456789ull})
        require(audio.step_time_ns(step) == std::llround(static_cast<long double>(step) * 1e9L / 44100), "Step time drifted");
    for (std::int64_t time : {std::int64_t{0}, std::int64_t{1}, std::int64_t{22675}, std::int64_t{22676}, std::int64_t{1'000'000'000}}) {
        const auto step = audio.first_step_at_ns(time);
        require(audio.step_time_ns(step) >= time && (step == 0 || audio.step_time_ns(step - 1) < time), "First step at time");
    }
    rejects<std::overflow_error>([] { static_cast<void>(s7::Rate::from_hz(1).step_time_ns(10'000'000'000ull)); });
}
int main() try {
    fractional_rates(); overflow_is_atomic(); rate_mismatch_preserves_timebase();
    resolved_rates_bind_model_time(); step_times();
    return 0;
} catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
