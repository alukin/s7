#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include <boost/json.hpp>

#include "s7/core.hpp"

namespace {

namespace json = boost::json;
using s7::SignalType;

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

void test_forward_inference_and_saturation()
{
    for (const auto* overflow : {"saturate", "wrap"}) {
        s7::Schema schema{"forward"};
        add(schema, "source", "Constant", {{"type", "I16"}, {"value", 20000}});
        add(schema, "gain", "Gain", {{"factor", 2}, {"overflow", overflow}});
        add(schema, "delay", "Delay");
        schema.connect("source", 0, "gain", 0);
        schema.connect("gain", 0, "delay", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        expect(runtime.output_type_of("gain") == SignalType::I16, "Gain inferred I16");
        expect(runtime.output_type_of("delay") == SignalType::I16, "Delay inferred I16");
        const auto expected = std::string(overflow) == "saturate" ? std::int16_t{32767} : static_cast<std::int16_t>(40000 - 65536);
        expect(runtime.output_of<SignalType::I16>("gain") == expected, std::string("I16 gain ") + overflow);
    }
}

void test_backward_inference_and_default()
{
    s7::Schema pinned{"backward"};
    add(pinned, "source", "Constant", {{"value", 0.25}});
    add(pinned, "gain", "Gain", {{"type", "F32"}, {"factor", 4}});
    pinned.connect("source", 0, "gain", 0);
    s7::NodeRuntime runtime{s7::Compiler::compile(pinned)};
    runtime.step();
    expect(runtime.output_type_of("source") == SignalType::F32, "Constant inferred backwards as F32");
    expect(runtime.output_of<SignalType::F32>("gain") == 1.0f, "F32 gain value");

    s7::Schema open{"default"};
    add(open, "source", "Constant", {{"value", 1.5}});
    add(open, "gain", "Gain", {{"factor", 2}});
    open.connect("source", 0, "gain", 0);
    s7::NodeRuntime defaulted{s7::Compiler::compile(open)};
    defaulted.step();
    expect(defaulted.output_type_of("gain") == SignalType::F64, "undetermined types default to F64");
    expect(defaulted.real_output_of("gain") == 3.0, "F64 gain value");
}

void test_feedback_loop()
{
    // counter: sum = 1 + previous(sum), all I32 from the pinned constant.
    s7::Schema schema{"counter"};
    add(schema, "one", "Constant", {{"type", "I32"}, {"value", 1}});
    add(schema, "sum", "Adder");
    add(schema, "previous", "Delay");
    schema.connect("one", 0, "sum", 0);
    schema.connect("previous", 0, "sum", 1);
    schema.connect("sum", 0, "previous", 0);
    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    for (int step = 1; step <= 3; ++step) {
        runtime.step();
        expect(runtime.output_of<SignalType::I32>("sum") == step, "counter step " + std::to_string(step));
    }
    expect(runtime.output_type_of("previous") == SignalType::I32, "type propagates around the loop");
}

void test_conflicts_and_errors()
{
    s7::Schema mixed{"mixed"};
    add(mixed, "a", "Constant", {{"type", "I16"}});
    add(mixed, "b", "Constant", {{"type", "F32"}});
    add(mixed, "sum", "Adder");
    mixed.connect("a", 0, "sum", 0);
    mixed.connect("b", 0, "sum", 1);
    expect_error(mixed, "Type conflict", "two types meet at one Adder");
    expect_error(mixed, "Insert a Convert element", "conflict suggests Convert");

    s7::Schema unsupported{"unsupported"};
    add(unsupported, "iq", "Constant", {{"type", "CU8"}, {"value", json::array{128, 128}}});
    add(unsupported, "gain", "Gain");
    unsupported.connect("iq", 0, "gain", 0);
    expect_error(unsupported, "Type conflict", "Gain has no CU8 arithmetic");

    s7::Schema pinned_unsupported{"pinned"};
    add(pinned_unsupported, "gain", "Gain", {{"type", "CU8"}});
    expect_error(pinned_unsupported, "is not supported", "pinning an unsupported type");

    s7::Schema concrete{"concrete"};
    add(concrete, "byte", "Constant", {{"type", "I8"}});
    concrete.add_element("legacy", [] { return std::make_unique<s7::Gain>(2.0); });
    concrete.connect("byte", 0, "legacy", 0);
    expect_error(concrete, "requires {F64}", "concrete F64 element seeds its type");

    s7::Schema bad_value{"bad_value"};
    add(bad_value, "gain", "Gain", {{"type", "U8"}, {"factor", -1}});
    add(bad_value, "source", "Constant");
    bad_value.connect("source", 0, "gain", 0);
    expect_error(bad_value, "negative value for unsigned U8", "parameters convert exactly");

    s7::Schema fractional{"fractional"};
    add(fractional, "c", "Constant", {{"type", "I32"}, {"value", 1.5}});
    expect_error(fractional, "expected an integer", "no rounding of integer parameters");
}

void test_wide_and_complex_types()
{
    s7::Schema wide{"wide"};
    add(wide, "big", "Constant", {{"type", "I128"}, {"value", "170141183460469231731687303715884105727"}});
    add(wide, "double_it", "Gain", {{"factor", 2}});
    add(wide, "third", "Constant", {{"type", "F128"}, {"value", 3}});
    add(wide, "scale", "Gain", {{"factor", 0.5}});
    wide.connect("big", 0, "double_it", 0);
    wide.connect("third", 0, "scale", 0);
    s7::NodeRuntime runtime{s7::Compiler::compile(wide)};
    runtime.step();
    expect(runtime.output_of<SignalType::I128>("double_it") == s7::kInt128Max, "I128 saturates at its max");
    expect(runtime.output_of<SignalType::F128>("scale") == 1.5f128, "F128 arithmetic");

    // Raw SDR-style I/Q: (100 + 100i)^2 = 0 + 20000i saturates to (0, 127) in CI8.
    s7::Schema iq{"iq"};
    add(iq, "sample", "Constant", {{"type", "CI8"}, {"value", json::array{100, 100}}});
    add(iq, "square", "Multiply");
    iq.connect("sample", 0, "square", 0);
    iq.connect("sample", 0, "square", 1);
    s7::NodeRuntime iq_runtime{s7::Compiler::compile(iq)};
    iq_runtime.step();
    const auto squared = iq_runtime.output_of<SignalType::CI8>("square");
    expect(squared.re == 0 && squared.im == 127, "CI8 product saturates");
}

} // namespace

int main()
try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    test_forward_inference_and_saturation();
    test_backward_inference_and_default();
    test_feedback_loop();
    test_conflicts_and_errors();
    test_wide_and_complex_types();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core type inference smoke: " << ex.what() << '\n';
    return 1;
}
