#ifndef S7CORE_INCLUDE_S7_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ELEMENTS_HPP

#include "s7/element.hpp"

#include <cmath>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace s7 {

// ============================================================================
// Helper to determine SignalType from C++ type
// ============================================================================
template <typename T>
constexpr SignalType signal_type_for()
{
    if constexpr (std::is_same_v<T, ComplexValue>) {
        return SignalType::COMPLEX;
    } else {
        return SignalType::DOUBLE;
    }
}

// ============================================================================
// Templated signal processing elements
// Element owns storage, ports reference it
// ============================================================================
/// Constant source: 0 inputs, 1 output of type T
template <typename T>
class ConstantT final : public Element {
public:
    explicit ConstantT(T value)
        : Element("Constant")
        , output_value_(value)
    {
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override
    {
        // Output already holds the constant value.
    }

private:
    T output_value_;
};

/// Gain (amplifier): 1 input, 1 output of type T
template <typename T>
class GainT final : public Element {
public:
    explicit GainT(T factor)
        : Element("Gain")
        , factor_(factor)
        , output_value_{}
    {
        register_input(0, port_names::in, signal_type_for<T>(), nullptr);
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override
    {
        output_value_ = read_input<T>(0) * factor_;
    }

private:
    T factor_;
    T output_value_;
};

/// Adder: weighted sum of N inputs, 1 output of type T
template <typename T>
class AdderT final : public Element {
public:
    explicit AdderT(std::vector<RealValue> weights = {1.0, 1.0})
        : Element("Adder")
        , weights_(weights.empty() ? std::vector<RealValue>{1.0, 1.0} : std::move(weights))
        , output_value_{}
    {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            std::string port_name;
            if (index == 0) {
                port_name = port_names::lhs;
            } else if (index == 1) {
                port_name = port_names::rhs;
            } else {
                port_name = "in" + std::to_string(index);
            }
            register_input(index, port_name, signal_type_for<T>(), nullptr);
        }
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override
    {
        T weighted_sum{};
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            weighted_sum += read_input<T>(index) * weights_[index];
        }
        output_value_ = weighted_sum;
    }

private:
    std::vector<RealValue> weights_;
    T output_value_;
};

/// Multiplier: product of N inputs with optional per-input factors, 1 output of type T
template <typename T>
class MultiplyT final : public Element {
public:
    explicit MultiplyT(std::vector<RealValue> factors = {1.0, 1.0})
        : Element("Multiply")
        , factors_(factors.empty() ? std::vector<RealValue>{1.0, 1.0} : std::move(factors))
        , output_value_{}
    {
        for (std::size_t index = 0; index < factors_.size(); ++index) {
            std::string port_name;
            if (index == 0) {
                port_name = port_names::lhs;
            } else if (index == 1) {
                port_name = port_names::rhs;
            } else {
                port_name = "in" + std::to_string(index);
            }
            register_input(index, port_name, signal_type_for<T>(), nullptr);
        }
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    void step() override
    {
        T product = static_cast<T>(1.0);
        for (std::size_t index = 0; index < factors_.size(); ++index) {
            product *= read_input<T>(index) * factors_[index];
        }
        output_value_ = product;
    }

private:
    std::vector<RealValue> factors_;
    T output_value_;
};

/// Delay (z^-1): 1 input, 1 output of type T, causal memory (no direct feedthrough)
template <typename T>
class DelayT final : public Element {
public:
    explicit DelayT(T initial_value = T{})
        : Element("Delay", true, Feedthrough::None)
        , output_value_(initial_value)
    {
        register_input(0, port_names::in, signal_type_for<T>(), nullptr);
        register_output(0, port_names::out, signal_type_for<T>(), &output_value_);
    }

    // Writes the next step's output; NodeRuntime publishes it after the step.
    void step() override
    {
        output_value_ = read_input<T>(0);
    }

private:
    T output_value_;
};

class Integrator final : public Element {
public:
    explicit Integrator(RealValue initial_value = 0.0);
    void step() override;

private:
    RealValue output_value_;
};

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

class FirFilter final : public Element {
public:
    explicit FirFilter(std::vector<RealValue> taps);
    void step() override;

private:
    std::vector<RealValue> taps_;
    std::vector<RealValue> history_;
    RealValue output_value_;
};

class IirFilter final : public Element {
public:
    IirFilter(RealValue b0, RealValue b1, RealValue a1);
    void step() override;

private:
    RealValue b0_, b1_, a1_;
    RealValue x1_{0.0}, y1_{0.0};
    RealValue output_value_;
};

class PreviousValue final : public Element {
public:
    explicit PreviousValue(RealValue initial_value = 0.0);
    void step() override;

private:
    RealValue output_value_;
};

class ReImToComplex final : public Element {
public:
    ReImToComplex();
    void step() override;

private:
    ComplexValue output_value_;
};

class ComplexToReIm final : public Element {
public:
    ComplexToReIm();
    void step() override;

private:
    RealValue output_re_;
    RealValue output_im_;
};

class ComplexToMagnitudePhase final : public Element {
public:
    ComplexToMagnitudePhase();
    void step() override;
    void bind_inputs(std::span<const InputBinding> bindings) override;

private:
    RealValue output_magnitude_;
    RealValue output_phase_;
};

class Int12ToDouble final : public Element {
public:
    Int12ToDouble();
    void step() override;

private:
    RealValue output_value_;
};

class DoubleToInt12 final : public Element {
public:
    DoubleToInt12();
    void step() override;

private:
    RealValue output_value_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENTS_HPP
