#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "s7/core.hpp"
#include "s7/observe.hpp"

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

    // Allowed with warning: DOUBLE -> COMPLEX.
    schema.connect("unit", 0, "cgain", 0);
    return schema;
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
        assert(analog->has_causal_memory());
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

        auto previousValue = std::make_unique<s7::PreviousValue>(3.5);
        assert(previousValue->has_causal_memory());
        const s7::RealValue pvIn1{8.0};
        const s7::RealValue pvIn2{9.0};
        const std::array<s7::InputBinding, 1> pvInputs1{s7::bind_input<s7::SignalType::DOUBLE>(pvIn1)};
        const std::array<s7::InputBinding, 1> pvInputs2{s7::bind_input<s7::SignalType::DOUBLE>(pvIn2)};
        previousValue->bind_inputs({pvInputs1.data(), pvInputs1.size()});
        previousValue->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previousValue->outputs()[0]) == 3.5);
        previousValue->bind_inputs({pvInputs2.data(), pvInputs2.size()});
        previousValue->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previousValue->outputs()[0]) == 8.0);

        auto integrator = std::make_unique<s7::Integrator>(1.5);
        assert(integrator->has_causal_memory());
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
        std::unique_ptr<s7::Element> composite = std::make_unique<s7::Schema>("composite");
        assert(composite->type_name() == "Schema");
        assert(composite->input_count() == 0);
        assert(composite->output_count() == 0);

        bool threw = false;
        try {
            composite->step();
        } catch (const std::logic_error&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Schema::step() was expected to throw std::logic_error.");
        }
    }

    {
        const auto schema = make_valid_schema();
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
        assert(samples[0].as_real() == 2.0);
        assert(samples[1].as_real() == 2.0);
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

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7tests: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7tests: unknown fatal error\n";
    return 1;
}
