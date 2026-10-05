#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
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

void test_integrators()
{
    // Rectangle rule on a constant and trapezoid rule on the resulting ramp:
    // x[n] = n*dt, and the trapezoid rule is exact for linear input: y[n] = (n*dt)^2 / 2.
    s7::Schema schema{"integrators"};
    add(schema, "one", "Constant", {{"value", 1}});
    add(schema, "ramp", "EulerIntegrator", {{"dt", 0.5}});
    add(schema, "area", "TrapezoidIntegrator", {{"dt", 0.5}});
    schema.connect("one", 0, "ramp", 0);
    schema.connect("ramp", 0, "area", 0);
    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    for (int n = 0; n < 6; ++n) {
        runtime.step();
        const double t = n * 0.5;
        expect(runtime.real_output_of("ramp") == t, "Euler ramp at step " + std::to_string(n));
        expect(runtime.real_output_of("area") == t * t / 2, "trapezoid area at step " + std::to_string(n));
    }

    // x' = v, v' = -x closed through two EulerIntegrators, no Delay, in F128.
    // Forward Euler multiplies x^2 + v^2 by exactly (1 + dt^2) per step.
    s7::Schema ode{"harmonic"};
    add(ode, "x", "EulerIntegrator", {{"type", "F128"}, {"dt", 0.001}, {"initial_value", 1}});
    add(ode, "v", "EulerIntegrator", {{"dt", 0.001}});
    add(ode, "minus", "Gain", {{"factor", -1}});
    ode.connect("v", 0, "x", 0);
    ode.connect("x", 0, "minus", 0);
    ode.connect("minus", 0, "v", 0);
    s7::NodeRuntime oscillator{s7::Compiler::compile(ode)};
    constexpr int steps = 1000;
    for (int n = 0; n <= steps; ++n) {
        oscillator.step();
    }
    const auto x = oscillator.output_of<SignalType::F128>("x");
    const auto v = oscillator.output_of<SignalType::F128>("v");
    std::float128_t growth = 1;
    const auto dt = static_cast<std::float128_t>(0.001); // the parameter's value, as converted
    for (int n = 0; n < steps; ++n) {
        growth *= 1 + dt * dt;
    }
    const auto error = (x * x + v * v) - growth;
    expect(error < 1e-28f128 && error > -1e-28f128, "F128 Euler energy growth matches (1 + dt^2)^n");
    expect(oscillator.output_type_of("minus") == SignalType::F128, "F128 inferred through the loop");

    // A loop closed only through TrapezoidIntegrator has no causal memory.
    s7::Schema algebraic{"algebraic"};
    add(algebraic, "y", "TrapezoidIntegrator", {{"dt", 0.1}});
    add(algebraic, "minus", "Gain", {{"factor", -1}});
    algebraic.connect("y", 0, "minus", 0);
    algebraic.connect("minus", 0, "y", 0);
    expect_error(algebraic, "Cycle detected without causal memory", "trapezoid loop needs causal memory");
}

template <SignalType From, SignalType To>
s7::signal_storage_t<To> convert(const s7::signal_storage_t<From>& value, s7::ConvertMode mode = s7::ConvertMode::Value,
                                 s7::ConvertRounding rounding = s7::ConvertRounding::HalfEven,
                                 s7::ConvertOverflow overflow = s7::ConvertOverflow::Saturate)
{
    return s7::convert_value<From, To>(value, s7::ConvertOptions{mode, rounding, overflow});
}

template <typename F>
bool throws(F&& f)
{
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void test_convert()
{
    using M = s7::ConvertMode;
    using R = s7::ConvertRounding;
    using O = s7::ConvertOverflow;

    // value mode, integer range rules
    expect(convert<SignalType::U16, SignalType::I16>(40000) == 32767, "U16 -> I16 saturates");
    expect(throws([] { (void)convert<SignalType::U16, SignalType::I16>(40000, M::Value, R::HalfEven, O::Fault); }),
           "U16 -> I16 can fault");
    expect(convert<SignalType::U128, SignalType::I128>(s7::kUint128Max) == s7::kInt128Max, "U128 max -> I128 saturates");
    expect(convert<SignalType::I128, SignalType::U128>(-1) == 0, "I128 -1 -> U128 saturates to 0");
    expect(convert<SignalType::I32, SignalType::I12>(-5000) == -2048, "I32 -> I12 saturates at 12 bits");

    // value mode, float -> integer rounding and special values
    expect(convert<SignalType::F64, SignalType::I32>(2.5) == 2 && convert<SignalType::F64, SignalType::I32>(-2.5) == -2,
           "half even");
    expect(convert<SignalType::F64, SignalType::I32>(2.9, M::Value, R::TowardZero) == 2, "toward zero");
    expect(convert<SignalType::F64, SignalType::I8>(std::nan("")) == 0, "NaN -> 0 when saturating");
    expect(throws([] { (void)convert<SignalType::F64, SignalType::I8>(std::nan(""), M::Value, R::HalfEven, O::Fault); }),
           "NaN faults");
    expect(convert<SignalType::F64, SignalType::U8>(1e300) == 255, "large value saturates");
    expect(convert<SignalType::F128, SignalType::I128>(std::ldexp(1.0f128, 100)) == (static_cast<s7::int128_t>(1) << 100),
           "F128 -> I128 exact");
    expect(convert<SignalType::I128, SignalType::F128>(static_cast<s7::int128_t>(1) << 100) == std::ldexp(1.0f128, 100),
           "I128 -> F128 exact");

    // float narrowing overflow
    expect(convert<SignalType::F64, SignalType::F32>(1e300) == std::numeric_limits<float>::max(), "F64 -> F32 saturates");
    expect(convert<SignalType::F64, SignalType::F16>(1e6) == std::numeric_limits<std::float16_t>::max(), "F64 -> F16 saturates");
    expect(throws([] { (void)convert<SignalType::F64, SignalType::F32>(1e300, M::Value, R::HalfEven, O::Fault); }),
           "float overflow can fault");

    // normalized mode
    expect(convert<SignalType::I16, SignalType::F32>(16384, M::Normalized) == 0.5f, "I16 code -> F32");
    expect(convert<SignalType::F32, SignalType::I8>(0.5f, M::Normalized) == 64, "F32 -> I8 code");
    expect(convert<SignalType::F64, SignalType::I8>(1.0, M::Normalized) == 127, "1.0 saturates to max code");
    expect(convert<SignalType::I16, SignalType::I8>(16384, M::Normalized) == 64, "I16 -> I8 rescales");
    expect(convert<SignalType::I16, SignalType::I8>(16512, M::Normalized) == 64, "I16 -> I8 rounds half to even");
    expect(convert<SignalType::I8, SignalType::I16>(-128, M::Normalized) == -32768, "I8 -> I16 rescales");
    expect(convert<SignalType::U8, SignalType::F64>(255, M::Normalized) == 255.0 / 256.0, "unsigned codes are unipolar");
    expect(throws([] { (void)convert<SignalType::U8, SignalType::I8>(1, M::Normalized); }),
           "signed <-> unsigned normalized is refused");

    // complex, component-wise; CU8 is offset binary
    using CU8 = s7::signal_storage_t<SignalType::CU8>;
    using CI8 = s7::signal_storage_t<SignalType::CI8>;
    expect(convert<SignalType::CU8, SignalType::CI8>(CU8{128, 0}, M::Normalized) == CI8{0, -128}, "CU8 -> CI8 offset binary");
    const auto iq = convert<SignalType::CU8, SignalType::C32>(CU8{255, 0}, M::Normalized);
    expect(iq.real() == 127.0f / 128.0f && iq.imag() == -1.0f, "CU8 -> C32 normalized");
    using CI16 = s7::signal_storage_t<SignalType::CI16>;
    expect(convert<SignalType::CI16, SignalType::C64>(CI16{300, -5}) == std::complex<double>{300, -5}, "CI16 -> C64 value");
    expect(convert<SignalType::C64, SignalType::CI8>(std::complex<double>{1.5, 2.5}) == CI8{2, 2}, "C64 -> CI8 rounds");
    expect(throws([] { (void)s7::make_convert(SignalType::F64, SignalType::C64, {}); }), "real -> complex is refused");

    // In a graph: CI8 I/Q widened to CI32 so the product does not saturate;
    // the Convert's output type is inferred from the pinned Multiply.
    s7::Schema widen{"widen"};
    add(widen, "sample", "Constant", {{"type", "CI8"}, {"value", json::array{100, 100}}});
    add(widen, "wide", "Convert", {{"mode", "value"}});
    add(widen, "square", "Multiply", {{"type", "CI32"}});
    widen.connect("sample", 0, "wide", 0);
    widen.connect("wide", 0, "square", 0);
    widen.connect("wide", 0, "square", 1);
    s7::NodeRuntime runtime{s7::Compiler::compile(widen)};
    runtime.step();
    const auto squared = runtime.output_of<SignalType::CI32>("square");
    expect(squared.re == 0 && squared.im == 20000, "widened CI32 product is exact");
    expect(runtime.output_type_of("wide") == SignalType::CI32, "Convert output type inferred");

    s7::Schema no_mode{"no_mode"};
    add(no_mode, "c", "Convert", {{"from", "I16"}, {"to", "F32"}});
    expect_error(no_mode, "explicit 'mode'", "Convert requires a mode");
}

static_assert(s7::widens_exactly(SignalType::I8, SignalType::I32));
static_assert(s7::widens_exactly(SignalType::U16, SignalType::I32) && !s7::widens_exactly(SignalType::U16, SignalType::I16));
static_assert(s7::widens_exactly(SignalType::I16, SignalType::F32) && !s7::widens_exactly(SignalType::I32, SignalType::F32));
static_assert(s7::widens_exactly(SignalType::I64, SignalType::F128) && !s7::widens_exactly(SignalType::I64, SignalType::F64));
static_assert(s7::widens_exactly(SignalType::CI8, SignalType::CI32) && !s7::widens_exactly(SignalType::CI8, SignalType::I32));
static_assert(!s7::widens_exactly(SignalType::F64, SignalType::I64) && !s7::widens_exactly(SignalType::CU8, SignalType::CI16));

void test_accumulator_type()
{
    // A hardware-style FIR: CI8 samples, CI32 accumulator. Without it, CI8 saturates.
    for (const bool wide : {false, true}) {
        s7::Schema schema{"fir"};
        add(schema, "iq", "Constant", {{"type", "CI8"}, {"value", json::array{100, 100}}});
        json::object fir{{"taps", json::array{1, 1, 1}}};
        if (wide) {
            fir["accumulator_type"] = "CI32";
        }
        add(schema, "fir", "FirFilter", fir);
        schema.connect("iq", 0, "fir", 0);
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        for (int n = 0; n < 3; ++n) {
            runtime.step();
        }
        if (wide) {
            expect(runtime.output_type_of("fir") == SignalType::CI32, "FIR output has the accumulator type");
            const auto y = runtime.output_of<SignalType::CI32>("fir");
            expect(y.re == 300 && y.im == 300, "CI32 accumulator holds the sum");
        } else {
            const auto y = runtime.output_of<SignalType::CI8>("fir");
            expect(y.re == 127 && y.im == 127, "CI8 accumulator saturates");
        }
    }

    // Integrator over I8 samples accumulating in I64; Adder of I16 inputs in F32.
    s7::Schema schema{"accumulate"};
    add(schema, "step", "Constant", {{"type", "I8"}, {"value", 100}});
    add(schema, "total", "Integrator", {{"accumulator_type", "I64"}});
    add(schema, "a", "Constant", {{"type", "I16"}, {"value", 3}});
    add(schema, "b", "Constant", {{"type", "I16"}, {"value", 4}});
    add(schema, "mix", "Adder", {{"accumulator_type", "F32"}, {"weights", json::array{0.5, 0.25}}});
    schema.connect("step", 0, "total", 0);
    schema.connect("a", 0, "mix", 0);
    schema.connect("b", 0, "mix", 1);
    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    for (int n = 0; n < 10; ++n) {
        runtime.step();
    }
    expect(runtime.output_of<SignalType::I64>("total") == 1000, "I8 summed in I64");
    expect(runtime.output_of<SignalType::F32>("mix") == 2.5f, "I16 summed in F32 with F32 weights");

    s7::Schema narrow{"narrow"};
    add(narrow, "x", "Constant", {{"type", "I16"}});
    add(narrow, "sum", "Integrator", {{"accumulator_type", "I8"}});
    narrow.connect("x", 0, "sum", 0);
    expect_error(narrow, "cannot hold every I16 value exactly", "too narrow accumulator is refused");
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
    test_integrators();
    test_convert();
    test_accumulator_type();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core type inference smoke: " << ex.what() << '\n';
    return 1;
}
