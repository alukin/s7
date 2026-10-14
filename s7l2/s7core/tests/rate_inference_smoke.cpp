// Stand-alone smoke test for RateResolver (s7/rate_inference.hpp,
// doc/S7_Sample_Rate_Model.md). Not yet wired into Validator::validate(): see
// that header's comment. A schema's elements are created once via
// s7::detail::check_schema (the same helper Validator/Compiler use), and the
// resulting instances are handed to RateResolver directly.

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

s7::ResolvedRates resolve(const s7::Schema& schema)
{
    auto checked = s7::detail::check_schema(schema);
    std::string check_errors;
    for (const auto& error : checked.errors) {
        check_errors += error + "\n";
    }
    expect(checked.errors.empty(), "schema is otherwise valid: " + check_errors);
    std::unordered_map<std::string, const s7::Element*> instances;
    const auto& elements = schema.elements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        instances.emplace(elements[index].id, checked.elements[index].get());
    }
    return s7::RateResolver::resolve(schema, instances);
}

void expect_rate_error(const s7::Schema& schema, const std::string& fragment, const std::string& what)
{
    const auto resolved = resolve(schema);
    std::string text;
    for (const auto& error : resolved.errors) {
        text += error + "\n";
    }
    expect(text.find(fragment) != std::string::npos, what + ": expected '" + fragment + "' in: " + text);
}

void test_pinned_component_agrees()
{
    // x' = v, v' = -x, both integrators at dt = 0.5: one rate, 2 Hz, for
    // every element in the loop.
    s7::Schema schema{"harmonic"};
    add(schema, "x", "EulerIntegrator", {{"dt", 0.5}, {"initial_value", 1}});
    add(schema, "v", "EulerIntegrator", {{"dt", 0.5}});
    add(schema, "minus", "Gain", {{"factor", -1}});
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);

    const auto resolved = resolve(schema);
    expect(resolved.ok(), "matching dt resolves without error");
    const auto expected = s7::Rate::from_seconds(0.5);
    expect(resolved.rates.at("x") == expected, "integrator rate resolved");
    expect(resolved.rates.at("v") == expected, "second integrator rate resolved");
    expect(resolved.rates.at("minus") == expected, "Gain between them takes the loop's rate");
    expect(expected.numerator == 2 && expected.denominator == 1, "1/0.5 s reduces to exactly 2 Hz");
}

void test_conflict_is_reported()
{
    s7::Schema schema{"mismatched"};
    add(schema, "one", "Constant", {{"value", 1}});
    add(schema, "fast", "EulerIntegrator", {{"dt", 0.1}});
    add(schema, "slow", "EulerIntegrator", {{"dt", 0.5}});
    add(schema, "sum", "Adder");
    schema.connect("one", 0, "fast", 0);
    schema.connect("one", 0, "slow", 0);
    schema.connect("fast", 0, "sum", 0);
    schema.connect("slow", 0, "sum", 1);
    expect_rate_error(schema, "Rate conflict", "two different dt values meet at one Adder");
    expect_rate_error(schema, "Insert a rate-changing element", "conflict names the fix");
}

void test_undetermined_without_a_pin_or_seed()
{
    // Gain/Adder/Constant have no rate-fixing parameter of their own.
    s7::Schema schema{"unpinned"};
    add(schema, "one", "Constant", {{"value", 1}});
    add(schema, "gain", "Gain", {{"factor", 2}});
    schema.connect("one", 0, "gain", 0);
    expect_rate_error(schema, "cannot be determined", "no element pins a rate and the schema has none");

    s7::Schema seeded{"seeded"};
    add(seeded, "one", "Constant", {{"value", 1}});
    add(seeded, "gain", "Gain", {{"factor", 2}});
    seeded.connect("one", 0, "gain", 0);
    seeded.set_rate(s7::Rate::from_hz(100));
    const auto resolved = resolve(seeded);
    expect(resolved.ok(), "the schema-level seed resolves what no element pins");
    const auto expected = s7::Rate::from_hz(100);
    expect(resolved.rates.at("one") == expected && resolved.rates.at("gain") == expected, "seed applies to the whole component");
}

void test_components_are_resolved_independently()
{
    s7::Schema schema{"two_components"};
    add(schema, "feed", "Constant", {{"value", 1}});
    add(schema, "ramp", "EulerIntegrator", {{"dt", 0.25}});
    add(schema, "double_it", "Gain", {{"factor", 2}});
    schema.connect("feed", 0, "ramp", 0);
    schema.connect("ramp", 0, "double_it", 0);
    add(schema, "one", "Constant", {{"value", 1}});
    add(schema, "triple_it", "Gain", {{"factor", 3}});
    schema.connect("one", 0, "triple_it", 0);

    const auto resolved = resolve(schema);
    expect(!resolved.ok(), "the unconnected, unpinned second component is still undetermined");
    bool names_pinned_component = false;
    for (const auto& error : resolved.errors) {
        names_pinned_component |= error.find("ramp") != std::string::npos || error.find("double_it") != std::string::npos;
    }
    expect(!names_pinned_component, "the resolved component is not the one reported as undetermined");
}

void test_isolated_pinned_element()
{
    s7::Schema schema{"oscillator"};
    add(schema, "osc", "AnalogOscillator", {{"sample_rate_hz", 48000}});
    const auto resolved = resolve(schema);
    expect(resolved.ok(), "a single pinned element with no connections still resolves");
    expect(resolved.rates.at("osc") == s7::Rate::from_hz(48000), "AnalogOscillator's own sample_rate_hz pins its rate");
}

} // namespace

int main()
try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    test_pinned_component_agrees();
    test_conflict_is_reported();
    test_undetermined_without_a_pin_or_seed();
    test_components_are_resolved_independently();
    test_isolated_pinned_element();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core rate inference smoke: " << ex.what() << '\n';
    return 1;
}
