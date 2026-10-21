// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "s7/observe.hpp"

int main() try
{
    s7::Schema schema{"observe_smoke"};
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("memory", [] { return std::make_unique<s7::Delay>(0.0); });
    schema.connect("unit", 0, "sum", 0);
    schema.connect("memory", 0, "sum", 1);
    schema.connect("sum", 0, "memory", 0);

    const auto validation = s7::Validator::validate(schema);
    assert(validation.ok());

    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    const s7::ObservationService observation{runtime};

    runtime.step();
    assert(runtime.real_output_of("sum") == 1.0);
    assert(observation.sample_output("sum").as_real() == 1.0);
    assert(!observation.sample_output("sum").model_time_ns.has_value());

    // With a model-time base, samples carry the model time of their step.
    runtime.set_model_time_step_ns(1'000'000);

    runtime.step();
    assert(runtime.real_output_of("sum") == 2.0);

    const auto snapshot = observation.snapshot({s7::ProbePoint{"memory", 0}, s7::ProbePoint{"sum", 0}});
    assert(snapshot.step == 2);
    assert(snapshot.model_time_ns == 2'000'000);
    assert(snapshot.model_time_seconds == .002);
    assert(snapshot.samples[0].model_time_seconds == snapshot.model_time_seconds);
    assert(snapshot.samples[0].model_time_ns == 2'000'000);
    const auto& samples = snapshot.samples;
    assert(samples.size() == 2);
    assert(samples[0].step == 2 && samples[1].step == 2);
    // Samples belong to one step: memory holds the value sum consumed in step 2.
    assert(samples[0].as_real() == 1.0);
    assert(samples[1].as_real() == 2.0);

    // Outputs keep their exact type; reading them as another type is refused.
    {
        s7::Schema typed{"typed"};
        typed.set_rate(s7::Rate::from_hz(1));
        typed.add_element("x", [] { return std::make_unique<s7::Constant>(0.75); });
        typed.add_element("q", [] { return std::make_unique<s7::ConvertT<s7::SignalType::F64, s7::SignalType::I12>>(s7::ConvertOptions{s7::ConvertMode::Normalized}); });
        typed.connect("x", 0, "q", 0);
        s7::NodeRuntime typedRuntime{s7::Compiler::compile(typed)};
        typedRuntime.step();
        const auto sample = s7::ObservationService{typedRuntime}.sample_output("q");
        assert(sample.type == s7::SignalType::I12);
        assert(sample.as<s7::SignalType::I12>() == 1536);
        assert(sample.to_display_double() == 1536.0);
        bool rejected = false;
        try {
            static_cast<void>(sample.as_real());
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
    }

    // Observation from another thread than the stepping thread is refused.
    bool refused = false;
    std::thread other([&] {
        try {
            static_cast<void>(observation.snapshot({s7::ProbePoint{"sum", 0}}));
        } catch (const std::logic_error&) {
            refused = true;
        }
    });
    other.join();
    assert(refused);

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7observe smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7observe smoke: unknown fatal error\n";
    return 1;
}

