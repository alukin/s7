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
s7::Schema schema() {
    s7::Schema result{"model_time"};
    result.set_rate(s7::Rate::from_hz(1));
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
        s7::NodeRuntime runtime{s7::Compiler::compile(schema())};
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
    s7::NodeRuntime runtime{s7::Compiler::compile(schema())};
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
    s7::NodeRuntime runtime{s7::Compiler::compile(schema())};
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
int main() try {
    fractional_rates(); overflow_is_atomic(); rate_mismatch_preserves_timebase();
    return 0;
} catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
