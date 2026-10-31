// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Smoke test for RateResolver (s7/rate_inference.hpp,
// doc/S7_Sample_Rate_Model.md), wired into Validator::validate() /
// Compiler::compile() as a mandatory check (s7core/src/validation.cpp): every
// schema must resolve one rate per connected set of elements, pinned by an
// element's own declared_rate() or by Schema::set_rate(), or validation fails.

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <boost/json.hpp>

#include "s7/core.hpp"

namespace {

namespace json = boost::json;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

void add(s7::Schema& schema, const std::string& id, const std::string& type, json::object params = {})
{
    const auto* descriptor = s7::ElementRegistry::instance().find(type);
    expect(descriptor != nullptr, "registered: " + type);
    schema.add_element(s7::make_element_spec(id, *descriptor, std::move(params)));
}

std::string validation_errors(const s7::Schema& schema)
{
    std::string text;
    for (const auto& error : s7::Validator::validate(schema).errors) {
        text += error + "\n";
    }
    return text;
}

void expect_error(const s7::Schema& schema, const std::string& fragment, const std::string& what)
{
    const auto errors = validation_errors(schema);
    expect(errors.find(fragment) != std::string::npos, what + ": expected '" + fragment + "' in: " + errors);
}

// The schema must already validate: builds every instance once (as
// Validator/Compiler do) and hands them to RateResolver directly, to inspect
// the resolved rates (Validator::validate() itself reports only errors).
s7::ResolvedRates resolved_rates(const s7::Schema& schema)
{
    expect(s7::Validator::validate(schema).ok(), "schema validates: " + validation_errors(schema));
    auto checked = s7::detail::check_schema(schema);
    std::unordered_map<std::string, const s7::Element*> instances;
    const auto& elements = schema.elements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        instances.emplace(elements[index].id, checked.elements[index].get());
    }
    const auto result = s7::RateResolver::resolve(schema, instances);
    expect(result.ok(), "resolves now that validation passed");
    return result;
}

// Rate pins: elements built in C++ with an explicit rate (rate-authoritative
// elements and, for now, C++-built rate-following ones) and schema seeds.
// Elements created from a document follow their domain's rate instead
// (doc/S7_Sample_Rate_Model.md section 8).
template <typename Make>
void pinned(s7::Schema& schema, const std::string& id, Make make)
{
    schema.add_element(id, [make] { return make(); });
}

void test_pinned_component_agrees()
{
    // x' = v, v' = -x, both integrators at dt = 0.5: one rate, 2 Hz, for
    // every element in the loop.
    s7::Schema schema{"harmonic"};
    pinned(schema, "x", [] { return std::make_unique<s7::EulerIntegrator>(0.5, 1.0); });
    pinned(schema, "v", [] { return std::make_unique<s7::EulerIntegrator>(0.5); });
    add(schema, "minus", "Gain", {{"factor", -1}});
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);

    const auto resolved = resolved_rates(schema);
    const auto expected = s7::Rate::from_seconds(0.5);
    expect(resolved.rates.at("x") == expected, "integrator rate resolved");
    expect(resolved.rates.at("v") == expected, "second integrator rate resolved");
    expect(resolved.rates.at("minus") == expected, "Gain between them takes the loop's rate");
    expect(expected.numerator == 2 && expected.denominator == 1, "1/0.5 s reduces to exactly 2 Hz");
}

void test_document_elements_follow_the_seed()
{
    // A document's stored dt is a legacy field: the integrators follow the seed.
    s7::Schema schema{"document_harmonic"};
    add(schema, "x", "EulerIntegrator", {{"dt", 0.5}, {"initial_value", 1}});
    add(schema, "v", "EulerIntegrator", {{"dt", 0.25}});
    add(schema, "minus", "Gain", {{"factor", -1}});
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);
    expect_error(schema, "cannot be determined", "document integrators pin nothing without a seed");
    schema.set_rate(s7::Rate::from_hz(100));
    const auto resolved = resolved_rates(schema);
    expect(resolved.rates.at("x") == s7::Rate::from_hz(100) && resolved.rates.at("v") == s7::Rate::from_hz(100),
           "document integrators follow the seed, whatever dt they store");
    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    runtime.step();
    runtime.step();
    // After two steps v holds v[1] = v[0] - dt * x[0] = -dt: the seed's 0.01 s,
    // not the 0.25 s the document stores.
    expect(runtime.real_output_of("v") == -0.01, "dt comes from the seed");
}

void test_conflict_is_reported()
{
    s7::Schema schema{"mismatched"};
    add(schema, "one", "Constant", {{"value", 1}});
    pinned(schema, "fast", [] { return std::make_unique<s7::EulerIntegrator>(0.1); });
    pinned(schema, "slow", [] { return std::make_unique<s7::EulerIntegrator>(0.5); });
    add(schema, "sum", "Adder");
    schema.connect("one", 0, "fast", 0);
    schema.connect("one", 0, "slow", 0);
    schema.connect("fast", 0, "sum", 0);
    schema.connect("slow", 0, "sum", 1);
    expect_error(schema, "Rate conflict", "two different dt values meet at one Adder");
    expect_error(schema, "Insert a rate-changing element", "conflict names the fix");
}

void test_undetermined_without_a_pin_or_seed()
{
    // Gain/Adder/Constant have no rate-fixing parameter of their own.
    s7::Schema schema{"unpinned"};
    add(schema, "one", "Constant", {{"value", 1}});
    add(schema, "gain", "Gain", {{"factor", 2}});
    schema.connect("one", 0, "gain", 0);
    expect_error(schema, "cannot be determined", "no element pins a rate and the schema has none");

    s7::Schema seeded{"seeded"};
    add(seeded, "one", "Constant", {{"value", 1}});
    add(seeded, "gain", "Gain", {{"factor", 2}});
    seeded.connect("one", 0, "gain", 0);
    seeded.set_rate(s7::Rate::from_hz(100));
    const auto resolved = resolved_rates(seeded);
    const auto expected = s7::Rate::from_hz(100);
    expect(resolved.rates.at("one") == expected && resolved.rates.at("gain") == expected, "seed applies to the whole component");
}

void test_components_are_resolved_independently()
{
    s7::Schema schema{"two_components"};
    add(schema, "feed", "Constant", {{"value", 1}});
    pinned(schema, "ramp", [] { return std::make_unique<s7::EulerIntegrator>(0.25); });
    add(schema, "double_it", "Gain", {{"factor", 2}});
    schema.connect("feed", 0, "ramp", 0);
    schema.connect("ramp", 0, "double_it", 0);
    add(schema, "one", "Constant", {{"value", 1}});
    add(schema, "triple_it", "Gain", {{"factor", 3}});
    schema.connect("one", 0, "triple_it", 0);

    const auto errors = validation_errors(schema);
    expect(errors.find("cannot be determined") != std::string::npos, "the unconnected, unpinned second component is still undetermined");
    expect(errors.find("ramp") == std::string::npos && errors.find("double_it") == std::string::npos,
           "the resolved component is not the one reported as undetermined");
}

void test_isolated_pinned_element()
{
    s7::Schema schema{"oscillator"};
    pinned(schema, "osc", [] { return std::make_unique<s7::AnalogOscillator>(1.0, 1.0, 48000.0); });
    const auto resolved = resolved_rates(schema);
    expect(resolved.rates.at("osc") == s7::Rate::from_hz(48000), "a C++-built oscillator's sample rate pins its rate");

    s7::Schema document{"document_oscillator"};
    add(document, "osc", "AnalogOscillator", {{"sample_rate_hz", 48000}});
    expect_error(document, "cannot be determined", "a document oscillator's stored sample_rate_hz pins nothing");
}

} // namespace

int main()
try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    test_pinned_component_agrees();
    test_document_elements_follow_the_seed();
    test_conflict_is_reported();
    test_undetermined_without_a_pin_or_seed();
    test_components_are_resolved_independently();
    test_isolated_pinned_element();
    {
        s7::Schema schema{"fractional_rate_conflict"};
        schema.set_rate(s7::Rate::from_seconds(.0010000001));
        add(schema, "one", "Constant", {{"value", 1}});
        pinned(schema, "ramp", [] { return std::make_unique<s7::EulerIntegrator>(.0010000002); });
        pinned(schema, "other", [] { return std::make_unique<s7::EulerIntegrator>(.0010000001); });
        schema.connect("one", 0, "ramp", 0);
        schema.connect("ramp", 0, "other", 0);
        expect_error(schema, "Rate conflict", "sub-nanosecond period mismatch is not silently accepted");
    }
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core rate inference smoke: " << ex.what() << '\n';
    return 1;
}
