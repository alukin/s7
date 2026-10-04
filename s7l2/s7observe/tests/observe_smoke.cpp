#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>

#include "s7/observe.hpp"

int main() try
{
    s7::Schema schema{"observe_smoke"};
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

    runtime.step();
    assert(runtime.real_output_of("sum") == 2.0);

    const auto samples = observation.sample_outputs({s7::ProbePoint{"memory", 0}, s7::ProbePoint{"sum", 0}});
    assert(samples.size() == 2);
    // Samples belong to one step: memory holds the value sum consumed in step 2.
    assert(samples[0].as_real() == 1.0);
    assert(samples[1].as_real() == 2.0);

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7observe smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7observe smoke: unknown fatal error\n";
    return 1;
}

