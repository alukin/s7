// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Exposed outputs of a nested Schema: values, order, types, publication timing,
// recompilation, moves and faults. Nested results are compared bit for bit
// with the equivalent flat graph, which runs the same arithmetic in the same
// causal order.

#include <s7/core.hpp>
#include <s7/schema_library.hpp>

#include <boost/json.hpp>

#include <array>
#include <complex>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using F64 = s7::RealValue;
using C64 = s7::ComplexValue;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Fn>
void require_throws(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

F64 f64_output(const s7::Element& element, std::size_t port) {
    return s7::bound_value<s7::SignalType::F64>(element.outputs()[port]);
}

void bind_one(s7::Schema& schema, const F64& input) {
    const std::array<s7::InputBinding, 1> bindings{s7::bind_input<s7::SignalType::F64>(input)};
    schema.bind_inputs(bindings);
}

std::unique_ptr<s7::Schema> one_gain(double factor) {
    auto inner = std::make_unique<s7::Schema>("one_gain");
    inner->set_rate(s7::Rate::from_hz(1));
    inner->add_element("gain", [factor] { return std::make_unique<s7::Gain>(factor); });
    inner->expose_input("in", s7::SignalType::F64, "gain", 0);
    inner->expose_output("out", s7::SignalType::F64, "gain", 0);
    return inner;
}

// y = factor * (u + y[n-1]), the feedback closed by a Delay.
void add_feedback_cell(s7::Schema& schema, const std::string& prefix, double factor) {
    schema.add_element(prefix + "sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element(prefix + "gain", [factor] { return std::make_unique<s7::Gain>(factor); });
    schema.add_element(prefix + "delay", [] { return std::make_unique<s7::Delay>(0.0); });
    schema.connect(prefix + "delay", 0, prefix + "sum", 1);
    schema.connect(prefix + "sum", 0, prefix + "gain", 0);
    schema.connect(prefix + "gain", 0, prefix + "delay", 0);
}

std::unique_ptr<s7::Schema> feedback_schema(double factor) {
    auto inner = std::make_unique<s7::Schema>("feedback_cell");
    inner->set_rate(s7::Rate::from_hz(1));
    add_feedback_cell(*inner, "", factor);
    inner->expose_input("in", s7::SignalType::F64, "sum", 0);
    inner->expose_output("out", s7::SignalType::F64, "gain", 0);
    return inner;
}

// Exposed output is a Delay: visible one step after its input.
std::unique_ptr<s7::Schema> delayed_schema() {
    auto inner = std::make_unique<s7::Schema>("delayed");
    inner->set_rate(s7::Rate::from_hz(1));
    inner->add_element("delay", [] { return std::make_unique<s7::Delay>(-1.0); });
    inner->expose_input("in", s7::SignalType::F64, "delay", 0);
    inner->expose_output("out", s7::SignalType::F64, "delay", 0);
    return inner;
}

// A ramp source (1, 2, 3, ...) so every step has a distinct input.
void add_ramp(s7::Schema& schema) {
    schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("ramp", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("ramp_delay", [] { return std::make_unique<s7::Delay>(0.0); });
    schema.connect("one", 0, "ramp", 0);
    schema.connect("ramp_delay", 0, "ramp", 1);
    schema.connect("ramp", 0, "ramp_delay", 0);
}

s7::Schema root(const std::string& name) {
    s7::Schema schema(name);
    schema.set_rate(s7::Rate::from_hz(1));
    return schema;
}

void single_output_and_timing() {
    auto flat = root("flat");
    add_ramp(flat);
    flat.add_element("gain", [] { return std::make_unique<s7::Gain>(0.3); });
    flat.add_element("delay", [] { return std::make_unique<s7::Delay>(-1.0); });
    flat.add_element("after", [] { return std::make_unique<s7::Gain>(1.7); });
    flat.connect("ramp", 0, "gain", 0);
    flat.connect("ramp", 0, "delay", 0);
    flat.connect("gain", 0, "after", 0);

    auto nested = root("nested");
    add_ramp(nested);
    nested.add_element("gain", [] { return one_gain(0.3); });
    nested.add_element("delay", [] { return delayed_schema(); });
    nested.add_element("after", [] { return std::make_unique<s7::Gain>(1.7); });
    nested.connect("ramp", 0, "gain", 0);
    nested.connect("ramp", 0, "delay", 0);
    nested.connect("gain", 0, "after", 0);

    s7::NodeRuntime flatRuntime(s7::Compiler::compile(flat));
    s7::NodeRuntime nestedRuntime(s7::Compiler::compile(nested));
    for (int step = 1; step <= 50; ++step) {
        flatRuntime.step();
        nestedRuntime.step();
        for (const auto* id : {"gain", "delay", "after"}) {
            require(flatRuntime.real_output_of(id) == nestedRuntime.real_output_of(id),
                    std::string("Nested output differs from flat graph: ") + id);
        }
        // Direct output in the same step, causal memory one step later.
        require(nestedRuntime.real_output_of("gain") == 0.3 * step, "Direct nested output is not same-step");
        require(nestedRuntime.real_output_of("delay") == (step == 1 ? -1.0 : step - 1.0),
                "Nested causal-memory output has wrong publication step");
    }
}

// Exposed outputs keep their declared order and types; C64 crosses the
// boundary both ways.
std::unique_ptr<s7::Schema> mixed_schema() {
    auto inner = std::make_unique<s7::Schema>("mixed");
    inner->set_rate(s7::Rate::from_hz(1));
    inner->add_element("split", [] { return std::make_unique<s7::Gain>(1.0); });
    inner->add_element("triple", [] { return std::make_unique<s7::Gain>(3.0); });
    inner->add_element("join", [] { return std::make_unique<s7::ReImToComplex>(); });
    inner->add_element("rotate", [] { return std::make_unique<s7::ComplexGain>(C64{0.6, 0.8}); });
    inner->add_element("scale", [] { return std::make_unique<s7::ComplexGain>(C64{-0.25, 1.5}); });
    inner->connect("split", 0, "triple", 0);
    inner->connect("split", 0, "join", 0);
    inner->connect("triple", 0, "join", 1);
    inner->connect("join", 0, "rotate", 0);
    inner->expose_input("x", s7::SignalType::F64, "split", 0);
    inner->expose_input("w", s7::SignalType::C64, "scale", 0);
    inner->expose_output("a", s7::SignalType::F64, "triple", 0);
    inner->expose_output("z", s7::SignalType::C64, "join", 0);
    inner->expose_output("b", s7::SignalType::F64, "split", 0);
    inner->expose_output("zr", s7::SignalType::C64, "rotate", 0);
    inner->expose_output("ws", s7::SignalType::C64, "scale", 0);
    return inner;
}

void multiple_real_and_complex_outputs() {
    {
        auto probe = mixed_schema();
        const auto ports = probe->output_ports();
        const std::array<std::pair<const char*, s7::SignalType>, 5> expected{{
            {"a", s7::SignalType::F64},
            {"z", s7::SignalType::C64},
            {"b", s7::SignalType::F64},
            {"zr", s7::SignalType::C64},
            {"ws", s7::SignalType::C64},
        }};
        require(ports.size() == expected.size(), "Exposed output count changed");
        for (std::size_t i = 0; i < expected.size(); ++i) {
            require(ports[i].index == i && ports[i].name == expected[i].first && ports[i].type == expected[i].second,
                    "Exposed output order or type changed");
        }
    }

    auto flat = root("flat_mixed");
    add_ramp(flat);
    flat.add_element("w", [] { return std::make_unique<s7::ComplexConstant>(C64{0.1, -0.7}); });
    flat.add_element("split", [] { return std::make_unique<s7::Gain>(1.0); });
    flat.add_element("triple", [] { return std::make_unique<s7::Gain>(3.0); });
    flat.add_element("join", [] { return std::make_unique<s7::ReImToComplex>(); });
    flat.add_element("rotate", [] { return std::make_unique<s7::ComplexGain>(C64{0.6, 0.8}); });
    flat.add_element("scale", [] { return std::make_unique<s7::ComplexGain>(C64{-0.25, 1.5}); });
    flat.connect("ramp", 0, "split", 0);
    flat.connect("split", 0, "triple", 0);
    flat.connect("split", 0, "join", 0);
    flat.connect("triple", 0, "join", 1);
    flat.connect("join", 0, "rotate", 0);
    flat.connect("w", 0, "scale", 0);

    auto nested = root("nested_mixed");
    add_ramp(nested);
    nested.add_element("w", [] { return std::make_unique<s7::ComplexConstant>(C64{0.1, -0.7}); });
    nested.add_element("mixed", [] { return mixed_schema(); });
    nested.connect("ramp", 0, "mixed", 0);
    nested.connect("w", 0, "mixed", 1);

    s7::NodeRuntime flatRuntime(s7::Compiler::compile(flat));
    s7::NodeRuntime nestedRuntime(s7::Compiler::compile(nested));
    for (int step = 0; step < 20; ++step) {
        flatRuntime.step();
        nestedRuntime.step();
        require(nestedRuntime.real_output_of("mixed", 0) == flatRuntime.real_output_of("triple"), "F64 output a");
        require(nestedRuntime.complex_output_of("mixed", 1) == flatRuntime.complex_output_of("join"), "C64 output z");
        require(nestedRuntime.real_output_of("mixed", 2) == flatRuntime.real_output_of("split"), "F64 output b");
        require(nestedRuntime.complex_output_of("mixed", 3) == flatRuntime.complex_output_of("rotate"), "C64 output zr");
        require(nestedRuntime.complex_output_of("mixed", 4) == flatRuntime.complex_output_of("scale"), "C64 output ws");
    }
    require(nestedRuntime.complex_output_of("mixed", 3).imag() != 0.0, "Complex output lost its imaginary part");
}

// depth 0 is one Gain; depth d chains two schemas of depth d - 1. Leaf k of
// the chain has factor 1 + 0.01 * (first + k).
std::unique_ptr<s7::Schema> deep_schema(int depth, int first) {
    if (depth == 0) return one_gain(1.0 + 0.01 * first);
    auto schema = std::make_unique<s7::Schema>("level" + std::to_string(depth));
    schema->set_rate(s7::Rate::from_hz(1));
    const int half = 1 << (depth - 1);
    schema->add_element("first", [depth, first] { return deep_schema(depth - 1, first); });
    schema->add_element("second", [depth, first, half] { return deep_schema(depth - 1, first + half); });
    schema->connect("first", 0, "second", 0);
    schema->expose_input("in", s7::SignalType::F64, "first", 0);
    schema->expose_output("out", s7::SignalType::F64, "second", 0);
    return schema;
}

void deep_nesting() {
    constexpr int depth = 4;
    auto nested = root("deep");
    add_ramp(nested);
    nested.add_element("deep", [] { return deep_schema(depth, 0); });
    nested.connect("ramp", 0, "deep", 0);

    auto flat = root("flat_chain");
    add_ramp(flat);
    std::string previous = "ramp";
    for (int i = 0; i < (1 << depth); ++i) {
        const auto id = "gain" + std::to_string(i);
        flat.add_element(id, [i] { return std::make_unique<s7::Gain>(1.0 + 0.01 * i); });
        flat.connect(previous, 0, id, 0);
        previous = id;
    }

    s7::NodeRuntime flatRuntime(s7::Compiler::compile(flat));
    s7::NodeRuntime nestedRuntime(s7::Compiler::compile(nested));
    for (int step = 0; step < 30; ++step) {
        flatRuntime.step();
        nestedRuntime.step();
        require(nestedRuntime.real_output_of("deep") == flatRuntime.real_output_of(previous),
                "Deeply nested output differs from flat chain");
    }
}

void independent_stateful_instances_and_feedback() {
    // Two instances of one stateful schema with separate state, plus a
    // parent feedback loop that passes through a nested schema.
    auto flat = root("flat_feedback");
    add_ramp(flat);
    add_feedback_cell(flat, "a_", 0.5);
    add_feedback_cell(flat, "b_", 0.25);
    flat.connect("ramp", 0, "a_sum", 0);
    flat.connect("ramp", 0, "b_sum", 0);
    flat.add_element("loop_sum", [] { return std::make_unique<s7::Adder>(); });
    flat.add_element("loop_gain", [] { return std::make_unique<s7::Gain>(0.75); });
    flat.add_element("loop_delay", [] { return std::make_unique<s7::Delay>(0.5); });
    flat.connect("a_gain", 0, "loop_sum", 0);
    flat.connect("loop_delay", 0, "loop_sum", 1);
    flat.connect("loop_sum", 0, "loop_gain", 0);
    flat.connect("loop_gain", 0, "loop_delay", 0);

    auto nested = root("nested_feedback");
    add_ramp(nested);
    nested.add_element("a", [] { return feedback_schema(0.5); });
    nested.add_element("b", [] { return feedback_schema(0.25); });
    nested.connect("ramp", 0, "a", 0);
    nested.connect("ramp", 0, "b", 0);
    nested.add_element("loop_sum", [] { return std::make_unique<s7::Adder>(); });
    nested.add_element("loop_gain", [] { return one_gain(0.75); });
    nested.add_element("loop_delay", [] { return std::make_unique<s7::Delay>(0.5); });
    nested.connect("a", 0, "loop_sum", 0);
    nested.connect("loop_delay", 0, "loop_sum", 1);
    nested.connect("loop_sum", 0, "loop_gain", 0);
    nested.connect("loop_gain", 0, "loop_delay", 0);

    s7::NodeRuntime flatRuntime(s7::Compiler::compile(flat));
    s7::NodeRuntime nestedRuntime(s7::Compiler::compile(nested));
    for (int step = 0; step < 100; ++step) {
        flatRuntime.step();
        nestedRuntime.step();
        require(nestedRuntime.real_output_of("a") == flatRuntime.real_output_of("a_gain"), "Stateful instance a");
        require(nestedRuntime.real_output_of("b") == flatRuntime.real_output_of("b_gain"), "Stateful instance b");
        require(nestedRuntime.real_output_of("loop_gain") == flatRuntime.real_output_of("loop_gain"),
                "Feedback through nested schema");
        require(nestedRuntime.real_output_of("loop_delay") == flatRuntime.real_output_of("loop_delay"),
                "Parent causal memory after nested feedback");
    }
    require(nestedRuntime.real_output_of("a") != nestedRuntime.real_output_of("b"),
            "Independent nested instances share state");
}

void add_oscillator(s7::Schema& schema) {
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); });
    schema.add_element("v", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 0.0); });
    schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);
}

void nested_coupled_trapezoid() {
    s7::Schema flat("flat_oscillator");
    add_oscillator(flat);

    s7::Schema nested("nested_oscillator");
    nested.add_element("oscillator", [] {
        auto inner = std::make_unique<s7::Schema>("oscillator");
        inner->set_rate(s7::Rate::from_hz(100));
        add_oscillator(*inner);
        inner->expose_output("x", s7::SignalType::F64, "x", 0);
        inner->expose_output("v", s7::SignalType::F64, "v", 0);
        return inner;
    });

    s7::NodeRuntime flatRuntime(s7::Compiler::compile(flat));
    s7::NodeRuntime nestedRuntime(s7::Compiler::compile(nested));
    for (int step = 0; step < 500; ++step) {
        flatRuntime.step();
        nestedRuntime.step();
        require(nestedRuntime.real_output_of("oscillator", 0) == flatRuntime.real_output_of("x") &&
                    nestedRuntime.real_output_of("oscillator", 1) == flatRuntime.real_output_of("v"),
                "Nested coupled trapezoid differs from flat model");
    }
}

void recompilation_after_changes() {
    s7::Schema schema("editable");
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("g1", [] { return std::make_unique<s7::Gain>(2.0); });
    schema.expose_input("in", s7::SignalType::F64, "g1", 0);
    schema.expose_output("out", s7::SignalType::F64, "g1", 0);
    F64 input = 1.5;
    bind_one(schema, input);
    schema.step();
    require(f64_output(schema, 0) == 3.0, "Initial schema output");
    const auto firstStorage = schema.outputs()[0];

    // Adding elements, connections and outputs discards the compiled runtime;
    // the next step rebinds to the new one.
    schema.add_element("g2", [] { return std::make_unique<s7::Gain>(10.0); });
    schema.connect("g1", 0, "g2", 0);
    schema.expose_output("out2", s7::SignalType::F64, "g2", 0);
    schema.step();
    require(schema.outputs()[0] == firstStorage, "Exposed output storage moved on recompilation");
    require(f64_output(schema, 0) == 3.0 && f64_output(schema, 1) == 30.0, "Outputs after recompilation");
    input = -4.0;
    schema.step();
    require(f64_output(schema, 0) == -8.0 && f64_output(schema, 1) == -80.0, "Outputs after input change");

    schema.set_rate(s7::Rate::from_hz(2));
    schema.step();
    require(f64_output(schema, 0) == -8.0 && f64_output(schema, 1) == -80.0, "Outputs after rate change");

    // A schema used as a child is recompiled in every parent compilation.
    auto parent = root("parent");
    parent.add_element("source", [] { return std::make_unique<s7::Constant>(0.5); });
    parent.add_element("child", [] { return one_gain(4.0); });
    parent.connect("source", 0, "child", 0);
    for (int i = 0; i < 3; ++i) {
        s7::NodeRuntime runtime(s7::Compiler::compile(parent));
        runtime.step();
        require(runtime.real_output_of("child") == 2.0, "Recompiled parent output");
    }
}

std::vector<F64> feedback_reference(int steps, double factor, F64 input) {
    std::vector<F64> values;
    F64 previous = 0.0;
    for (int i = 0; i < steps; ++i) {
        previous = factor * (input + previous);
        values.push_back(previous);
    }
    return values;
}

void moves() {
    const F64 input = 1.0;
    const auto reference = feedback_reference(8, 0.5, input);
    auto first = std::move(*feedback_schema(0.5));
    bind_one(first, input);
    for (int i = 0; i < 3; ++i) {
        first.step();
        require(f64_output(first, 0) == reference[static_cast<std::size_t>(i)], "Before move");
    }

    // The compiled runtime and its bindings move with the schema; state continues.
    s7::Schema constructed(std::move(first));
    for (int i = 3; i < 5; ++i) {
        constructed.step();
        require(f64_output(constructed, 0) == reference[static_cast<std::size_t>(i)], "After move construction");
    }

    auto assigned = std::move(*feedback_schema(0.25));
    const F64 otherInput = 7.0;
    bind_one(assigned, otherInput);
    assigned.step();
    require(f64_output(assigned, 0) == 1.75, "Assignment target before move");
    assigned = std::move(constructed);
    for (int i = 5; i < 8; ++i) {
        assigned.step();
        require(f64_output(assigned, 0) == reference[static_cast<std::size_t>(i)], "After move assignment");
    }

    // A moved-and-recompiled schema reads its new runtime, not the old one.
    assigned.add_element("extra", [] { return std::make_unique<s7::Gain>(3.0); });
    assigned.connect("gain", 0, "extra", 0);
    assigned.expose_output("tripled", s7::SignalType::F64, "extra", 0);
    assigned.step();
    require(f64_output(assigned, 0) == 0.5 && f64_output(assigned, 1) == 1.5, "Recompiled after move");
}

void repeated_construction() {
    auto parent = root("repeated");
    add_ramp(parent);
    parent.add_element("a", [] { return feedback_schema(0.5); });
    parent.add_element("b", [] { return deep_schema(2, 0); });
    parent.connect("ramp", 0, "a", 0);
    parent.connect("a", 0, "b", 0);
    F64 expected{};
    for (int repetition = 0; repetition < 100; ++repetition) {
        s7::NodeRuntime runtime(s7::Compiler::compile(parent));
        for (int step = 0; step < 5; ++step) runtime.step();
        const auto value = runtime.real_output_of("b");
        if (repetition == 0) expected = value;
        require(value == expected, "Repeated construction changed nested result");
    }
}

void invalid_mappings() {
    const auto compile_with = [](std::function<void(s7::Schema&)> expose) {
        auto parent = root("invalid");
        parent.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        parent.add_element("child", [expose] {
            auto inner = std::make_unique<s7::Schema>("child");
            inner->set_rate(s7::Rate::from_hz(1));
            inner->add_element("g", [] { return std::make_unique<s7::Gain>(2.0); });
            inner->expose_input("in", s7::SignalType::F64, "g", 0);
            expose(*inner);
            return inner;
        });
        parent.connect("source", 0, "child", 0);
        s7::NodeRuntime runtime(s7::Compiler::compile(parent));
        runtime.step();
    };
    require_throws([&] { compile_with([](s7::Schema& s) { s.expose_output("out", s7::SignalType::F64, "missing", 0); }); },
                   "Missing exposed source element accepted");
    require_throws([&] { compile_with([](s7::Schema& s) { s.expose_output("out", s7::SignalType::F64, "g", 1); }); },
                   "Out-of-range exposed source port accepted");
    require_throws([&] { compile_with([](s7::Schema& s) { s.expose_output("out", s7::SignalType::C64, "g", 0); }); },
                   "Mismatched exposed output type accepted");

    // A failed standalone compilation keeps failing and leaves nothing to step.
    s7::Schema broken("broken");
    broken.set_rate(s7::Rate::from_hz(1));
    broken.add_element("g", [] { return std::make_unique<s7::Gain>(2.0); });
    broken.expose_input("in", s7::SignalType::F64, "g", 0);
    broken.expose_output("out", s7::SignalType::C64, "g", 0);
    const F64 input = 1.0;
    require_throws([&] { bind_one(broken, input); }, "Broken schema bound");
    require_throws([&] { broken.step(); }, "Broken schema stepped");
    require_throws([&] { broken.step(); }, "Broken schema stepped on retry");

    // Exposed ports are backed by F64/C64 storage; any other type is rejected
    // when exposed instead of misreading values or failing on a later step.
    s7::Schema integer("integer_ports");
    integer.set_rate(s7::Rate::from_hz(1));
    integer.add_element("count", [] { return std::make_unique<s7::GainT<s7::SignalType::I32>>(2); });
    for (const auto type : s7::kAllSignalTypes) {
        if (type == s7::SignalType::F64 || type == s7::SignalType::C64) continue;
        require_throws([&] { integer.expose_input("in", type, "count", 0); }, "Unsupported exposed input type accepted");
        require_throws([&] { integer.expose_output("out", type, "count", 0); },
                       "Unsupported exposed output type accepted");
    }
    require(integer.input_count() == 0 && integer.output_count() == 0, "Rejected exposure registered a port");

    // The same rule applies to schemas loaded from JSON.
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto definition = boost::json::parse(R"json({
        "name": "integer_library_schema", "dt": 1.0,
        "elements": [{"id": "gain", "type": "Gain", "params": {"factor": 2}}],
        "connections": [],
        "exposed_inputs": [{"name": "in", "type": "I32", "element": "gain", "port": 0}],
        "exposed_outputs": []
    })json");
    require_throws([&] { (void)s7::SchemaLibraryLoader::build_schema(definition, registry); },
                   "Schema JSON with an I32 exposed input was accepted");
}

class FaultingGain final : public s7::Element {
public:
    explicit FaultingGain(const bool& fail) : Element("FaultingGain"), fail_(fail) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        value_ = 3.0 * read_input<F64>(0);
        if (fail_) throw std::runtime_error("injected nested fault");
    }

private:
    const bool& fail_;
    F64 value_{};
};

std::unique_ptr<s7::Schema> faulting_schema(const bool& fail) {
    auto inner = std::make_unique<s7::Schema>("faulting");
    inner->set_rate(s7::Rate::from_hz(1));
    inner->add_element("ok", [] { return std::make_unique<s7::Gain>(2.0); });
    inner->add_element("faulty", [&fail] { return std::make_unique<FaultingGain>(fail); });
    inner->add_element("memory", [] { return std::make_unique<s7::Delay>(0.0); });
    inner->connect("ok", 0, "faulty", 0);
    inner->connect("faulty", 0, "memory", 0);
    inner->expose_input("in", s7::SignalType::F64, "ok", 0);
    inner->expose_output("ok", s7::SignalType::F64, "ok", 0);
    inner->expose_output("faulty", s7::SignalType::F64, "faulty", 0);
    inner->expose_output("memory", s7::SignalType::F64, "memory", 0);
    return inner;
}

void fault_rollback() {
    bool fail = false;

    // Standalone: a faulting nested step publishes none of its outputs.
    {
        auto schema = std::move(*faulting_schema(fail));
        F64 input = 1.0;
        bind_one(schema, input);
        schema.step();
        input = 2.0;
        schema.step();
        require(f64_output(schema, 0) == 4.0 && f64_output(schema, 1) == 12.0 && f64_output(schema, 2) == 6.0,
                "Standalone nested outputs before fault");
        input = 5.0;
        fail = true;
        require_throws([&] { schema.step(); }, "Nested fault did not propagate");
        require(f64_output(schema, 0) == 4.0 && f64_output(schema, 1) == 12.0 && f64_output(schema, 2) == 6.0,
                "Faulting nested step published partial outputs");
        fail = false;
        schema.step();
        require(f64_output(schema, 0) == 10.0 && f64_output(schema, 1) == 30.0 && f64_output(schema, 2) == 12.0,
                "Nested schema did not recover after fault");
    }

    // Inside a parent: the parent step is all-or-nothing, and the retried
    // step sees the same published inputs.
    auto parent = root("faulting_parent");
    add_ramp(parent);
    parent.add_element("child", [&fail] { return faulting_schema(fail); });
    parent.add_element("after", [] { return std::make_unique<s7::Gain>(10.0); });
    parent.connect("ramp", 0, "child", 0);
    parent.connect("child", 1, "after", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(parent));
    runtime.step();
    runtime.step();
    require(runtime.real_output_of("child", 1) == 12.0 && runtime.real_output_of("after") == 120.0,
            "Parent outputs before fault");
    fail = true;
    require_throws([&] { runtime.step(); }, "Parent did not propagate nested fault");
    require(runtime.step_count() == 2, "Faulting step advanced the parent");
    require(runtime.real_output_of("ramp") == 2.0 && runtime.real_output_of("child", 0) == 4.0 &&
                runtime.real_output_of("child", 1) == 12.0 && runtime.real_output_of("child", 2) == 6.0 &&
                runtime.real_output_of("after") == 120.0,
            "Parent published part of a faulting step");
    fail = false;
    runtime.step();
    require(runtime.real_output_of("ramp") == 3.0 && runtime.real_output_of("child", 1) == 18.0 &&
                runtime.real_output_of("child", 2) == 12.0 && runtime.real_output_of("after") == 180.0,
            "Parent did not recover after nested fault");
}
} // namespace

int main() try {
    single_output_and_timing();
    multiple_real_and_complex_outputs();
    deep_nesting();
    independent_stateful_instances_and_feedback();
    nested_coupled_trapezoid();
    recompilation_after_changes();
    moves();
    repeated_construction();
    invalid_mappings();
    fault_rollback();
    std::cout << "Nested schema output bindings, order, types, timing, recompilation, moves and faults passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
