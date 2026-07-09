#ifndef S7CORE_INCLUDE_S7_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ELEMENTS_HPP

#include "s7/element.hpp"
#include <vector>

namespace s7 {

// ============================================================================
// Helper to determine SignalType from C++ type
// ============================================================================

template <typename T>
constexpr SignalType signal_type_for() {
    if constexpr (std::is_same_v<T, ComplexValue>) {
        return SignalType::COMPLEX;
    } else {
        return SignalType::DOUBLE;
    }
}

// ============================================================================
// Templated signal processing elements - CLEAN like s7pp!
// Element owns storage, ports reference it
// ============================================================================

/// Constant source: 0 inputs, 1 output of type T
template <typename T>
class ConstantT final : public Element {
public:
    explicit ConstantT(T value)
        : Element("Constant"),
          output_value_(value)
    {
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override {
        // Output already holds the constant value, nothing to do!
    }

private:
    T output_value_;  // Element owns the storage
};

/// Gain (amplifier): 1 input, 1 output of type T
template <typename T>
class GainT final : public Element {
public:
    explicit GainT(T factor)
        : Element("Gain"),
          factor_(factor),
          output_value_{}
    {
        register_input(0, port_names::in, signal_type_for<T>(), nullptr);  // Input doesn't need storage
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override {
        output_value_ = read_input<T>(0) * factor_;  // Direct assignment!
    }

private:
    T factor_;
    T output_value_;  // Element owns the storage
};

/// Adder: 2 inputs, 1 output of type T
template <typename T>
class AdderT final : public Element {
public:
    AdderT()
        : Element("Adder"),
          output_value_{}
    {
        register_input(0, port_names::lhs, signal_type_for<T>(), nullptr);
        register_input(1, port_names::rhs, signal_type_for<T>(), nullptr);
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override {
        output_value_ = read_input<T>(0) + read_input<T>(1);  // Direct assignment!
    }

private:
    T output_value_;  // Element owns the storage
};

/// Multiplier: 2 inputs, 1 output of type T
template <typename T>
class MultiplyT final : public Element {
public:
    MultiplyT()
        : Element("Multiply"),
          output_value_{}
    {
        register_input(0, port_names::lhs, signal_type_for<T>(), nullptr);
        register_input(1, port_names::rhs, signal_type_for<T>(), nullptr);
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override {
        output_value_ = read_input<T>(0) * read_input<T>(1);
    }

private:
    T output_value_;  // Element owns the storage
};

/// Delay (z^-1): 1 input, 1 output of type T, stateful
template <typename T>
class DelayT final : public Element {
public:
    explicit DelayT(T initial_value = T{})
        : Element("Delay", true),  // has_state = true
          prev_value_(initial_value),
          output_value_(initial_value)
    {
        register_input(0, port_names::in, signal_type_for<T>(), nullptr);
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override {
        // Update for next step
        prev_value_ = read_input<T>(0);
        output_value_ = prev_value_;  // Direct assignment!
    }

private:
    T prev_value_;
    T output_value_;  // Element owns the storage
};

/// Classical digital integrator: accumulates the input into the output state.
class Integrator final : public Element {
public:
    explicit Integrator(RealValue initial_value = 0.0);
    void step() override;

private:
    RealValue output_value_;
};

/// Reference sinusoidal oscillator using sin(phase).
class AnalogOscillator final : public Element {
public:
    AnalogOscillator(RealValue amplitude, RealValue frequency_hz, RealValue sample_rate_hz, RealValue phase = 0.0);
    void step() override;

private:
    RealValue amplitude_;
    RealValue phase_;
    RealValue phase_increment_;
    RealValue output_value_;
};

// ============================================================================
// Type aliases for concrete element types
// ============================================================================

using Constant = ConstantT<RealValue>;
using ComplexConstant = ConstantT<ComplexValue>;

using Gain = GainT<RealValue>;
using ComplexGain = GainT<ComplexValue>;

using Adder = AdderT<RealValue>;
using ComplexAdder = AdderT<ComplexValue>;

using Multiply = MultiplyT<RealValue>;
using ComplexMultiply = MultiplyT<ComplexValue>;

using Delay = DelayT<RealValue>;
using ComplexDelay = DelayT<ComplexValue>;

// ============================================================================
// Real-valued specific elements (no complex equivalent)
// ============================================================================


// ============================================================================
// Real-valued specific elements (no complex equivalent)
// ============================================================================

/// FIR filter: 1 input, 1 output, stateful
class FirFilter final : public Element {
public:
    explicit FirFilter(std::vector<RealValue> taps);
    void step() override;

private:
    std::vector<RealValue> taps_;
    std::vector<RealValue> history_;
    RealValue output_value_;
};

/// IIR filter (1st order): 1 input, 1 output, stateful
class IirFilter final : public Element {
public:
    IirFilter(RealValue b0, RealValue b1, RealValue a1);
    void step() override;

private:
    RealValue b0_, b1_, a1_;
    RealValue x1_{0.0}, y1_{0.0};
    RealValue output_value_;
};

/// Output the previous sample (unit delay) - for standalone testing
class PreviousValue final : public Element {
public:
    explicit PreviousValue(RealValue initial_value = 0.0);
    void step() override;

private:
    RealValue previous_;
    RealValue output_value_;
};

// ============================================================================
// Type conversion elements
// ============================================================================

/// Combine real and imaginary parts into complex: 2 DOUBLE inputs, 1 COMPLEX output
class ReImToComplex final : public Element {
public:
    ReImToComplex();
    void step() override;

private:
    ComplexValue output_value_;
};

/// Split complex into real and imaginary: 1 COMPLEX input, 2 DOUBLE outputs
class ComplexToReIm final : public Element {
public:
    ComplexToReIm();
    void step() override;

private:
    RealValue output_re_;
    RealValue output_im_;
};

/// Split complex into magnitude and phase: 1 COMPLEX input, 2 DOUBLE outputs
class ComplexToMagnitudePhase final : public Element {
public:
    ComplexToMagnitudePhase();
    void step() override;
    void bind_inputs(std::span<const InputBinding> bindings) override;

private:
    RealValue output_magnitude_;
    RealValue output_phase_;
};

/// Convert int12 to double (ADC simulation)
class Int12ToDouble final : public Element {
public:
    Int12ToDouble();
    void step() override;

private:
    RealValue output_value_;
};

/// Convert double to int12 (DAC simulation)
class DoubleToInt12 final : public Element {
public:
    DoubleToInt12();
    void step() override;

private:
    RealValue output_value_;
};


} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENTS_HPP
