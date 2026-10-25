// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json.hpp>

#include "s7/core.hpp"

namespace {

s7::Schema make_valid_schema()
{
    s7::Schema schema{"valid_feedback"};
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("memory", [] { return std::make_unique<s7::Delay>(0.0); });

    schema.connect("unit", 0, "sum", 0);
    schema.connect("memory", 0, "sum", 1);
    schema.connect("sum", 0, "memory", 0);
    return schema;
}

s7::Schema make_invalid_schema()
{
    s7::Schema schema{"invalid_cycle"};
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("a", [] { return std::make_unique<s7::Gain>(2.0); });
    schema.add_element("b", [] { return std::make_unique<s7::Gain>(0.5); });

    schema.connect("a", 0, "b", 0);
    schema.connect("b", 0, "a", 0);
    return schema;
}

s7::Schema make_complex_schema()
{
    s7::Schema schema{"complex_path"};
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("c0", [] { return std::make_unique<s7::ComplexConstant>(std::complex<double>{1.0, 2.0}); });
    schema.add_element("gain", [] { return std::make_unique<s7::ComplexGain>(std::complex<double>{0.5, 0.0}); });

    schema.connect("c0", 0, "gain", 0);
    return schema;
}

s7::Schema make_implicit_conversion_schema()
{
    s7::Schema schema{"real_to_complex_with_warning"};
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("cgain", [] { return std::make_unique<s7::ComplexGain>(std::complex<double>{2.0, 0.0}); });

    schema.connect("unit", 0, "cgain", 0);
    return schema;
}

boost::json::value make_schema_library_json()
{
    return boost::json::parse(R"json(
    {
      "document_kind": "s7.element_library",
      "name": "test_library",
      "elements": [
        {
          "type_name": "DoubleGain",
          "display_name": "Double Gain",
          "description": "Schema-backed gain element.",
          "category": "tests",
          "version": "1.0.0",
          "schema": {
            "name": "double_gain",
            "dt": 1.0,
            "elements": [
              {
                "id": "gain",
                "type": "Gain",
                "params": {
                  "factor": 2.0
                }
              }
            ],
            "connections": [],
            "exposed_inputs": [
              {
                "name": "in",
                "type": "F64",
                "element": "gain",
                "port": 0
              }
            ],
            "exposed_outputs": [
              {
                "name": "out",
                "type": "F64",
                "element": "gain",
                "port": 0
              }
            ]
          }
        }
      ]
    }
    )json");
}

void test_cycles_inside_memory_scc()
{
    // A direct-feedthrough cycle must be rejected even when a second cycle
    // through causal memory puts every element in the same SCC.
    for (const bool self_loop : {false, true}) {
        for (const bool reversed : {false, true}) {
            s7::Schema schema{"mixed_cycles"};
            schema.set_rate(s7::Rate::from_hz(1));
            std::vector<std::pair<std::string, s7::ElementFactory>> elements{
                {"a", [] { return std::make_unique<s7::Adder>(); }},
                {"b", [] { return std::make_unique<s7::Gain>(); }},
                {"memory", [] { return std::make_unique<s7::Delay>(); }},
            };
            if (reversed) std::ranges::reverse(elements);
            for (auto& [id, factory] : elements) schema.add_element(id, std::move(factory));
            schema.connect("a", 0, "b", 0);
            schema.connect(self_loop ? "a" : "b", 0, "a", 0);
            schema.connect("b", 0, "memory", 0);
            schema.connect("memory", 0, "a", 1);

            const auto validation = s7::Validator::validate(schema);
            assert(!validation.ok());
            assert(std::ranges::any_of(validation.errors, [](const std::string& error) {
                return error.find("Cycle detected without causal memory") != std::string::npos;
            }));
            bool rejected_by_validation = false;
            try {
                static_cast<void>(s7::Compiler::compile(schema));
            } catch (const std::invalid_argument& error) {
                rejected_by_validation = std::string_view{error.what()}.find("Cycle detected without causal memory") != std::string_view::npos;
            }
            assert(rejected_by_validation);
        }
    }

    // Removing a memory self-loop leaves an empty, valid feedthrough graph.
    s7::Schema memory_loop{"memory_self_loop"};
    memory_loop.set_rate(s7::Rate::from_hz(1));
    memory_loop.add_element("memory", [] { return std::make_unique<s7::Delay>(3.0); });
    memory_loop.connect("memory", 0, "memory", 0);
    assert(s7::Validator::validate(memory_loop).ok());
    s7::NodeRuntime runtime{s7::Compiler::compile(memory_loop)};
    for (int step = 0; step < 3; ++step) {
        runtime.step();
        assert(runtime.real_output_of("memory") == 3.0);
    }
}

void test_variadic_input_bounds()
{
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    for (const auto* type : {"Adder", "Multiply"}) {
        const bool adder = std::string_view{type} == "Adder";
        const auto* descriptor = registry.find(type);
        assert(descriptor);
        for (const std::size_t count : {0, 1, 2, 64, 65}) {
            const bool valid = count >= 2 && count <= 64;
            bool constructed = false;
            try {
                const std::vector<double> factors(count, 1.0);
                const auto element = adder ? std::unique_ptr<s7::Element>{std::make_unique<s7::Adder>(factors)}
                                           : std::unique_ptr<s7::Element>{std::make_unique<s7::Multiply>(factors)};
                assert(element->input_count() == count);
                constructed = true;
            } catch (const std::invalid_argument& error) {
                assert(std::string_view{error.what()}.find("between 2 and 64") != std::string_view::npos);
            }
            assert(constructed == valid);

            boost::json::array factors;
            for (std::size_t index = 0; index < count; ++index) factors.push_back(1.0);
            s7::Schema schema{"variadic_bounds"};
            schema.set_rate(s7::Rate::from_hz(1));
            schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element(s7::make_element_spec("operation", *descriptor,
                {{"type", "F64"}, {adder ? "weights" : "factors", std::move(factors)}}));
            for (std::size_t index = 0; index < count; ++index) schema.connect("one", 0, "operation", index);
            const auto validation = s7::Validator::validate(schema);
            assert(validation.ok() == valid);
            if (valid) {
                s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
                runtime.step();
                assert(runtime.real_output_of("operation") == (adder ? static_cast<double>(count) : 1.0));
            } else {
                assert(std::ranges::any_of(validation.errors, [type, count](const std::string& error) {
                    return error.find("Element 'operation' (" + std::string{type} + ")") != std::string::npos &&
                           error.find(count < 2 ? "at least 2" : "between 2 and 64") != std::string::npos;
                }));
                bool rejected = false;
                try {
                    static_cast<void>(s7::Compiler::compile(schema));
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                assert(rejected);
            }
        }
    }
}

} // namespace

int main() try
{
    test_cycles_inside_memory_scc();
    test_variadic_input_bounds();
    {
        auto constant = std::make_unique<s7::Constant>(1.0);
        assert(constant->output_ports().size() == 1);
        assert(constant->output_ports()[0].type == s7::SignalType::F64);
        assert(constant->output_ports()[0].name == "out");

        auto gain = std::make_unique<s7::Gain>(2.0);
        assert(gain->input_ports().size() == 1);
        assert(gain->input_ports()[0].type == s7::SignalType::F64);
        assert(gain->input_ports()[0].name == "in");

        auto adder = std::make_unique<s7::Adder>();
        assert(adder->input_ports().size() == 2);
        assert(adder->input_ports()[0].name == "lhs");
        assert(adder->input_ports()[1].name == "rhs");

        const s7::Adder adder3{{1.0, 1.0, 1.0}};
        assert(adder3.input_ports().size() == 3);
        assert(adder3.input_ports()[0].name == "lhs");
        assert(adder3.input_ports()[1].name == "rhs");
        assert(adder3.input_ports()[2].name == "in2");

        const s7::Multiply multiply4{{1.0, 1.0, 1.0, 1.0}};
        assert(multiply4.input_ports().size() == 4);
        assert(multiply4.input_ports()[0].name == "lhs");
        assert(multiply4.input_ports()[1].name == "rhs");
        assert(multiply4.input_ports()[2].name == "in2");
        assert(multiply4.input_ports()[3].name == "in3");
        assert(adder->input_ports()[0].type == s7::SignalType::F64);
        assert(adder->input_ports()[1].type == s7::SignalType::F64);

        auto weightedAdder = std::make_unique<s7::Adder>(std::vector<double>{1.0, 1.0, 2.4});
        assert(weightedAdder->input_ports().size() == 3);
        assert(weightedAdder->input_ports()[2].type == s7::SignalType::F64);

        auto multiply = std::make_unique<s7::Multiply>();
        assert(multiply->input_ports().size() == 2);
        assert(multiply->input_ports()[0].type == s7::SignalType::F64);
        assert(multiply->input_ports()[1].type == s7::SignalType::F64);

        auto variadicMultiply = std::make_unique<s7::Multiply>(std::vector<double>{1.0, 0.5, 2.0});
        assert(variadicMultiply->input_ports().size() == 3);
        assert(variadicMultiply->input_ports()[2].type == s7::SignalType::F64);

        auto analog = std::make_unique<s7::AnalogOscillator>(1.0, 1.0, 1000.0);
        assert(analog->has_state());
        assert(!analog->has_causal_memory());
        assert(analog->output_ports().size() == 1);
        assert(analog->output_ports()[0].type == s7::SignalType::F64);
    }

    {
        const s7::ConvertOptions normalized{s7::ConvertMode::Normalized};
        auto int12ToDouble = std::make_unique<s7::ConvertT<s7::SignalType::I12, s7::SignalType::F64>>(normalized);
        assert(int12ToDouble->input_count() == 1);
        assert(int12ToDouble->input_ports()[0].type == s7::SignalType::I12);
        assert(int12ToDouble->output_ports()[0].type == s7::SignalType::F64);

        auto doubleToInt12 = std::make_unique<s7::ConvertT<s7::SignalType::F64, s7::SignalType::I12>>(normalized);
        assert(doubleToInt12->input_ports()[0].type == s7::SignalType::F64);
        assert(doubleToInt12->output_ports()[0].type == s7::SignalType::I12);

        // Normalized INT12 conversion: Q11 scaling, round half to even, saturation.
        const std::vector<std::pair<double, std::int16_t>> int12Cases{
            {0.0, 0}, {0.5, 1024}, {-1.0, -2048}, {1.0, 2047}, {100.0, 2047}, {-100.0, -2048},
            {0.5 / 2048.0, 0}, {1.5 / 2048.0, 2}, {-2.5 / 2048.0, -2}, {std::nan(""), 0},
        };
        for (const auto& [input, expected] : int12Cases) {
            const std::array<s7::InputBinding, 1> binding{s7::bind_input<s7::SignalType::F64>(input)};
            doubleToInt12->bind_inputs({binding.data(), binding.size()});
            doubleToInt12->step();
            const auto code = s7::bound_value<s7::SignalType::I12>(doubleToInt12->outputs()[0]);
            assert(code == expected);

            const std::array<s7::InputBinding, 1> back{s7::bind_input<s7::SignalType::I12>(code)};
            int12ToDouble->bind_inputs({back.data(), back.size()});
            int12ToDouble->step();
            assert(s7::bound_value<s7::SignalType::F64>(int12ToDouble->outputs()[0]) == code / 2048.0);
        }

        // Standalone step() writes the next-step output; NodeRuntime defers its publication.
        auto previousValue = std::make_unique<s7::PreviousValue>(3.5);
        assert(previousValue->has_causal_memory());
        assert(s7::bound_value<s7::SignalType::F64>(previousValue->outputs()[0]) == 3.5);
        const s7::RealValue pvIn1{8.0};
        const s7::RealValue pvIn2{9.0};
        const std::array<s7::InputBinding, 1> pvInputs1{s7::bind_input<s7::SignalType::F64>(pvIn1)};
        const std::array<s7::InputBinding, 1> pvInputs2{s7::bind_input<s7::SignalType::F64>(pvIn2)};
        previousValue->bind_inputs({pvInputs1.data(), pvInputs1.size()});
        previousValue->step();
        assert(s7::bound_value<s7::SignalType::F64>(previousValue->outputs()[0]) == 8.0);
        previousValue->bind_inputs({pvInputs2.data(), pvInputs2.size()});
        previousValue->step();
        assert(s7::bound_value<s7::SignalType::F64>(previousValue->outputs()[0]) == 9.0);

        auto integrator = std::make_unique<s7::Accumulator>(1.5);
        assert(integrator->has_state());
        assert(!integrator->has_causal_memory());
        const s7::RealValue intIn1{2.0};
        const s7::RealValue intIn2{-0.5};
        const std::array<s7::InputBinding, 1> intInputs1{s7::bind_input<s7::SignalType::F64>(intIn1)};
        const std::array<s7::InputBinding, 1> intInputs2{s7::bind_input<s7::SignalType::F64>(intIn2)};
        integrator->bind_inputs({intInputs1.data(), intInputs1.size()});
        integrator->step();
        assert(s7::bound_value<s7::SignalType::F64>(integrator->outputs()[0]) == 3.5);
        integrator->bind_inputs({intInputs2.data(), intInputs2.size()});
        integrator->step();
        assert(s7::bound_value<s7::SignalType::F64>(integrator->outputs()[0]) == 3.0);

        auto complexToMagPhase = std::make_unique<s7::ComplexToMagnitudePhase>();
        assert(complexToMagPhase->input_ports()[0].type == s7::SignalType::C64);
        assert(complexToMagPhase->output_count() == 2);
        assert(complexToMagPhase->output_ports()[0].name == "magnitude");
        assert(complexToMagPhase->output_ports()[1].name == "phase");
        const s7::ComplexValue mpIn{3.0, 4.0};
        const std::array<s7::InputBinding, 1> mpInputs{s7::bind_input<s7::SignalType::C64>(mpIn)};
        complexToMagPhase->bind_inputs({mpInputs.data(), mpInputs.size()});
        complexToMagPhase->step();
        const auto magPhase = complexToMagPhase->outputs();
        assert(s7::bound_value<s7::SignalType::F64>(magPhase[0]) == 5.0);
        assert(std::abs(s7::bound_value<s7::SignalType::F64>(magPhase[1]) - std::atan2(4.0, 3.0)) < 1e-12);

        auto fir = std::make_unique<s7::FirFilter>(std::vector<double>{0.5, 0.5});
        const s7::RealValue firIn1{2.0};
        const s7::RealValue firIn2{4.0};
        const std::array<s7::InputBinding, 1> firInputs1{s7::bind_input<s7::SignalType::F64>(firIn1)};
        const std::array<s7::InputBinding, 1> firInputs2{s7::bind_input<s7::SignalType::F64>(firIn2)};
        fir->bind_inputs({firInputs1.data(), firInputs1.size()});
        fir->step();
        assert(s7::bound_value<s7::SignalType::F64>(fir->outputs()[0]) == 1.0);
        fir->bind_inputs({firInputs2.data(), firInputs2.size()});
        fir->step();
        assert(s7::bound_value<s7::SignalType::F64>(fir->outputs()[0]) == 3.0);

        auto iir = std::make_unique<s7::IirFilter>(1.0, 0.0, 0.5);
        const s7::RealValue iirIn{1.0};
        const std::array<s7::InputBinding, 1> iirInputs{s7::bind_input<s7::SignalType::F64>(iirIn)};
        iir->bind_inputs({iirInputs.data(), iirInputs.size()});
        iir->step();
        assert(std::abs(s7::bound_value<s7::SignalType::F64>(iir->outputs()[0]) - 1.0) < 1e-12);
        iir->step();
        assert(std::abs(s7::bound_value<s7::SignalType::F64>(iir->outputs()[0]) - 0.5) < 1e-12);
    }

    {
        auto makeComposite = [] {
            auto schema = std::make_unique<s7::Schema>("composite_gain");
            schema->set_rate(s7::Rate::from_hz(1));
            schema->add_element("inner_gain", [] { return std::make_unique<s7::Gain>(2.5); });
            schema->expose_input("in", s7::SignalType::F64, "inner_gain", 0);
            schema->expose_output("out", s7::SignalType::F64, "inner_gain", 0);
            return schema;
        };
        {
            auto probeComposite = makeComposite();
            assert(probeComposite->type_name() == "Schema");
            assert(probeComposite->input_count() == 1);
            assert(probeComposite->output_count() == 1);
        }

        s7::Schema outer{"outer_with_schema_element"};
        outer.set_rate(s7::Rate::from_hz(1));
        outer.add_element("source", [] { return std::make_unique<s7::Constant>(4.0); });
        outer.add_element("composite", [makeComposite] { return makeComposite(); });
        outer.connect("source", 0, "composite", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(outer)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("composite") - 10.0) < 1e-12);
    }

    {
        s7::Schema root{"nested_schema"};
        root.set_rate(s7::Rate::from_hz(1));
        root.add_element("source", [] { return std::make_unique<s7::Constant>(3.0); });
        root.add_element("nested", [] {
            auto inner = std::make_unique<s7::Schema>("inner");
            inner->set_rate(s7::Rate::from_hz(1));
            inner->add_element("g1", [] { return std::make_unique<s7::Gain>(2.0); });
            inner->add_element("g2", [] { return std::make_unique<s7::Gain>(5.0); });
            inner->connect("g1", 0, "g2", 0);
            inner->expose_input("in", s7::SignalType::F64, "g1", 0);
            inner->expose_output("out", s7::SignalType::F64, "g2", 0);
            return inner;
        });
        root.connect("source", 0, "nested", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(root)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("nested") - 30.0) < 1e-12);
    }

    {
        s7::Schema outer{"invalid_nested_schema"};
        outer.set_rate(s7::Rate::from_hz(1));
        outer.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        outer.add_element("broken_nested", [] {
            auto inner = std::make_unique<s7::Schema>("broken");
            inner->set_rate(s7::Rate::from_hz(1));
            inner->add_element("g", [] { return std::make_unique<s7::Gain>(2.0); });
            inner->expose_input("in", s7::SignalType::F64, "g", 0);
            inner->expose_output("out", s7::SignalType::F64, "missing", 0);
            return inner;
        });
        outer.connect("source", 0, "broken_nested", 0);

        bool threw = false;
        try {
            auto runtime = s7::NodeRuntime{s7::Compiler::compile(outer)};
            (void)runtime;
        } catch (const std::exception&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected nested schema validation failure was not reported.");
        }
    }

    {
        const auto schema = make_valid_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(validation.ok());

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(runtime.real_output_of("sum") == 1.0);
        const auto sumInstanceUuid = runtime.element_instance_uuid_of("sum");
        assert(!sumInstanceUuid.empty());
        assert(runtime.element_id_of_instance_uuid(sumInstanceUuid) == "sum");
        assert(runtime.real_output_of_instance_uuid(sumInstanceUuid) == 1.0);
        runtime.step();
        assert(runtime.real_output_of("sum") == 2.0);
    }

    {
        s7::Schema schema{"explicit_uuid_lookup"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(4.0); });
        schema.add_element("gain", [] { return std::make_unique<s7::Gain>(3.0); });
        schema.connect("source", 0, "gain", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema,
                                                      "00000000-0000-4000-8000-000000000010",
                                                      "00000000-0000-4000-8000-000000000020")};
        runtime.step();
        assert(runtime.runtime_instance_uuid() == "00000000-0000-4000-8000-000000000010");
        assert(runtime.schema_instance_uuid() == "00000000-0000-4000-8000-000000000020");
        const auto sourceUuid = runtime.element_instance_uuid_of("source");
        const auto gainUuid = runtime.element_instance_uuid_of("gain");
        assert(sourceUuid != gainUuid);
        assert(runtime.element_id_of_instance_uuid(gainUuid) == "gain");
        assert(std::abs(runtime.real_output_of_instance_uuid(gainUuid) - 12.0) < 1e-12);
    }

    {
        s7::Schema schema{"weighted_adder"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("a", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.add_element("b", [] { return std::make_unique<s7::Constant>(2.0); });
        schema.add_element("c", [] { return std::make_unique<s7::Constant>(3.0); });
        schema.add_element("sum", [] { return std::make_unique<s7::Adder>(std::vector<double>{1.0, 1.0, 2.4}); });
        schema.connect("a", 0, "sum", 0);
        schema.connect("b", 0, "sum", 1);
        schema.connect("c", 0, "sum", 2);

        const auto validation = s7::Validator::validate(schema);
        assert(validation.ok());

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("sum") - 10.2) < 1e-12);
    }

    {
        s7::Schema schema{"variadic_multiply"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("a", [] { return std::make_unique<s7::Constant>(2.0); });
        schema.add_element("b", [] { return std::make_unique<s7::Constant>(3.0); });
        schema.add_element("c", [] { return std::make_unique<s7::Constant>(4.0); });
        schema.add_element("mul", [] { return std::make_unique<s7::Multiply>(std::vector<double>{1.0, 0.5, 2.0}); });
        schema.connect("a", 0, "mul", 0);
        schema.connect("b", 0, "mul", 1);
        schema.connect("c", 0, "mul", 2);

        const auto validation = s7::Validator::validate(schema);
        assert(validation.ok());

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("mul") - 24.0) < 1e-12);
    }

    {
        const auto schema = make_complex_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(validation.ok());

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        const auto out = runtime.complex_output_of("gain");
        if (out.real() != 0.5 || out.imag() != 1.0) {
            throw std::runtime_error("Unexpected ComplexGain output values.");
        }
    }

    {
        const auto schema = make_implicit_conversion_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(!validation.ok());

        const auto hasMismatchError = std::ranges::any_of(validation.errors, [](const std::string& error) {
            return error.find("Type conflict") != std::string::npos &&
                   error.find("Insert a Convert element") != std::string::npos;
        });
        if (!hasMismatchError) {
            throw std::runtime_error("Expected validation mismatch error was not reported.");
        }
    }

    {
        const auto schema = make_invalid_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(!validation.ok());

        const auto hasCycleError = std::ranges::any_of(validation.errors, [](const std::string& error) {
            return error.find("Cycle detected without causal memory") != std::string::npos;
        });
        if (!hasCycleError) {
            throw std::runtime_error("Expected cycle validation error was not reported.");
        }
    }

    {
        // Stateful elements with direct feedthrough add no delay.
        s7::Schema schema{"fir_feedthrough"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.add_element("fir", [] { return std::make_unique<s7::FirFilter>(std::vector<double>{1.0}); });
        schema.add_element("gain", [] { return std::make_unique<s7::Gain>(1.0); });
        schema.connect("source", 0, "fir", 0);
        schema.connect("fir", 0, "gain", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(runtime.real_output_of("fir") == 1.0);
        assert(runtime.real_output_of("gain") == 1.0);
    }

    {
        // Stateful-to-stateful chains run in topological order within one step.
        s7::Schema schema{"stateful_chain"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(2.0); });
        schema.add_element("integrator", [] { return std::make_unique<s7::Accumulator>(0.0); });
        schema.add_element("iir", [] { return std::make_unique<s7::IirFilter>(1.0, 0.0, 0.0); });
        schema.connect("source", 0, "integrator", 0);
        schema.connect("integrator", 0, "iir", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(runtime.real_output_of("iir") == 2.0);
        runtime.step();
        assert(runtime.real_output_of("iir") == 4.0);
    }

    {
        // Delay and PreviousValue delay by exactly one step, including in a chain,
        // independent of declaration order.
        for (const bool reversed : {false, true}) {
            s7::Schema schema{"unit_delays"};
            schema.set_rate(s7::Rate::from_hz(1));
            std::vector<std::pair<std::string, s7::ElementFactory>> elements{
                {"source", [] { return std::make_unique<s7::Accumulator>(0.0); }},
                {"unit", [] { return std::make_unique<s7::Constant>(1.0); }},
                {"delay", [] { return std::make_unique<s7::Delay>(-1.0); }},
                {"previous", [] { return std::make_unique<s7::PreviousValue>(-2.0); }},
                {"after_delay", [] { return std::make_unique<s7::Gain>(1.0); }},
            };
            if (reversed) {
                std::ranges::reverse(elements);
            }
            for (auto& [id, factory] : elements) {
                schema.add_element(id, std::move(factory));
            }
            schema.connect("unit", 0, "source", 0);
            schema.connect("source", 0, "delay", 0);
            schema.connect("delay", 0, "previous", 0);
            schema.connect("delay", 0, "after_delay", 0);

            s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
            const std::array<double, 4> expectedSource{1.0, 2.0, 3.0, 4.0};
            const std::array<double, 4> expectedDelay{-1.0, 1.0, 2.0, 3.0};
            const std::array<double, 4> expectedPrevious{-2.0, -1.0, 1.0, 2.0};
            for (std::size_t step = 0; step < expectedSource.size(); ++step) {
                runtime.step();
                assert(runtime.real_output_of("source") == expectedSource[step]);
                assert(runtime.real_output_of("delay") == expectedDelay[step]);
                assert(runtime.real_output_of("after_delay") == expectedDelay[step]);
                assert(runtime.real_output_of("previous") == expectedPrevious[step]);
            }
        }
    }

    {
        // A loop closed only through elements with direct feedthrough is rejected.
        for (const auto* type : {"FirFilter", "Accumulator"}) {
            s7::Schema schema{"stateful_loop_without_memory"};
            schema.set_rate(s7::Rate::from_hz(1));
            schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
            if (std::string_view{type} == "FirFilter") {
                schema.add_element("state", [] { return std::make_unique<s7::FirFilter>(std::vector<double>{0.5}); });
            } else {
                schema.add_element("state", [] { return std::make_unique<s7::Accumulator>(0.0); });
            }
            schema.connect("unit", 0, "sum", 0);
            schema.connect("state", 0, "sum", 1);
            schema.connect("sum", 0, "state", 0);

            const auto validation = s7::Validator::validate(schema);
            const auto hasCycleError = std::ranges::any_of(validation.errors, [](const std::string& error) {
                return error.find("Cycle detected without causal memory") != std::string::npos;
            });
            if (!hasCycleError) {
                throw std::runtime_error(std::string{"Expected cycle through "} + type + " to be rejected.");
            }
        }
    }

    {
        // Wrapping a graph in a nested schema does not change its trace.
        auto buildChain = [](s7::Schema& schema) {
            schema.add_element("fir", [] { return std::make_unique<s7::FirFilter>(std::vector<double>{0.5, 0.5}); });
            schema.add_element("integrator", [] { return std::make_unique<s7::Accumulator>(0.0); });
            schema.add_element("delay", [] { return std::make_unique<s7::Delay>(0.0); });
            schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
            schema.connect("fir", 0, "integrator", 0);
            schema.connect("integrator", 0, "sum", 0);
            schema.connect("delay", 0, "sum", 1);
            schema.connect("sum", 0, "delay", 0);
        };

        s7::Schema flat{"flat"};
        flat.set_rate(s7::Rate::from_hz(1));
        flat.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        buildChain(flat);
        flat.connect("source", 0, "fir", 0);

        s7::Schema wrapped{"wrapped"};
        wrapped.set_rate(s7::Rate::from_hz(1));
        wrapped.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        wrapped.add_element("nested", [buildChain] {
            auto inner = std::make_unique<s7::Schema>("chain");
            inner->set_rate(s7::Rate::from_hz(1));
            buildChain(*inner);
            inner->expose_input("in", s7::SignalType::F64, "fir", 0);
            inner->expose_output("out", s7::SignalType::F64, "sum", 0);
            return inner;
        });
        wrapped.connect("source", 0, "nested", 0);

        s7::NodeRuntime flatRuntime{s7::Compiler::compile(flat)};
        s7::NodeRuntime wrappedRuntime{s7::Compiler::compile(wrapped)};
        for (int step = 0; step < 6; ++step) {
            flatRuntime.step();
            wrappedRuntime.step();
            assert(flatRuntime.real_output_of("sum") == wrappedRuntime.real_output_of("nested"));
        }
        assert(flatRuntime.real_output_of("sum") != 0.0);
    }

    {
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);

        const auto library = s7::SchemaLibraryLoader::load_from_json(make_schema_library_json());
        s7::SchemaLibraryLoader::register_library(library, registry);

        const auto* descriptor = registry.find("DoubleGain");
        assert(descriptor != nullptr);

        const auto& metadata = descriptor->metadata();
        assert(metadata.schema_definition.has_value());
        assert(metadata.library == library.name);
        assert(metadata.input_ports.size() == 1);
        assert(metadata.output_ports.size() == 1);

        const s7::ElementCreationContext ctx{metadata, boost::json::object{}};
        auto libraryElement = descriptor->create(ctx);
        assert(libraryElement != nullptr);
        assert(libraryElement->type_name() == "Schema");

        s7::Schema schema{"library_use"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(3.0); });
        schema.add_element("gain", [descriptor] {
            const auto& descriptorMetadata = descriptor->metadata();
            const s7::ElementCreationContext libraryCtx{descriptorMetadata, boost::json::object{}};
            return descriptor->create(libraryCtx);
        });
        schema.connect("source", 0, "gain", 0);

        const auto validation = s7::Validator::validate(schema);
        assert(validation.ok());

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("gain") - 6.0) < 1e-12);

        s7::ElementCatalogService catalog(registry);
        const auto* catalogEntry = catalog.find_element("DoubleGain");
        if (!catalogEntry || catalogEntry->input_count != 1 || catalogEntry->output_count != 1) {
            throw std::runtime_error("Schema library catalog entry was not indexed correctly.");
        }
    }

    {
        const auto malformed_schema = boost::json::parse(R"json({
            "name": "bad_library_schema",
            "elements": [
                {
                    "id": "gain",
                    "type": "Gain",
                    "params": "not-an-object"
                }
            ],
            "connections": [],
            "exposed_inputs": [
                {
                    "name": "in",
                    "type": "F64",
                    "element": "gain",
                    "port": 0
                }
            ],
            "exposed_outputs": [
                {
                    "name": "out",
                    "type": "F64",
                    "element": "gain",
                    "port": 0
                }
            ]
        })json");

        bool threw = false;
        try {
            auto schema = s7::SchemaLibraryLoader::build_schema(malformed_schema, s7::ElementRegistry::instance());
            (void)schema;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected malformed schema library params to be rejected.");
        }
    }

    {
        // Direct construction: each function against its std:: reference.
        auto check = [](s7::TrigFunction function, const double x, const double expected) {
            s7::TrigT<s7::SignalType::F64> trig{function};
            const s7::RealValue input{x};
            const std::array<s7::InputBinding, 1> binding{s7::bind_input<s7::SignalType::F64>(input)};
            trig.bind_inputs({binding.data(), binding.size()});
            trig.step();
            assert(std::abs(s7::bound_value<s7::SignalType::F64>(trig.outputs()[0]) - expected) < 1e-12);
        };
        check(s7::TrigFunction::Sin, 0.5, std::sin(0.5));
        check(s7::TrigFunction::Cos, 0.5, std::cos(0.5));
        check(s7::TrigFunction::Tan, 0.5, std::tan(0.5));
        check(s7::TrigFunction::Asin, 0.5, std::asin(0.5));
        check(s7::TrigFunction::Acos, 0.5, std::acos(0.5));
        check(s7::TrigFunction::Atan, 0.5, std::atan(0.5));
        check(s7::TrigFunction::Sinh, 0.5, std::sinh(0.5));
        check(s7::TrigFunction::Cosh, 0.5, std::cosh(0.5));
        check(s7::TrigFunction::Tanh, 0.5, std::tanh(0.5));

        bool threw = false;
        try {
            static_cast<void>(s7::trig_function_from_name("bogus"));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected an unknown Trig function name to be rejected.");
        }
    }

    {
        // Through the registry: the mathematical pendulum's v' = -omega0^2 * sin(x) needs exactly this.
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        const auto* trig = registry.find("Trig");
        assert(trig != nullptr);

        s7::Schema schema{"pendulum_term"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("x", [] { return std::make_unique<s7::Constant>(0.3); });
        schema.add_element(s7::make_element_spec("sin_x", *trig, {{"function", "sin"}}));
        schema.connect("x", 0, "sin_x", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("sin_x") - std::sin(0.3)) < 1e-12);

        // Pinned to an integer type: no sensible sin/cos, so refused like any
        // other element outside its allowed type set.
        s7::Schema intSchema{"int_trig"};
        intSchema.set_rate(s7::Rate::from_hz(1));
        intSchema.add_element("x", [] { return std::make_unique<s7::ConstantT<s7::SignalType::I32>>(1); });
        intSchema.add_element(s7::make_element_spec("f", *trig, {{"type", "I32"}}));
        intSchema.connect("x", 0, "f", 0);
        const auto intValidation = s7::Validator::validate(intSchema);
        if (intValidation.ok()) {
            throw std::runtime_error("Expected Trig pinned to I32 to be rejected.");
        }
    }

    {
        // A causal-order element that faults (Divider's zero-denominator
        // check) must not leave an earlier causal element's published output
        // partially advanced: the whole step is reverted, not torn.
        s7::Schema schema{"causal_atomicity"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.add_element("accum", [] { return std::make_unique<s7::Accumulator>(0.0); });
        schema.add_element("num", [] { return std::make_unique<s7::ConstantT<s7::SignalType::F64>>(1.0); });
        schema.add_element("zero", [] { return std::make_unique<s7::ConstantT<s7::SignalType::F64>>(0.0); });
        schema.add_element("div", [] { return std::make_unique<s7::DividerT<s7::SignalType::F64>>(); });
        schema.connect("source", 0, "accum", 0);
        schema.connect("num", 0, "div", 0);
        schema.connect("zero", 0, "div", 1);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        for (int attempt = 0; attempt < 2; ++attempt) {
            bool threw = false;
            try {
                runtime.step();
            } catch (const std::domain_error&) {
                threw = true;
            }
            if (!threw) {
                throw std::runtime_error("Expected the zero-denominator Divider to fault the step.");
            }
            // 'accum' runs before 'div' in causal order; its output must stay
            // at its pre-step value, not the sum it computed before the fault.
            if (runtime.real_output_of("accum") != 0.0) {
                throw std::runtime_error("A faulted step left a torn (partially updated) causal output.");
            }
        }
    }

    {
        // The outputs NodeRuntime::step() publishes from the *previous*
        // step's memory_order results, right before causal_order runs, must
        // also be reverted on a causal fault: otherwise a memory element
        // (Delay here) shows the new step while causal elements still show
        // the old one.
        s7::Schema schema{"memory_publish_atomicity"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("zero", [] { return std::make_unique<s7::Constant>(0.0); });
        schema.add_element("delay", [] { return std::make_unique<s7::Delay>(1.0); });
        schema.add_element("num", [] { return std::make_unique<s7::ConstantT<s7::SignalType::F64>>(1.0); });
        schema.add_element("div", [] { return std::make_unique<s7::DividerT<s7::SignalType::F64>>(); });
        schema.connect("zero", 0, "delay", 0);
        schema.connect("num", 0, "div", 0);
        schema.connect("delay", 0, "div", 1);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step(); // delay outputs its initial_value (1.0); div = 1/1, no fault.
        if (runtime.real_output_of("delay") != 1.0 || runtime.real_output_of("div") != 1.0) {
            throw std::runtime_error("Unexpected first-step Delay/Divider output.");
        }

        // Step 2 publishes delay's queued 0.0 before causal_order runs, so
        // div's denominator is 0 and the step faults.
        bool threw = false;
        try {
            runtime.step();
        } catch (const std::domain_error&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected the second step's zero denominator to fault.");
        }
        if (runtime.real_output_of("delay") != 1.0) {
            throw std::runtime_error("A faulted step left Delay's output published to the next step's value.");
        }
        if (runtime.real_output_of("div") != 1.0) {
            throw std::runtime_error("A faulted step left a torn (partially updated) causal output.");
        }

        // The reverted step must be retried in full on the next call, not
        // skipped: it should fault again with the same (still-staged) values.
        threw = false;
        try {
            runtime.step();
        } catch (const std::domain_error&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected the retried step to fault again with the same staged values.");
        }
    }

    {
        // Sqrt and Log fail fast on out-of-domain input, like Reciprocal,
        // instead of silently producing NaN/-inf.
        auto sqrtFn = std::make_unique<s7::MathFunctionT<s7::SignalType::F64>>(s7::MathFunctionOperation::Sqrt);
        const double negative = -4.0;
        const std::array<s7::InputBinding, 1> sqrtBinding{s7::bind_input<s7::SignalType::F64>(negative)};
        sqrtFn->bind_inputs({sqrtBinding.data(), sqrtBinding.size()});
        bool threw = false;
        try {
            sqrtFn->step();
        } catch (const std::domain_error&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected MathFunction sqrt of a negative input to be rejected.");
        }

        auto logFn = std::make_unique<s7::MathFunctionT<s7::SignalType::F64>>(s7::MathFunctionOperation::Log);
        const double nonPositive = 0.0;
        const std::array<s7::InputBinding, 1> logBinding{s7::bind_input<s7::SignalType::F64>(nonPositive)};
        logFn->bind_inputs({logBinding.data(), logBinding.size()});
        threw = false;
        try {
            logFn->step();
        } catch (const std::domain_error&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected MathFunction log of a non-positive input to be rejected.");
        }
    }

    {
        // MinMax ignores a NaN input while another input is valid, and
        // propagates NaN only when every input is NaN.
        const double nanValue = std::nan("");
        const double low = 1.0;
        const double high = 2.0;

        auto minOfThree = std::make_unique<s7::MinMaxT<s7::SignalType::F64>>(3, false);
        const std::array<s7::InputBinding, 3> minBindings{
            s7::bind_input<s7::SignalType::F64>(nanValue),
            s7::bind_input<s7::SignalType::F64>(low),
            s7::bind_input<s7::SignalType::F64>(high),
        };
        minOfThree->bind_inputs({minBindings.data(), minBindings.size()});
        minOfThree->step();
        if (s7::bound_value<s7::SignalType::F64>(minOfThree->outputs()[0]) != low) {
            throw std::runtime_error("Expected MinMax to ignore a leading NaN input.");
        }

        auto allNan = std::make_unique<s7::MinMaxT<s7::SignalType::F64>>(2, true);
        const std::array<s7::InputBinding, 2> nanBindings{
            s7::bind_input<s7::SignalType::F64>(nanValue),
            s7::bind_input<s7::SignalType::F64>(nanValue),
        };
        allNan->bind_inputs({nanBindings.data(), nanBindings.size()});
        allNan->step();
        if (!std::isnan(s7::bound_value<s7::SignalType::F64>(allNan->outputs()[0]))) {
            throw std::runtime_error("Expected MinMax of all-NaN inputs to be NaN.");
        }
    }

    {
        // A cross-parameter invariant checked in an element's constructor
        // (Saturation's lower <= upper, which a single ParameterSpec cannot
        // express) is still reported with the element's id and type, like
        // every other parameter-validation error.
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        const auto* saturation = registry.find("Saturation");
        assert(saturation != nullptr);

        s7::Schema schema{"bad_saturation"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("x", [] { return std::make_unique<s7::Constant>(0.5); });
        schema.add_element(s7::make_element_spec("sat1", *saturation, {{"type", "F64"}, {"lower_limit", 1.0}, {"upper_limit", 0.0}}));
        schema.connect("x", 0, "sat1", 0);

        const auto validation = s7::Validator::validate(schema);
        if (validation.ok()) {
            throw std::runtime_error("Expected Saturation with lower_limit > upper_limit to be rejected.");
        }
        const auto hasIdPrefixedError = std::ranges::any_of(validation.errors, [](const std::string& error) {
            return error.find("Element 'sat1'") != std::string::npos && error.find("lower_limit") != std::string::npos;
        });
        if (!hasIdPrefixedError) {
            throw std::runtime_error("Expected the Saturation limit error to name element 'sat1'.");
        }
    }

    {
        // Comparator: each relational operation, and IEEE 754 NaN behavior
        // (false for every ordered operation and eq, true for ne).
        using Op = s7::ComparatorOperation;
        const auto compare = [](const double lhs, const double rhs, const Op op) {
            s7::ComparatorT<s7::SignalType::F64> comparator{op};
            const std::array<s7::InputBinding, 2> bindings{
                s7::bind_input<s7::SignalType::F64>(lhs),
                s7::bind_input<s7::SignalType::F64>(rhs),
            };
            comparator.bind_inputs({bindings.data(), bindings.size()});
            comparator.step();
            return s7::bound_value<s7::SignalType::F64>(comparator.outputs()[0]);
        };

        if (compare(1.0, 2.0, Op::Less) != 1.0 || compare(2.0, 1.0, Op::Less) != 0.0) {
            throw std::runtime_error("Expected Comparator 'lt' to compare lhs < rhs.");
        }
        if (compare(2.0, 2.0, Op::LessEqual) != 1.0 || compare(3.0, 2.0, Op::LessEqual) != 0.0) {
            throw std::runtime_error("Expected Comparator 'le' to compare lhs <= rhs.");
        }
        if (compare(2.0, 1.0, Op::Greater) != 1.0 || compare(1.0, 2.0, Op::Greater) != 0.0) {
            throw std::runtime_error("Expected Comparator 'gt' to compare lhs > rhs.");
        }
        if (compare(2.0, 2.0, Op::GreaterEqual) != 1.0 || compare(1.0, 2.0, Op::GreaterEqual) != 0.0) {
            throw std::runtime_error("Expected Comparator 'ge' to compare lhs >= rhs.");
        }
        if (compare(2.0, 2.0, Op::Equal) != 1.0 || compare(2.0, 3.0, Op::Equal) != 0.0) {
            throw std::runtime_error("Expected Comparator 'eq' to compare lhs == rhs.");
        }
        if (compare(2.0, 3.0, Op::NotEqual) != 1.0 || compare(2.0, 2.0, Op::NotEqual) != 0.0) {
            throw std::runtime_error("Expected Comparator 'ne' to compare lhs != rhs.");
        }

        const double nanValue = std::nan("");
        for (const auto op : {Op::Less, Op::LessEqual, Op::Greater, Op::GreaterEqual, Op::Equal}) {
            if (compare(nanValue, 1.0, op) != 0.0) {
                throw std::runtime_error("Expected Comparator to be false against NaN, except 'ne'.");
            }
        }
        if (compare(nanValue, 1.0, Op::NotEqual) != 1.0) {
            throw std::runtime_error("Expected Comparator 'ne' to be true against NaN.");
        }
    }

    {
        // Comparator through the registry: drives the "operation" string ->
        // ComparatorOperation mapping end to end (direct construction above
        // only exercises the enum itself), for every declared operation plus
        // the omitted-parameter default ("lt"), and for a non-default type.
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        const auto* comparator = registry.find("Comparator");
        assert(comparator != nullptr);

        // Each operation string against three input pairs (1,2), (2,2), (3,2):
        // the six expected patterns are all distinct, so a swapped string ->
        // enum mapping shows up as a wrong pattern rather than passing by luck.
        const auto expectF64 = [&](const std::string& operation, const bool lt, const bool eq, const bool gt) {
            const double pairs[3][2] = {{1.0, 2.0}, {2.0, 2.0}, {3.0, 2.0}};
            const bool expected[3] = {lt, eq, gt};
            for (int index = 0; index < 3; ++index) {
                s7::Schema schema{"comparator_f64"};
                schema.set_rate(s7::Rate::from_hz(1));
                schema.add_element("a", [&] { return std::make_unique<s7::Constant>(pairs[index][0]); });
                schema.add_element("b", [&] { return std::make_unique<s7::Constant>(pairs[index][1]); });
                schema.add_element(s7::make_element_spec("cmp", *comparator, {{"type", "F64"}, {"operation", operation}}));
                schema.connect("a", 0, "cmp", 0);
                schema.connect("b", 0, "cmp", 1);

                s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
                runtime.step();
                const double output = runtime.real_output_of("cmp");
                const double expectedOutput = expected[index] ? 1.0 : 0.0;
                if (output != expectedOutput) {
                    throw std::runtime_error("Comparator '" + operation + "' on (" + std::to_string(pairs[index][0]) + ", " +
                                             std::to_string(pairs[index][1]) + ") produced " + std::to_string(output) +
                                             ", expected " + std::to_string(expectedOutput) + ".");
                }
            }
        };
        expectF64("lt", true, false, false);
        expectF64("le", true, true, false);
        expectF64("gt", false, false, true);
        expectF64("ge", false, true, true);
        expectF64("eq", false, true, false);
        expectF64("ne", true, false, true);

        {
            // The "operation" parameter is not required: the default is "lt".
            s7::Schema schema{"comparator_default"};
            schema.set_rate(s7::Rate::from_hz(1));
            schema.add_element("a", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element("b", [] { return std::make_unique<s7::Constant>(2.0); });
            schema.add_element(s7::make_element_spec("cmp", *comparator, {{"type", "F64"}}));
            schema.connect("a", 0, "cmp", 0);
            schema.connect("b", 0, "cmp", 1);

            s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
            runtime.step();
            if (runtime.real_output_of("cmp") != 1.0) {
                throw std::runtime_error("Expected Comparator with no 'operation' parameter to default to 'lt'.");
            }
        }

        {
            // A non-F64 real type: the "T" type variable, not just the F64 alias.
            s7::Schema schema{"comparator_i16"};
            schema.set_rate(s7::Rate::from_hz(1));
            schema.add_element(s7::make_element_spec("a", *registry.find("Constant"), {{"type", "I16"}, {"value", 3}}));
            schema.add_element(s7::make_element_spec("b", *registry.find("Constant"), {{"type", "I16"}, {"value", 2}}));
            schema.add_element(s7::make_element_spec("cmp", *comparator, {{"type", "I16"}, {"operation", "gt"}}));
            schema.connect("a", 0, "cmp", 0);
            schema.connect("b", 0, "cmp", 1);

            s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
            runtime.step();
            if (runtime.output_of<s7::SignalType::I16>("cmp") != 1) {
                throw std::runtime_error("Expected Comparator 'gt' on I16 inputs (3, 2) to output 1.");
            }
        }

        {
            // An unsupported (complex) type is rejected, naming the element,
            // like every other element outside its allowed type set.
            s7::Schema schema{"bad_comparator_type"};
            schema.set_rate(s7::Rate::from_hz(1));
            schema.add_element("a", [] { return std::make_unique<s7::ComplexConstant>(); });
            schema.add_element("b", [] { return std::make_unique<s7::ComplexConstant>(); });
            schema.add_element(s7::make_element_spec("cmp1", *comparator, {{"type", "C64"}, {"operation", "lt"}}));
            schema.connect("a", 0, "cmp1", 0);
            schema.connect("b", 0, "cmp1", 1);

            const auto validation = s7::Validator::validate(schema);
            if (validation.ok()) {
                throw std::runtime_error("Expected Comparator with type C64 to be rejected.");
            }
            const auto hasIdPrefixedError = std::ranges::any_of(validation.errors, [](const std::string& error) {
                return error.find("Element 'cmp1'") != std::string::npos;
            });
            if (!hasIdPrefixedError) {
                throw std::runtime_error("Expected the Comparator type error to name element 'cmp1'.");
            }
        }

        {
            // An operation string outside enum_values is rejected by parameter
            // checking before the element is ever created, naming the element,
            // like every other declared-enum parameter.
            s7::Schema schema{"bad_comparator_operation"};
            schema.set_rate(s7::Rate::from_hz(1));
            schema.add_element("a", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element("b", [] { return std::make_unique<s7::Constant>(2.0); });
            schema.add_element(s7::make_element_spec("cmp2", *comparator, {{"type", "F64"}, {"operation", "between"}}));
            schema.connect("a", 0, "cmp2", 0);
            schema.connect("b", 0, "cmp2", 1);

            const auto validation = s7::Validator::validate(schema);
            if (validation.ok()) {
                throw std::runtime_error("Expected Comparator with an unknown operation to be rejected.");
            }
            const auto hasIdPrefixedError = std::ranges::any_of(validation.errors, [](const std::string& error) {
                return error.find("Element 'cmp2'") != std::string::npos;
            });
            if (!hasIdPrefixedError) {
                throw std::runtime_error("Expected the Comparator operation error to name element 'cmp2'.");
            }
        }
    }

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7core smoke: unknown fatal error\n";
    return 1;
}


