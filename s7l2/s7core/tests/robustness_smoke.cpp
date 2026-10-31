// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Regressions for defects found in an s7core review: recursive schema
// libraries, integer parameter bounds, oversized variadic counts, Convert with
// narrow floating types, PhaseWrap range, nested-schema rates, schema rate
// normalization, the F16 oscillator rate and deep graph walks.

#include <s7/builtin_element_registry.hpp>
#include <s7/control_elements.hpp>
#include <s7/convert.hpp>
#include <s7/core.hpp>
#include <s7/element_registry.hpp>
#include <s7/schema_library.hpp>

#include <boost/json.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace {
using s7::SignalType;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Fn>
std::string error_of(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}

boost::json::value library(const char* json) { return boost::json::parse(json); }

void recursive_libraries() {
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto registered = [&](const char* json) {
        return error_of([&] { s7::SchemaLibraryLoader::register_library(s7::SchemaLibraryLoader::load_from_json(library(json)), registry); });
    };
    // A type that contains itself, and two that contain each other.
    auto error = registered(R"({"document_kind":"s7.element_library","name":"self","elements":[
        {"type_name":"ReviewLoop","schema":{"name":"loop","dt":1.0,"elements":[{"id":"inner","type":"ReviewLoop"}],"connections":[]}}]})");
    require(error.find("contains itself") != std::string::npos, "Self-referential library type accepted: " + error);
    require(!registry.find("ReviewLoop"), "Rejected library type was registered");
    error = registered(R"({"document_kind":"s7.element_library","name":"mutual","elements":[
        {"type_name":"ReviewA","schema":{"name":"a","dt":1.0,"elements":[{"id":"b","type":"ReviewB"}],"connections":[]}},
        {"type_name":"ReviewB","schema":{"name":"b","dt":1.0,"elements":[{"id":"nested","schema":{"name":"n","dt":1.0,
            "elements":[{"id":"a","type":"ReviewA"}],"connections":[]}}],"connections":[]}}]})");
    require(error.find("contains itself") != std::string::npos, "Mutually recursive library types accepted: " + error);

    // Across libraries: the later library closes the cycle and is rejected.
    error = registered(R"({"document_kind":"s7.element_library","name":"first","elements":[
        {"type_name":"ReviewOuter","schema":{"name":"outer","dt":1.0,"elements":[{"id":"x","type":"ReviewLater"}],"connections":[]}}]})");
    require(error.empty(), "Library referring to a not yet registered type rejected: " + error);
    error = registered(R"({"document_kind":"s7.element_library","name":"second","elements":[
        {"type_name":"ReviewLater","schema":{"name":"later","dt":1.0,"elements":[{"id":"y","type":"ReviewOuter"}],"connections":[]}}]})");
    require(error.find("contains itself") != std::string::npos, "Cross-library cycle accepted: " + error);

    // Acyclic nesting still registers, builds and compiles.
    error = registered(R"({"document_kind":"s7.element_library","name":"acyclic","elements":[
        {"type_name":"ReviewLeaf","schema":{"name":"leaf","dt":1.0,"elements":[{"id":"g","type":"Gain","params":{"factor":2.0}}],
            "connections":[],"exposed_inputs":[{"name":"in","element":"g","port":0}],"exposed_outputs":[{"name":"out","element":"g","port":0}]}},
        {"type_name":"ReviewTwice","schema":{"name":"twice","dt":1.0,"elements":[{"id":"a","type":"ReviewLeaf"},{"id":"b","type":"ReviewLeaf"}],
            "connections":[{"from_element":"a","from_port":0,"to_element":"b","to_port":0}],
            "exposed_inputs":[{"name":"in","element":"a","port":0}],"exposed_outputs":[{"name":"out","element":"b","port":0}]}}]})");
    require(error.empty(), "Acyclic library rejected: " + error);
    const auto top = s7::SchemaLibraryLoader::build_schema(library(R"({"name":"top","dt":1.0,
        "elements":[{"id":"c","type":"Constant","params":{"value":1.5}},{"id":"t","type":"ReviewTwice"}],
        "connections":[{"from_element":"c","from_port":0,"to_element":"t","to_port":0}]})"), registry);
    s7::NodeRuntime runtime(s7::Compiler::compile(top));
    runtime.step();
    require(runtime.real_output_of("t") == 6.0, "Nested library elements computed the wrong value");
    registry.unregister({"ReviewOuter", "ReviewLeaf", "ReviewTwice"});
}

void integer_parameter_bounds() {
    s7::ElementMetadata metadata;
    metadata.type_name = "Probe";
    const auto get = [&](double value) {
        const s7::ElementCreationContext context{metadata, boost::json::object{{"n", value}}};
        return context.get_int("n");
    };
    require(!error_of([&] { (void)get(0x1p63); }).empty(), "2^63 accepted as int64");
    require(!error_of([&] { (void)get(std::numeric_limits<double>::infinity()); }).empty(), "Infinity accepted as int64");
    require(get(-0x1p63) == std::numeric_limits<std::int64_t>::min(), "-2^63 rejected");
    require(get(std::nextafter(0x1p63, 0.0)) == std::int64_t{0x7ffffffffffffc00}, "Largest double below 2^63 changed");
}

void variadic_count_bounds() {
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto* descriptor = registry.find("MinMax"); require(descriptor, "MinMax not registered");
    // A huge count must neither allocate a rule per port nor pass validation.
    const auto spec = s7::make_element_spec("m", *descriptor, {{"input_count", 200000000}});
    require(spec.generic.input_count <= descriptor->metadata().variadic_inputs.max_count + 1,
            "Oversized variadic count reached type inference");
    s7::Schema schema("oversized");
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element(spec);
    const auto result = s7::Validator::validate(schema);
    require(!result.ok(), "Oversized variadic count validated");
}

template <SignalType From, SignalType To>
auto convert(const s7::signal_storage_t<From>& value, s7::ConvertMode mode,
             s7::ConvertOverflow overflow = s7::ConvertOverflow::Saturate) {
    s7::ConvertOptions options{mode};
    options.overflow = overflow;
    return s7::convert_value<From, To>(value, options);
}

void narrow_float_conversions() {
    using s7::ConvertMode;
    using s7::float16_t;
    constexpr auto value = ConvertMode::Value;
    constexpr auto normalized = ConvertMode::Normalized;
    const auto inf16 = std::numeric_limits<float16_t>::infinity();

    // F16 -> wide integers: infinities are out of range, normalized scaling does not overflow.
    require(convert<SignalType::F16, SignalType::I32>(-inf16, value) == std::numeric_limits<std::int32_t>::min(), "F16 -inf -> I32");
    require(convert<SignalType::F16, SignalType::I32>(inf16, value) == std::numeric_limits<std::int32_t>::max(), "F16 +inf -> I32");
    require(!error_of([&] { (void)convert<SignalType::F16, SignalType::I64>(-inf16, value, s7::ConvertOverflow::Fault); }).empty(),
            "F16 -inf -> I64 did not fault");
    require(convert<SignalType::F16, SignalType::I32>(float16_t(0.5), normalized) == (std::int32_t{1} << 30), "F16 0.5 -> I32 normalized");
    require(convert<SignalType::F16, SignalType::I64>(float16_t(-0.25), normalized) == -(std::int64_t{1} << 61), "F16 -0.25 -> I64 normalized");
    require(convert<SignalType::F16, SignalType::I128>(float16_t(0.5), normalized) == (s7::int128_t{1} << 126), "F16 0.5 -> I128 normalized");
    require(convert<SignalType::F16, SignalType::I16>(float16_t(1000), value) == 1000, "F16 -> I16 value unchanged");
    const auto complex = convert<SignalType::C16, SignalType::CI32>(std::complex<float16_t>(float16_t(0.5), float16_t(-0.25)), normalized);
    require(complex.re == (1 << 30) && complex.im == -(1 << 29), "C16 -> CI32 normalized");
    require(convert<SignalType::F32, SignalType::U128>(std::numeric_limits<float>::infinity(), value) == s7::kUint128Max, "F32 +inf -> U128");
    require(convert<SignalType::F32, SignalType::U128>(0.5f, normalized) == (s7::uint128_t{1} << 127), "F32 0.5 -> U128 normalized");

    // Integers -> narrow floats: scale before narrowing.
    const double small = static_cast<double>(convert<SignalType::I32, SignalType::F16>(100000, normalized));
    require(std::abs(small - 100000.0 / 0x1p31) < 1e-7, "I32 -> F16 normalized saturated");
    require(convert<SignalType::U16, SignalType::F16>(65535, normalized) == float16_t(1), "U16 -> F16 normalized");
    require(convert<SignalType::I16, SignalType::F16>(-32768, normalized) == float16_t(-1), "I16 -> F16 normalized");
    require(convert<SignalType::U128, SignalType::F32>(s7::kUint128Max, normalized) == 1.0f, "U128 -> F32 normalized");
    require(convert<SignalType::I64, SignalType::F16>(std::int64_t{1} << 62, normalized) == float16_t(0.5), "I64 -> F16 normalized");
    // Value mode keeps its range rules.
    require(convert<SignalType::U16, SignalType::F16>(65535, value) == std::numeric_limits<float16_t>::max(), "U16 -> F16 value saturates");
    require(!error_of([&] { (void)convert<SignalType::U16, SignalType::F16>(65535, value, s7::ConvertOverflow::Fault); }).empty(),
            "U16 -> F16 value overflow did not fault");
    require(convert<SignalType::I64, SignalType::F64>(std::int64_t{1} << 62, normalized) == 0.5, "I64 -> F64 normalized");
}

void phase_wrap_range() {
    for (const double period : {1.0, 0.7, 2 * 3.141592653589793}) {
        for (const double lower : {-0.9, -0.5 * period, 0.0, 0.3, 0.9, 5.5}) {
            s7::PhaseWrap wrap(period, lower);
            double input = 0;
            const s7::InputBinding binding = &input;
            wrap.bind_inputs(std::span(&binding, 1));
            for (double x = -12.0; x <= 12.0; x += 0.37) {
                input = x;
                wrap.step();
                const double out = *static_cast<const double*>(wrap.outputs()[0]);
                require(out >= lower && out < lower + period, "PhaseWrap left [lower, lower + period)");
                const double turns = (out - x) / period;
                require(std::abs(turns - std::round(turns)) < 1e-9, "PhaseWrap changed the phase");
            }
        }
    }
    s7::PhaseWrap wrap(1.0, 0.9);
    double input = -0.9;
    const s7::InputBinding binding = &input;
    wrap.bind_inputs(std::span(&binding, 1));
    wrap.step();
    require(std::abs(*static_cast<const double*>(wrap.outputs()[0]) - 1.1) < 1e-12, "PhaseWrap(-0.9, period 1, lower 0.9)");
}

s7::Schema rate_graph(double inner_dt) {
    s7::Schema parent("parent");
    parent.add_element("zero", [] { return std::make_unique<s7::Constant>(0.0); });
    parent.add_element("src", [] { return std::make_unique<s7::EulerIntegrator>(0.01); });
    parent.add_element("child", [inner_dt] {
        auto inner = std::make_unique<s7::Schema>("child");
        inner->add_element("i", [inner_dt] { return std::make_unique<s7::EulerIntegrator>(inner_dt); });
        inner->expose_input("in", SignalType::F64, "i", 0);
        inner->expose_output("out", SignalType::F64, "i", 0);
        return inner;
    });
    parent.add_element("sink", [] { return std::make_unique<s7::EulerIntegrator>(0.01); });
    parent.connect("zero", 0, "src", 0);
    parent.connect("src", 0, "child", 0);
    parent.connect("child", 0, "sink", 0);
    return parent;
}

void nested_schema_rates() {
    // 100 Hz parent around a nested schema whose inner integrator runs at 1 kHz.
    const auto error = error_of([] { (void)s7::Compiler::compile(rate_graph(0.001)); });
    require(error.find("parent graph rate") != std::string::npos, "Nested rate mismatch accepted: " + error);
    s7::NodeRuntime runtime(s7::Compiler::compile(rate_graph(0.01)));
    runtime.set_model_time_rate(s7::Rate::from_hz(100));
    runtime.step();
}

void schema_rate_normalization() {
    s7::Schema schema("unreduced");
    schema.set_rate(s7::Rate{2000, 2});
    require(schema.rate() == s7::Rate{1000, 1}, "Schema rate not reduced");
    require(!error_of([&] { schema.set_rate(s7::Rate{0, 1}); }).empty(), "Zero schema rate accepted");
    schema.add_element("i", [] { return std::make_unique<s7::EulerIntegrator>(0.001); });
    schema.add_element("c", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.connect("c", 0, "i", 0);
    schema.expose_output("o", SignalType::F64, "i", 0);
    schema.bind_inputs({});
    schema.step();
}

void f16_oscillator_rate() {
    using s7::float16_t;
    s7::AnalogOscillatorT<SignalType::F16> cd(float16_t(1), float16_t(1000), 44100.0);
    require(cd.declared_rate() == s7::Rate::from_hz(44100), "F16 oscillator rate rounded");
    s7::AnalogOscillatorT<SignalType::F16> fast(float16_t(1), float16_t(1000), 96000.0);
    require(fast.declared_rate() == s7::Rate::from_hz(96000), "F16 oscillator above the F16 range");
    // Through the registry: the oscillator follows the schema's 44.1 kHz rate.
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto* descriptor = registry.find("AnalogOscillator"); require(descriptor, "AnalogOscillator not registered");
    s7::Schema schema("f16_tone");
    schema.set_rate(s7::Rate::from_hz(44100));
    schema.add_element(s7::make_element_spec("osc", *descriptor, {{"type", "F16"}, {"frequency_hz", 1000}}));
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_rate(s7::Rate::from_hz(44100));
    runtime.step();
    runtime.step();
    const double second = static_cast<double>(runtime.output_of<SignalType::F16>("osc"));
    require(std::abs(second - std::sin(2 * 3.141592653589793 * 1000 / 44100)) < 2e-3, "F16 oscillator phase step");
}

// Runs fn on a thread with a small stack, so a recursive graph walk over a
// long chain would overflow it.
void with_small_stack(const std::function<void()>& fn) {
#if defined(__linux__)
    struct Call {
        const std::function<void()>* fn;
        std::string error;
    } call{&fn, {}};
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 256 * 1024);
    pthread_t thread;
    const auto body = [](void* argument) -> void* {
        auto* call = static_cast<Call*>(argument);
        try {
            (*call->fn)();
        } catch (const std::exception& error) {
            call->error = error.what();
        }
        return nullptr;
    };
    require(pthread_create(&thread, &attributes, body, &call) == 0, "Cannot start a small-stack thread");
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attributes);
    require(call.error.empty(), call.error);
#else
    fn();
#endif
}

void deep_graph_walks() {
    constexpr int length = 50000;
    with_small_stack([] {
        // A long direct chain: cycle components and rate union-find.
        s7::Schema chain("chain");
        chain.set_rate(s7::Rate::from_hz(1));
        chain.add_element("g0", [] { return std::make_unique<s7::Constant>(1.0); });
        for (int i = 1; i < length; ++i) {
            chain.add_element("g" + std::to_string(i), [] { return std::make_unique<s7::Gain>(1.0); });
            chain.connect("g" + std::to_string(i - 1), 0, "g" + std::to_string(i), 0);
        }
        require(s7::Validator::validate(chain).ok(), "Long chain did not validate");

        // A long derivative chain feeding an implicit trapezoid integrator.
        s7::Schema loop("trapezoid_chain");
        loop.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); });
        std::string previous = "x";
        for (int i = 0; i < length; ++i) {
            const auto id = "d" + std::to_string(i);
            loop.add_element(id, [factor = i == 0 ? -1.0 : 1.0] { return std::make_unique<s7::Gain>(factor); });
            loop.connect(previous, 0, id, 0);
            previous = id;
        }
        loop.connect(previous, 0, "x", 0);
        const auto result = s7::Validator::validate(loop);
        require(result.ok(), "Long trapezoid derivative chain did not validate: " +
                (result.errors.empty() ? std::string{} : result.errors.front()));
    });
}
} // namespace

int main() try {
    recursive_libraries();
    integer_parameter_bounds();
    variadic_count_bounds();
    narrow_float_conversions();
    phase_wrap_range();
    nested_schema_rates();
    schema_rate_normalization();
    f16_oscillator_rate();
    deep_graph_walks();
    std::cout << "Library recursion, parameter bounds, conversions, PhaseWrap, rates and deep graphs passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
