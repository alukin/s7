#include <algorithm>
#include <array>
#include <cmath>
#include <cassert>
#include <complex>
#include <memory>
#include <stdexcept>
#include <string>

#include "s7/core.hpp"
#include "s7/observe.hpp"

namespace {

s7::Schema make_valid_schema() {
    s7::Schema schema{"valid_feedback"};
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("memory", [] { return std::make_unique<s7::Delay>(0.0); });

    schema.connect("unit", 0, "sum", 0);
    schema.connect("memory", 0, "sum", 1);
    schema.connect("sum", 0, "memory", 0);
    return schema;
}

s7::Schema make_invalid_schema() {
    s7::Schema schema{"invalid_cycle"};
    schema.add_element("a", [] { return std::make_unique<s7::Gain>(2.0); });
    schema.add_element("b", [] { return std::make_unique<s7::Gain>(0.5); });

    schema.connect("a", 0, "b", 0);
    schema.connect("b", 0, "a", 0);
    return schema;
}

s7::Schema make_complex_schema() {
    s7::Schema schema{"complex_path"};
    schema.add_element("c0", [] { return std::make_unique<s7::ComplexConstant>(std::complex<double>{1.0, 2.0}); });
    schema.add_element("gain", [] { return std::make_unique<s7::ComplexGain>(std::complex<double>{0.5, 0.0}); });

    schema.connect("c0", 0, "gain", 0);
    return schema;
}

s7::Schema make_implicit_conversion_schema() {
    s7::Schema schema{"real_to_complex_with_warning"};
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("cgain", [] { return std::make_unique<s7::ComplexGain>(std::complex<double>{2.0, 0.0}); });

    // Allowed with warning: DOUBLE -> COMPLEX.
    schema.connect("unit", 0, "cgain", 0);
    return schema;
}

} // namespace

int main() {
    {
        auto constant = std::make_unique<s7::Constant>(1.0);
        auto output_ports = constant->output_ports();
        assert(output_ports.size() == 1);
        assert(output_ports[0].type == s7::SignalType::DOUBLE);
        assert(output_ports[0].name == "out");

        auto gain = std::make_unique<s7::Gain>(2.0);
        auto gain_inputs = gain->input_ports();
        assert(gain_inputs.size() == 1);
        assert(gain_inputs[0].type == s7::SignalType::DOUBLE);
        assert(gain_inputs[0].name == "in");

        auto adder = std::make_unique<s7::Adder>();
        auto adder_inputs = adder->input_ports();
        assert(adder_inputs.size() == 2);
        assert(adder_inputs[0].type == s7::SignalType::DOUBLE);
        assert(adder_inputs[1].type == s7::SignalType::DOUBLE);
    }

    {
        auto int12_to_double = std::make_unique<s7::Int12ToDouble>();
        assert(int12_to_double->input_count() == 1);
        assert(int12_to_double->input_ports()[0].type == s7::SignalType::INT12);
        assert(int12_to_double->output_ports()[0].type == s7::SignalType::DOUBLE);

        auto double_to_int12 = std::make_unique<s7::DoubleToInt12>();
        assert(double_to_int12->input_ports()[0].type == s7::SignalType::DOUBLE);
        assert(double_to_int12->output_ports()[0].type == s7::SignalType::INT12);

        auto previous_value = std::make_unique<s7::PreviousValue>(3.5);
        assert(previous_value->has_causal_memory());
        const s7::RealValue pv_in1{8.0};
        const s7::RealValue pv_in2{9.0};
        const std::array<s7::InputBinding, 1> pv_inputs1{s7::bind_input<s7::SignalType::DOUBLE>(pv_in1)};
        const std::array<s7::InputBinding, 1> pv_inputs2{s7::bind_input<s7::SignalType::DOUBLE>(pv_in2)};
        previous_value->bind_inputs({pv_inputs1.data(), pv_inputs1.size()});
        previous_value->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previous_value->outputs()[0]) == 3.5);
        previous_value->bind_inputs({pv_inputs2.data(), pv_inputs2.size()});
        previous_value->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(previous_value->outputs()[0]) == 8.0);

        auto complex_to_mag_phase = std::make_unique<s7::ComplexToMagnitudePhase>();
        assert(complex_to_mag_phase->input_ports()[0].type == s7::SignalType::COMPLEX);
        assert(complex_to_mag_phase->output_count() == 2);
        assert(complex_to_mag_phase->output_ports()[0].name == "magnitude");
        assert(complex_to_mag_phase->output_ports()[1].name == "phase");
        const s7::ComplexValue mp_in{3.0, 4.0};
        const std::array<s7::InputBinding, 1> mp_inputs{s7::bind_input<s7::SignalType::COMPLEX>(mp_in)};
        complex_to_mag_phase->bind_inputs({mp_inputs.data(), mp_inputs.size()});
        complex_to_mag_phase->step();
        const auto mag_phase = complex_to_mag_phase->outputs();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(mag_phase[0]) == 5.0);
        assert(std::abs(s7::bound_value<s7::SignalType::DOUBLE>(mag_phase[1]) - std::atan2(4.0, 3.0)) < 1e-12);

        auto fir = std::make_unique<s7::FirFilter>(std::vector<double>{0.5, 0.5});
        const s7::RealValue fir_in1{2.0};
        const s7::RealValue fir_in2{4.0};
        const std::array<s7::InputBinding, 1> fir_inputs1{s7::bind_input<s7::SignalType::DOUBLE>(fir_in1)};
        const std::array<s7::InputBinding, 1> fir_inputs2{s7::bind_input<s7::SignalType::DOUBLE>(fir_in2)};
        fir->bind_inputs({fir_inputs1.data(), fir_inputs1.size()});
        fir->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(fir->outputs()[0]) == 1.0);
        fir->bind_inputs({fir_inputs2.data(), fir_inputs2.size()});
        fir->step();
        assert(s7::bound_value<s7::SignalType::DOUBLE>(fir->outputs()[0]) == 3.0);

        auto iir = std::make_unique<s7::IirFilter>(1.0, 0.0, 0.5);
        const s7::RealValue iir_in{1.0};
        const std::array<s7::InputBinding, 1> iir_inputs{s7::bind_input<s7::SignalType::DOUBLE>(iir_in)};
        iir->bind_inputs({iir_inputs.data(), iir_inputs.size()});
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
        assert(threw);
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
        const auto schema = make_complex_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(validation.ok());

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        const auto out = runtime.complex_output_of("gain");
        assert(out.real() == 0.5);
        assert(out.imag() == 1.0);
    }

    {
        const auto schema = make_implicit_conversion_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(!validation.ok());

        const auto has_mismatch_error = std::ranges::any_of(validation.errors, [](const std::string& error) {
            return error.find("Port type mismatch") != std::string::npos;
        });

        assert(has_mismatch_error);
    }

    {
        const auto schema = make_invalid_schema();
        const auto validation = s7::Validator::validate(schema);
        assert(!validation.ok());

        const auto has_cycle_error = std::ranges::any_of(validation.errors, [](const std::string& error) {
            return error.find("Cycle detected without causal memory") != std::string::npos;
        });

        assert(has_cycle_error);
    }

    return 0;
}

