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
    schema.add_element("a", [] { return std::make_unique<s7::Gain>(2.0); });
    schema.add_element("b", [] { return std::make_unique<s7::Gain>(0.5); });

    schema.connect("a", 0, "b", 0);
    schema.connect("b", 0, "a", 0);
    return schema;
}

s7::Schema make_complex_schema()
{
    s7::Schema schema{"complex_path"};
    schema.add_element("c0", [] { return std::make_unique<s7::ComplexConstant>(std::complex<double>{1.0, 2.0}); });
    schema.add_element("gain", [] { return std::make_unique<s7::ComplexGain>(std::complex<double>{0.5, 0.0}); });

    schema.connect("c0", 0, "gain", 0);
    return schema;
}

s7::Schema make_implicit_conversion_schema()
{
    s7::Schema schema{"real_to_complex_with_warning"};
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
                "type": "DOUBLE",
                "element": "gain",
                "port": 0
              }
            ],
            "exposed_outputs": [
              {
                "name": "out",
                "type": "DOUBLE",
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

} // namespace

int main() try
{
    {
        auto constant = std::make_unique<s7::Constant>(1.0);
        assert(constant->output_ports().size() == 1);
        assert(constant->output_ports()[0].type == s7::SignalType::DOUBLE);
        assert(constant->output_ports()[0].name == "out");

        auto gain = std::make_unique<s7::Gain>(2.0);
        assert(gain->input_ports().size() == 1);
        assert(gain->input_ports()[0].type == s7::SignalType::DOUBLE);
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
        assert(adder->input_ports()[0].type == s7::SignalType::DOUBLE);
        assert(adder->input_ports()[1].type == s7::SignalType::DOUBLE);

        auto weightedAdder = std::make_unique<s7::Adder>(std::vector<double>{1.0, 1.0, 2.4});
        assert(weightedAdder->input_ports().size() == 3);
        assert(weightedAdder->input_ports()[2].type == s7::SignalType::DOUBLE);

        auto multiply = std::make_unique<s7::Multiply>();
        assert(multiply->input_ports().size() == 2);
        assert(multiply->input_ports()[0].type == s7::SignalType::DOUBLE);
        assert(multiply->input_ports()[1].type == s7::SignalType::DOUBLE);

        auto variadicMultiply = std::make_unique<s7::Multiply>(std::vector<double>{1.0, 0.5, 2.0});
        assert(variadicMultiply->input_ports().size() == 3);
        assert(variadicMultiply->input_ports()[2].type == s7::SignalType::DOUBLE);

        auto analog = std::make_unique<s7::AnalogOscillator>(1.0, 1.0, 1000.0);
        assert(analog->has_state());
        assert(!analog->has_causal_memory());
        assert(analog->output_ports().size() == 1);
        assert(analog->output_ports()[0].type == s7::SignalType::DOUBLE);
    }

    {
        auto int12ToDouble = std::make_unique<s7::Int12ToDouble>();
        assert(int12ToDouble->input_count() == 1);
        assert(int12ToDouble->input_ports()[0].type == s7::SignalType::INT12);
        assert(int12ToDouble->output_ports()[0].type == s7::SignalType::DOUBLE);

        auto doubleToInt12 = std::make_unique<s7::DoubleToInt12>();
        assert(doubleToInt12->input_ports()[0].type == s7::SignalType::DOUBLE);
        assert(doubleToInt12->output_ports()[0].type == s7::SignalType::INT12);

        // INT12 conversion: Q11 scaling, round half to even, saturation.
        const std::vector<std::pair<double, double>> int12Cases{
            {0.0, 0.0}, {0.5, 1024.0}, {-1.0, -2048.0}, {1.0, 2047.0}, {100.0, 2047.0}, {-100.0, -2048.0},
            {0.5 / 2048.0, 0.0}, {1.5 / 2048.0, 2.0}, {-2.5 / 2048.0, -2.0}, {std::nan(""), 0.0},
        };
        for (const auto& [input, expected] : int12Cases) {
            const std::array<s7::InputBinding, 1> binding{s7::bind_input<s7::SignalType::DOUBLE>(input)};
            doubleToInt12->bind_inputs({binding.data(), binding.size()});
            doubleToInt12->step();
            const auto code = s7::bound_value<s7::SignalType::DOUBLE>(doubleToInt12->outputs()[0]);
            assert(code == expected);
            assert(s7::is_valid_signal_value(s7::SignalType::INT12, code));

            const std::array<s7::InputBinding, 1> back{s7::bind_input<s7::SignalType::INT12>(code)};
            int12ToDouble->bind_inputs({back.data(), back.size()});
            int12ToDouble->step();
            assert(s7::bound_value<s7::SignalType::DOUBLE>(int12ToDouble->outputs()[0]) == code / 2048.0);
        }

        // Every integer type has a defined range, quantization and wire id.
        assert(s7::signal_type_info(s7::SignalType::UINT8).max_value == 255.0);
        assert(s7::signal_type_info(s7::SignalType::INT16).min_value == -32768.0);
        assert(s7::signal_type_info(s7::SignalType::DOUBLE).wire_type_id == 0x000F);
        assert(s7::quantize_to_signal_type(s7::SignalType::UINT8, -3.0) == 0.0);
        assert(s7::quantize_to_signal_type(s7::SignalType::UINT16, 70000.0) == 65535.0);
        assert(s7::quantize_to_signal_type(s7::SignalType::INT8, 2.5) == 2.0);
        assert(s7::quantize_to_signal_type(s7::SignalType::DOUBLE, 2.5) == 2.5);
        assert(!s7::is_valid_signal_value(s7::SignalType::UINT12, 4096.0));
        assert(!s7::is_valid_signal_value(s7::SignalType::INT8, 1.5));

        // Standalone step() writes the next-step output; NodeRuntime defers its publication.
        auto previousValue = std::make_unique<s7::PreviousValue>(3.5);
        assert(previousValue->has_causal_memory());
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previousValue->outputs()[0]) == 3.5);
        const s7::RealValue pvIn1{8.0};
        const s7::RealValue pvIn2{9.0};
        const std::array<s7::InputBinding, 1> pvInputs1{s7::bind_input<s7::SignalType::DOUBLE>(pvIn1)};
        const std::array<s7::InputBinding, 1> pvInputs2{s7::bind_input<s7::SignalType::DOUBLE>(pvIn2)};
        previousValue->bind_inputs({pvInputs1.data(), pvInputs1.size()});
        previousValue->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previousValue->outputs()[0]) == 8.0);
        previousValue->bind_inputs({pvInputs2.data(), pvInputs2.size()});
        previousValue->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previousValue->outputs()[0]) == 9.0);

        auto integrator = std::make_unique<s7::Integrator>(1.5);
        assert(integrator->has_state());
        assert(!integrator->has_causal_memory());
        const s7::RealValue intIn1{2.0};
        const s7::RealValue intIn2{-0.5};
        const std::array<s7::InputBinding, 1> intInputs1{s7::bind_input<s7::SignalType::DOUBLE>(intIn1)};
        const std::array<s7::InputBinding, 1> intInputs2{s7::bind_input<s7::SignalType::DOUBLE>(intIn2)};
        integrator->bind_inputs({intInputs1.data(), intInputs1.size()});
        integrator->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(integrator->outputs()[0]) == 3.5);
        integrator->bind_inputs({intInputs2.data(), intInputs2.size()});
        integrator->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(integrator->outputs()[0]) == 3.0);

        auto complexToMagPhase = std::make_unique<s7::ComplexToMagnitudePhase>();
        assert(complexToMagPhase->input_ports()[0].type == s7::SignalType::COMPLEX);
        assert(complexToMagPhase->output_count() == 2);
        assert(complexToMagPhase->output_ports()[0].name == "magnitude");
        assert(complexToMagPhase->output_ports()[1].name == "phase");
        const s7::ComplexValue mpIn{3.0, 4.0};
        const std::array<s7::InputBinding, 1> mpInputs{s7::bind_input<s7::SignalType::COMPLEX>(mpIn)};
        complexToMagPhase->bind_inputs({mpInputs.data(), mpInputs.size()});
        complexToMagPhase->step();
        const auto magPhase = complexToMagPhase->outputs();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(magPhase[0]) == 5.0);
        assert(std::abs(s7::bound_value<s7::SignalType::DOUBLE>(magPhase[1]) - std::atan2(4.0, 3.0)) < 1e-12);

        auto fir = std::make_unique<s7::FirFilter>(std::vector<double>{0.5, 0.5});
        const s7::RealValue firIn1{2.0};
        const s7::RealValue firIn2{4.0};
        const std::array<s7::InputBinding, 1> firInputs1{s7::bind_input<s7::SignalType::DOUBLE>(firIn1)};
        const std::array<s7::InputBinding, 1> firInputs2{s7::bind_input<s7::SignalType::DOUBLE>(firIn2)};
        fir->bind_inputs({firInputs1.data(), firInputs1.size()});
        fir->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(fir->outputs()[0]) == 1.0);
        fir->bind_inputs({firInputs2.data(), firInputs2.size()});
        fir->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(fir->outputs()[0]) == 3.0);

        auto iir = std::make_unique<s7::IirFilter>(1.0, 0.0, 0.5);
        const s7::RealValue iirIn{1.0};
        const std::array<s7::InputBinding, 1> iirInputs{s7::bind_input<s7::SignalType::DOUBLE>(iirIn)};
        iir->bind_inputs({iirInputs.data(), iirInputs.size()});
        iir->step();
        assert(std::abs(s7::bound_value<s7::SignalType::DOUBLE>(iir->outputs()[0]) - 1.0) < 1e-12);
        iir->step();
        assert(std::abs(s7::bound_value<s7::SignalType::DOUBLE>(iir->outputs()[0]) - 0.5) < 1e-12);
    }

    {
        auto makeComposite = [] {
            auto schema = std::make_unique<s7::Schema>("composite_gain");
            schema->add_element("inner_gain", [] { return std::make_unique<s7::Gain>(2.5); });
            schema->expose_input("in", s7::SignalType::DOUBLE, "inner_gain", 0);
            schema->expose_output("out", s7::SignalType::DOUBLE, "inner_gain", 0);
            return schema;
        };
        {
            auto probeComposite = makeComposite();
            assert(probeComposite->type_name() == "Schema");
            assert(probeComposite->input_count() == 1);
            assert(probeComposite->output_count() == 1);
        }

        s7::Schema outer{"outer_with_schema_element"};
        outer.add_element("source", [] { return std::make_unique<s7::Constant>(4.0); });
        outer.add_element("composite", [makeComposite] { return makeComposite(); });
        outer.connect("source", 0, "composite", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(outer)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("composite") - 10.0) < 1e-12);
    }

    {
        s7::Schema root{"nested_schema"};
        root.add_element("source", [] { return std::make_unique<s7::Constant>(3.0); });
        root.add_element("nested", [] {
            auto inner = std::make_unique<s7::Schema>("inner");
            inner->add_element("g1", [] { return std::make_unique<s7::Gain>(2.0); });
            inner->add_element("g2", [] { return std::make_unique<s7::Gain>(5.0); });
            inner->connect("g1", 0, "g2", 0);
            inner->expose_input("in", s7::SignalType::DOUBLE, "g1", 0);
            inner->expose_output("out", s7::SignalType::DOUBLE, "g2", 0);
            return inner;
        });
        root.connect("source", 0, "nested", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(root)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("nested") - 30.0) < 1e-12);
    }

    {
        s7::Schema outer{"invalid_nested_schema"};
        outer.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        outer.add_element("broken_nested", [] {
            auto inner = std::make_unique<s7::Schema>("broken");
            inner->add_element("g", [] { return std::make_unique<s7::Gain>(2.0); });
            inner->expose_input("in", s7::SignalType::DOUBLE, "g", 0);
            inner->expose_output("out", s7::SignalType::DOUBLE, "missing", 0);
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
            return error.find("Port type mismatch") != std::string::npos;
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
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(2.0); });
        schema.add_element("integrator", [] { return std::make_unique<s7::Integrator>(0.0); });
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
            std::vector<std::pair<std::string, s7::ElementFactory>> elements{
                {"source", [] { return std::make_unique<s7::Integrator>(0.0); }},
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
        for (const auto* type : {"FirFilter", "Integrator"}) {
            s7::Schema schema{"stateful_loop_without_memory"};
            schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
            if (std::string_view{type} == "FirFilter") {
                schema.add_element("state", [] { return std::make_unique<s7::FirFilter>(std::vector<double>{0.5}); });
            } else {
                schema.add_element("state", [] { return std::make_unique<s7::Integrator>(0.0); });
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
            schema.add_element("integrator", [] { return std::make_unique<s7::Integrator>(0.0); });
            schema.add_element("delay", [] { return std::make_unique<s7::Delay>(0.0); });
            schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
            schema.connect("fir", 0, "integrator", 0);
            schema.connect("integrator", 0, "sum", 0);
            schema.connect("delay", 0, "sum", 1);
            schema.connect("sum", 0, "delay", 0);
        };

        s7::Schema flat{"flat"};
        flat.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        buildChain(flat);
        flat.connect("source", 0, "fir", 0);

        s7::Schema wrapped{"wrapped"};
        wrapped.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        wrapped.add_element("nested", [buildChain] {
            auto inner = std::make_unique<s7::Schema>("chain");
            buildChain(*inner);
            inner->expose_input("in", s7::SignalType::DOUBLE, "fir", 0);
            inner->expose_output("out", s7::SignalType::DOUBLE, "sum", 0);
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
        assert(metadata.input_ports.size() == 1);
        assert(metadata.output_ports.size() == 1);

        const s7::ElementCreationContext ctx{metadata, boost::json::object{}};
        auto libraryElement = descriptor->create(ctx);
        assert(libraryElement != nullptr);
        assert(libraryElement->type_name() == "Schema");

        s7::Schema schema{"library_use"};
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
                    "type": "DOUBLE",
                    "element": "gain",
                    "port": 0
                }
            ],
            "exposed_outputs": [
                {
                    "name": "out",
                    "type": "DOUBLE",
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

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7core smoke: unknown fatal error\n";
    return 1;
}


