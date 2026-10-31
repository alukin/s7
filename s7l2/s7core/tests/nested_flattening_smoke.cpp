// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// A nested schema behaves exactly like its inlined contents (ADR-024): it is
// flattened into its parent, so a feedback loop through causal memory inside
// it is valid, a loop through its direct paths is not, results are bitwise
// those of the inlined graph, and a faulting step is rolled back exactly as
// for the inlined graph. A nested schema with a rate of its own keeps it: its
// ports pin it and it seeds its elements.

#include <s7/builtin_element_registry.hpp>
#include <s7/core.hpp>
#include <s7/element_registry.hpp>
#include <s7/schema_library.hpp>

#include <boost/json.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string errors_of(const s7::Schema& schema) {
    std::string text;
    for (const auto& error : s7::Validator::validate(schema).errors) text += error + "\n";
    return text;
}

bool has_error(const s7::Schema& schema, const std::string& fragment) {
    return errors_of(schema).find(fragment) != std::string::npos;
}

// Direct element that throws while *fail is set.
class FaultingGain final : public s7::Element {
public:
    explicit FaultingGain(const bool* fail) : Element("FaultingGain"), fail_(fail) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        value_ = 2.0 * read_input<double>(0);
        if (*fail_) throw std::runtime_error("injected fault");
    }

private:
    const bool* fail_;
    double value_{};
};

// The plant u -> Gain(0.5) -> EulerIntegrator -> y, as a nested schema or
// inlined, in a proportional control loop: e = 1 - y, u = 3 e. The loop
// closes through the integrator only. `depth` nests the integrator further.
std::unique_ptr<s7::Schema> plant_schema(int depth) {
    auto plant = std::make_unique<s7::Schema>("plant");
    plant->add_element("gain", [] { return std::make_unique<s7::Gain>(0.5); });
    if (depth > 1) {
        plant->add_element("core", [] {
            auto core = std::make_unique<s7::Schema>("core");
            core->add_element("integrator", [] { return std::make_unique<s7::EulerIntegrator>(std::nullopt); });
            core->expose_input("x", s7::SignalType::F64, "integrator", 0);
            core->expose_output("y", s7::SignalType::F64, "integrator", 0);
            return core;
        });
        plant->connect("gain", 0, "core", 0);
        plant->expose_output("y", s7::SignalType::F64, "core", 0);
    } else {
        plant->add_element("integrator", [] { return std::make_unique<s7::EulerIntegrator>(std::nullopt); });
        plant->connect("gain", 0, "integrator", 0);
        plant->expose_output("y", s7::SignalType::F64, "integrator", 0);
    }
    plant->expose_input("u", s7::SignalType::F64, "gain", 0);
    return plant;
}

s7::Schema control_loop(int depth, const bool* fail = nullptr) {
    s7::Schema schema("loop");
    schema.set_rate(s7::Rate::from_hz(100));
    schema.add_element("setpoint", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("error", [] { return std::make_unique<s7::Adder>(std::vector<double>{1.0, -1.0}); });
    schema.add_element("controller", [] { return std::make_unique<s7::Gain>(3.0); });
    schema.connect("setpoint", 0, "error", 0);
    schema.connect("error", 0, "controller", 0);
    std::string y = "integrator";
    if (depth == 0) {
        schema.add_element("gain", [] { return std::make_unique<s7::Gain>(0.5); });
        schema.add_element("integrator", [] { return std::make_unique<s7::EulerIntegrator>(std::nullopt); });
        schema.connect("controller", 0, "gain", 0);
        schema.connect("gain", 0, "integrator", 0);
    } else {
        schema.add_element("plant", [depth] { return plant_schema(depth); });
        schema.connect("controller", 0, "plant", 0);
        y = "plant";
    }
    if (fail) {
        // A direct element after the plant, so a fault there comes after the
        // integrator's memory step would have run inside an opaque schema.
        schema.add_element("monitor", [fail] { return std::make_unique<FaultingGain>(fail); });
        schema.connect(y, 0, "monitor", 0);
    }
    schema.connect(y, 0, "error", 1);
    return schema;
}

std::vector<std::uint64_t> run(const s7::Schema& schema, const std::string& output, int steps) {
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_rate(s7::Rate::from_hz(100));
    std::vector<std::uint64_t> values;
    for (int n = 0; n < steps; ++n) {
        runtime.step();
        values.push_back(std::bit_cast<std::uint64_t>(runtime.real_output_of(output)));
    }
    return values;
}

void loop_through_internal_memory() {
    const auto inlined = run(control_loop(0), "integrator", 200);
    require(inlined.back() != inlined.front(), "The inlined loop moves");
    require(run(control_loop(1), "plant", 200) == inlined, "Nested plant: bitwise the inlined results");
    require(run(control_loop(2), "plant", 200) == inlined, "Doubly nested plant: bitwise the inlined results");
    require(run(control_loop(2), "plant/core/integrator", 200) == inlined, "Internal elements are addressable by path");
}

void direct_loops_rejected() {
    // Without the integrator, the loop through the plant has no memory.
    s7::Schema schema("direct");
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("plant", [] {
        auto plant = std::make_unique<s7::Schema>("plant");
        plant->add_element("gain", [] { return std::make_unique<s7::Gain>(0.5); });
        plant->expose_input("u", s7::SignalType::F64, "gain", 0);
        plant->expose_output("y", s7::SignalType::F64, "gain", 0);
        return plant;
    });
    schema.connect("one", 0, "sum", 0);
    schema.connect("sum", 0, "plant", 0);
    schema.connect("plant", 0, "sum", 1);
    require(has_error(schema, "Cycle detected without causal memory."), "A direct loop through a nested schema is rejected");
}

// Two outputs: "direct" = 2u, "delayed" = u one step late. A loop may close
// through the delayed output only, as it could inlined.
void feedthrough_per_output() {
    for (const bool through_delay : {true, false}) {
        s7::Schema schema("per_output");
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
        schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.add_element("split", [] {
            auto split = std::make_unique<s7::Schema>("split");
            split->add_element("double", [] { return std::make_unique<s7::Gain>(2.0); });
            split->add_element("delay", [] { return std::make_unique<s7::Delay>(); });
            split->expose_input("u", s7::SignalType::F64, "double", 0);
            split->expose_input("v", s7::SignalType::F64, "delay", 0);
            split->expose_output("direct", s7::SignalType::F64, "double", 0);
            split->expose_output("delayed", s7::SignalType::F64, "delay", 0);
            return split;
        });
        schema.connect("one", 0, "sum", 0);
        schema.connect("sum", 0, "split", 0);
        schema.connect("sum", 0, "split", 1);
        schema.connect("split", through_delay ? 1 : 0, "sum", 1);
        schema.add_element("sink", [] { return std::make_unique<s7::Gain>(1.0); });
        schema.connect("split", through_delay ? 0 : 1, "sink", 0);
        const auto errors = errors_of(schema);
        if (through_delay) {
            require(errors.empty(), "A loop through the delayed output is valid: " + errors);
            s7::NodeRuntime runtime(s7::Compiler::compile(schema));
            std::vector<double> sums;
            for (int n = 0; n < 4; ++n) {
                runtime.step();
                sums.push_back(runtime.real_output_of("sum"));
            }
            require(sums == std::vector<double>{1, 2, 3, 4}, "sum[n] = 1 + sum[n-1]");
            require(runtime.real_output_of("split", 0) == 8.0, "The direct output in the same step");
        } else {
            require(errors.find("Cycle detected without causal memory.") != std::string::npos,
                    "A loop through the direct output is rejected: " + errors);
        }
    }
}

void errors_name_the_nested_schema() {
    auto plant_only = [] {
        s7::Schema schema("errors");
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("plant", [] { return plant_schema(1); });
        return schema;
    };
    require(has_error(plant_only(), "Input port 0 of 'plant' is unconnected."), "Unconnected exposed input: " + errors_of(plant_only()));

    auto wrong_port = plant_only();
    wrong_port.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    wrong_port.connect("one", 0, "plant", 3);
    require(has_error(wrong_port, "Connection to 'plant' uses invalid input port 3."), "Invalid exposed port: " + errors_of(wrong_port));

    // Errors inside a nested schema are found by validating the parent.
    s7::Schema inner_error("inner_error");
    inner_error.set_rate(s7::Rate::from_hz(1));
    inner_error.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    inner_error.add_element("box", [] {
        auto box = std::make_unique<s7::Schema>("box");
        box->add_element("gain", [] { return std::make_unique<s7::Gain>(2.0); });
        box->connect("gain", 0, "missing", 0);
        box->expose_input("u", s7::SignalType::F64, "gain", 0);
        return box;
    });
    inner_error.connect("one", 0, "box", 0);
    require(has_error(inner_error, "Connection references unknown target element 'box/missing'."),
            "Internal error reported with its path: " + errors_of(inner_error));

    // Ids are unique per level before flattening: a nested schema and another
    // element named alike are reported even though the nested id disappears
    // into path ids, and Validator agrees with Compiler.
    s7::Schema twins("twins");
    twins.set_rate(s7::Rate::from_hz(1));
    twins.add_element("box", [] {
        auto box = std::make_unique<s7::Schema>("box");
        box->add_element("value", [] { return std::make_unique<s7::Constant>(1.0); });
        box->expose_output("y", s7::SignalType::F64, "value", 0);
        return box;
    });
    twins.add_element("box", [] { return std::make_unique<s7::Constant>(2.0); });
    require(has_error(twins, "Duplicate element id: 'box'."), "Duplicate id beside a nested schema: " + errors_of(twins));
    bool rejected = false;
    try {
        (void)s7::Compiler::compile(twins);
    } catch (const std::invalid_argument& error) {
        rejected = std::string(error.what()).find("Duplicate element id: 'box'.") != std::string::npos;
    }
    require(rejected, "Compile rejects the duplicate the same way");
    // Inside a nested schema, by path.
    s7::Schema inner_twins("inner_twins");
    inner_twins.set_rate(s7::Rate::from_hz(1));
    inner_twins.add_element("box", [] {
        auto box = std::make_unique<s7::Schema>("box");
        box->add_element("c", [] { return std::make_unique<s7::Constant>(1.0); });
        box->add_element("c", [] { return std::make_unique<s7::Constant>(2.0); });
        return box;
    });
    require(has_error(inner_twins, "Duplicate element id: 'box/c'."), "Duplicate id inside a nested schema: " + errors_of(inner_twins));

    // A path id that collides with a top-level id is a duplicate.
    auto collision = plant_only();
    collision.add_element("plant/gain", [] { return std::make_unique<s7::Constant>(1.0); });
    require(has_error(collision, "Duplicate element id: 'plant/gain'."), "Path collision: " + errors_of(collision));
}

// Studio documents give every nested schema the run's dt: equal to the
// parent's, it is flattened like a schema without a rate.
void document_nested_schemas() {
    auto& registry = s7::ElementRegistry::instance();
    const auto document = boost::json::parse(R"json({
        "name": "document", "dt": 0.01,
        "elements": [
            {"id": "setpoint", "type": "Constant", "params": {"value": 1.0}},
            {"id": "error", "type": "Adder", "params": {}},
            {"id": "negate", "type": "Gain", "params": {"factor": -1.0}},
            {"id": "plant", "type": "Schema", "schema": {
                "name": "plant", "dt": 0.01,
                "elements": [
                    {"id": "gain", "type": "Gain", "params": {"factor": 1.5}},
                    {"id": "integrator", "type": "EulerIntegrator", "params": {}}],
                "connections": [{"from_element": "gain", "from_port": 0, "to_element": "integrator", "to_port": 0}],
                "exposed_inputs": [{"name": "u", "type": "F64", "element": "gain", "port": 0}],
                "exposed_outputs": [{"name": "y", "type": "F64", "element": "integrator", "port": 0}]}}],
        "connections": [
            {"from_element": "setpoint", "from_port": 0, "to_element": "error", "to_port": 0},
            {"from_element": "error", "from_port": 0, "to_element": "plant", "to_port": 0},
            {"from_element": "plant", "from_port": 0, "to_element": "negate", "to_port": 0},
            {"from_element": "negate", "from_port": 0, "to_element": "error", "to_port": 1}]})json");
    const auto schema = s7::SchemaLibraryLoader::build_schema(document, registry);
    require(errors_of(schema).empty(), "A document loop through a nested integrator is valid: " + errors_of(schema));
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_rate(s7::Rate::from_hz(100));
    for (int n = 0; n < 3; ++n) runtime.step();
    // y[n+1] = y[n] + 0.01 * 1.5 * (1 - y[n]): 0, 0.015, 0.029775
    require(std::abs(runtime.real_output_of("plant") - 0.029775) < 1e-15, "Document loop result");
    const auto& uuid = runtime.element_instance_uuid_of("plant");
    require(runtime.real_output_of_instance_uuid(uuid) == runtime.real_output_of("plant/integrator"),
            "The nested schema's instance UUID addresses its output");
}

// A nested schema with a rate of its own is flattened too: its ports pin that
// rate, as the schema element declared it, and it seeds its elements. An
// unseeded parent around a 100 Hz plant is the same single-rate loop as the
// inlined one.
void own_rate_flattened() {
    const auto unseeded_loop = [] {
        s7::Schema schema("unseeded_loop");
        schema.add_element("setpoint", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.add_element("error", [] { return std::make_unique<s7::Adder>(std::vector<double>{1.0, -1.0}); });
        schema.add_element("controller", [] { return std::make_unique<s7::Gain>(3.0); });
        schema.add_element("plant", [] {
            auto plant = plant_schema(1);
            plant->set_rate(s7::Rate::from_hz(100));
            return plant;
        });
        schema.connect("setpoint", 0, "error", 0);
        schema.connect("error", 0, "controller", 0);
        schema.connect("controller", 0, "plant", 0);
        schema.connect("plant", 0, "error", 1);
        return schema;
    };
    require(errors_of(unseeded_loop()).empty(), "A loop through a 100 Hz plant in an unseeded parent: " + errors_of(unseeded_loop()));
    require(run(unseeded_loop(), "plant", 200) == run(control_loop(0), "integrator", 200),
            "Unseeded parent: bitwise the inlined 100 Hz results");

    // At another rate than its seeded parent, its ports pin their connected
    // set, and its elements, connected or not, take its rate.
    s7::Schema other("other_rate");
    other.set_rate(s7::Rate::from_hz(100));
    other.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    other.add_element("sink", [] { return std::make_unique<s7::Gain>(1.0); });
    other.add_element("slow", [] {
        auto slow = std::make_unique<s7::Schema>("slow");
        slow->set_rate(s7::Rate::from_hz(10));
        slow->add_element("gain", [] { return std::make_unique<s7::Gain>(2.0); });
        slow->add_element("idle", [] { return std::make_unique<s7::Constant>(0.0); });
        slow->expose_input("u", s7::SignalType::F64, "gain", 0);
        slow->expose_output("y", s7::SignalType::F64, "gain", 0);
        return slow;
    });
    other.connect("one", 0, "slow", 0);
    other.connect("slow", 0, "sink", 0);
    const auto graph = s7::Compiler::compile(other);
    const auto ten = s7::Rate::from_hz(10);
    require(graph.rates.at("one") == ten && graph.rates.at("sink") == ten && graph.rates.at("slow/gain") == ten &&
                graph.rates.at("slow/idle") == ten,
            "The nested rate pins its connected set and seeds its elements");

    // A conflict names the nested schema, not its port.
    s7::Schema conflict("conflict");
    conflict.add_element("x", [] { return std::make_unique<s7::EulerIntegrator>(0.01); });
    conflict.add_element("zero", [] { return std::make_unique<s7::Constant>(0.0); });
    conflict.add_element("slow", [] {
        auto slow = std::make_unique<s7::Schema>("slow");
        slow->set_rate(s7::Rate::from_hz(10));
        slow->add_element("gain", [] { return std::make_unique<s7::Gain>(2.0); });
        slow->expose_input("u", s7::SignalType::F64, "gain", 0);
        return slow;
    });
    conflict.connect("zero", 0, "x", 0);
    conflict.connect("x", 0, "slow", 0);
    const auto message = errors_of(conflict);
    require(message.find("nested schema 'slow'") != std::string::npos && message.find("10 Hz") != std::string::npos &&
                message.find("'x'") != std::string::npos,
            "A rate conflict names the nested schema: " + message);
}

// Rates follow the inlined form too: in a parent without a seed, an internal
// subgraph not connected to the schema's ports has no rate to borrow.
void rates_follow_the_inlined_form() {
    const auto build = [](bool seeded) {
        s7::Schema schema("unseeded");
        if (seeded) schema.set_rate(s7::Rate::from_hz(100));
        schema.add_element("x", [] { return std::make_unique<s7::EulerIntegrator>(0.01); });
        schema.add_element("zero", [] { return std::make_unique<s7::Constant>(0.0); });
        schema.add_element("box", [] {
            auto box = std::make_unique<s7::Schema>("box");
            box->add_element("gain", [] { return std::make_unique<s7::Gain>(2.0); });
            box->add_element("idle", [] { return std::make_unique<s7::Constant>(1.0); });
            box->add_element("idle_gain", [] { return std::make_unique<s7::Gain>(3.0); });
            box->connect("idle", 0, "idle_gain", 0);
            box->expose_input("u", s7::SignalType::F64, "gain", 0);
            return box;
        });
        schema.connect("zero", 0, "x", 0);
        schema.connect("x", 0, "box", 0);
        return schema;
    };
    const auto unseeded = errors_of(build(false));
    require(unseeded.find("box/idle") != std::string::npos && unseeded.find("cannot be determined") != std::string::npos,
            "An isolated internal subgraph needs a seed, as inlined: " + unseeded);
    require(errors_of(build(true)).empty(), "With a seed it resolves: " + errors_of(build(true)));
}

// ADR-021 covers the nested contents: a fault later in the step leaves the
// internal integrator unadvanced, and a retry continues exactly.
void fault_rolls_back_nested_state() {
    bool fail = false;
    const auto reference = run(control_loop(1, &fail), "plant", 20);
    s7::NodeRuntime runtime(s7::Compiler::compile(control_loop(1, &fail)));
    runtime.set_model_time_rate(s7::Rate::from_hz(100));
    std::vector<std::uint64_t> values;
    for (int n = 0; n < 20; ++n) {
        if (n == 7) {
            fail = true;
            bool threw = false;
            try {
                runtime.step();
            } catch (const std::runtime_error&) {
                threw = true;
            }
            fail = false;
            require(threw, "The injected fault stops the step");
            require(std::bit_cast<std::uint64_t>(runtime.real_output_of("plant")) == values.back(),
                    "The failed step published nothing inside the nested schema");
        }
        runtime.step();
        values.push_back(std::bit_cast<std::uint64_t>(runtime.real_output_of("plant")));
    }
    require(values == reference, "After the fault the run continues exactly");
}

// Rollback is exactly as inlined, no more: ADR-021 restores published
// outputs and staged causal memory, not private state such as a filter's
// history. A nested FIR before a fault behaves bitwise like the inlined one,
// both continuing from their advanced history.
void fault_with_nested_filter() {
    const auto build = [](bool nested, const bool* fail) {
        s7::Schema schema("filtered");
        schema.set_rate(s7::Rate::from_hz(100));
        schema.add_element("ramp_one", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.add_element("ramp", [] { return std::make_unique<s7::EulerIntegrator>(std::nullopt); });
        schema.connect("ramp_one", 0, "ramp", 0);
        std::string out = "fir";
        if (nested) {
            schema.add_element("box", [] {
                auto box = std::make_unique<s7::Schema>("box");
                box->add_element("fir", [] { return std::make_unique<s7::FirFilter>(std::vector<double>{0.5, 0.25, 0.25}); });
                box->expose_input("u", s7::SignalType::F64, "fir", 0);
                box->expose_output("y", s7::SignalType::F64, "fir", 0);
                return box;
            });
            out = "box";
        } else {
            schema.add_element("fir", [] { return std::make_unique<s7::FirFilter>(std::vector<double>{0.5, 0.25, 0.25}); });
        }
        schema.connect("ramp", 0, out, 0);
        schema.add_element("monitor", [fail] { return std::make_unique<FaultingGain>(fail); });
        schema.connect(out, 0, "monitor", 0);
        return std::pair{std::move(schema), out};
    };
    const auto faulted_run = [&](bool nested) {
        bool fail = false;
        auto [schema, out] = build(nested, &fail);
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        runtime.set_model_time_rate(s7::Rate::from_hz(100));
        std::vector<std::uint64_t> values;
        for (int n = 0; n < 12; ++n) {
            if (n == 5) {
                fail = true;
                try {
                    runtime.step();
                } catch (const std::runtime_error&) {
                }
                fail = false;
            }
            runtime.step();
            values.push_back(std::bit_cast<std::uint64_t>(runtime.real_output_of(out)));
        }
        return values;
    };
    const auto nested = faulted_run(true);
    require(nested == faulted_run(false), "A fault after a nested FIR: bitwise the inlined results");
    bool fail = false;
    auto [reference_schema, reference_out] = build(true, &fail);
    require(nested != run(reference_schema, reference_out, 12),
            "The FIR's private history is not rolled back (ADR-021), nested or not");
}

// An implicit trapezoid's derivative may pass through a nested schema's
// stateless elements, as it could inlined: x' = -2x.
void trapezoid_derivative_through_nested() {
    const auto build = [](bool nested) {
        s7::Schema schema("decay");
        schema.set_rate(s7::Rate::from_hz(100));
        schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(std::nullopt, 1.0); });
        if (nested) {
            schema.add_element("rhs", [] {
                auto rhs = std::make_unique<s7::Schema>("rhs");
                rhs->add_element("k", [] { return std::make_unique<s7::Gain>(-2.0); });
                rhs->expose_input("x", s7::SignalType::F64, "k", 0);
                rhs->expose_output("dx", s7::SignalType::F64, "k", 0);
                return rhs;
            });
        } else {
            schema.add_element("rhs", [] { return std::make_unique<s7::Gain>(-2.0); });
        }
        schema.connect("x", 0, "rhs", 0);
        schema.connect("rhs", 0, "x", 0);
        return schema;
    };
    require(errors_of(build(true)).empty(), "Derivative through a nested stateless schema: " + errors_of(build(true)));
    require(run(build(true), "x", 100) == run(build(false), "x", 100), "Nested derivative: bitwise the inlined results");
}

// A Resampler inside a nested schema joins rate domains of the parent graph.
void resampler_inside() {
    const auto build = [](bool nested) {
        s7::Schema schema("rates");
        schema.set_rate(s7::Rate::from_hz(1000));
        schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
        const std::string prefix = nested ? "fast/" : "";
        if (nested) {
            schema.add_element("fast", [] {
                auto fast = std::make_unique<s7::Schema>("fast");
                fast->add_element("up", [] { return std::make_unique<s7::Resampler>(); });
                fast->add_element("integrator", [] { return std::make_unique<s7::EulerIntegrator>(0.0005); });
                fast->connect("up", 0, "integrator", 0);
                fast->expose_input("u", s7::SignalType::F64, "up", 0);
                return fast;
            });
            schema.connect("one", 0, "fast", 0);
        } else {
            schema.add_element("up", [] { return std::make_unique<s7::Resampler>(); });
            schema.add_element("integrator", [] { return std::make_unique<s7::EulerIntegrator>(0.0005); });
            schema.connect("up", 0, "integrator", 0);
            schema.connect("one", 0, "up", 0);
        }
        return schema;
    };
    const auto graph = s7::Compiler::compile(build(true));
    require(graph.domains.size() == 2 && graph.domain_links.size() == 1, "Two rate domains joined by the nested Resampler");
    const auto read = [](const s7::Schema& schema, const std::string& output) {
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        runtime.set_model_time_rate(s7::Rate::from_hz(1000));
        std::vector<std::uint64_t> values;
        for (int n = 0; n < 10; ++n) {
            runtime.step();
            values.push_back(std::bit_cast<std::uint64_t>(runtime.real_output_of(output)));
        }
        return values;
    };
    const auto nested = read(build(true), "fast/integrator");
    require(nested == read(build(false), "integrator"), "Nested Resampler: bitwise the inlined results");
    // After 10 steps at 1 kHz the 2 kHz domain took 19; the integrator's
    // output lags its input by one of them: 18 * 0.5 ms.
    require(std::abs(std::bit_cast<double>(nested.back()) - 0.009) < 1e-12, "The fast domain steps twice per step");
}
} // namespace

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    loop_through_internal_memory();
    direct_loops_rejected();
    feedthrough_per_output();
    errors_name_the_nested_schema();
    document_nested_schemas();
    own_rate_flattened();
    rates_follow_the_inlined_form();
    fault_rolls_back_nested_state();
    fault_with_nested_filter();
    trapezoid_derivative_through_nested();
    resampler_inside();
    std::cout << "Nested schemas flattened: loops through internal memory, per-output feedthrough, errors, rollback, rates\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
