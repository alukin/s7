#ifndef S7CORE_INCLUDE_S7_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ELEMENTS_HPP

#include "s7/element.hpp"
#include "s7/signal_values.hpp"

#include <cmath>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace s7 {

// ============================================================================
// Generic elements: one template per element, instantiated per SignalType
// (ELEMENT_DEV_GUIDE.md, "Type system"). Integer arithmetic applies the
// element's Overflow rule; floating-point arithmetic is IEEE 754.
// ============================================================================

/// Constant source: 0 inputs, 1 output of type Type. Any type.
template <SignalType Type>
class ConstantT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    explicit ConstantT(value_type value = {})
        : Element("Constant")
        , output_value_(value)
    {
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override {}

private:
    value_type output_value_;
};

/// Gain: out = factor * in. Arithmetic types.
template <SignalType Type>
class GainT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using Ops = SignalOps<Type>;

    explicit GainT(value_type factor = Ops::one(), Overflow overflow = Overflow::Saturate)
        : Element("Gain")
        , factor_(factor)
        , overflow_(overflow)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        output_value_ = Ops::multiply(read_input<value_type>(0), factor_, overflow_);
    }

private:
    value_type factor_;
    Overflow overflow_;
    value_type output_value_{};
};

namespace detail {

[[nodiscard]] inline std::string sum_port_name(const std::size_t index)
{
    if (index == 0) {
        return std::string(port_names::lhs);
    }
    if (index == 1) {
        return std::string(port_names::rhs);
    }
    return "in" + std::to_string(index);
}

} // namespace detail

/// Adder: weighted sum of N inputs (N = weights.size(), at least 2). Arithmetic types.
template <SignalType Type>
class AdderT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using Ops = SignalOps<Type>;

    explicit AdderT(std::vector<value_type> weights = {Ops::one(), Ops::one()},
                    Overflow overflow = Overflow::Saturate)
        : Element("Adder")
        , weights_(weights.empty() ? std::vector<value_type>{Ops::one(), Ops::one()} : std::move(weights))
        , overflow_(overflow)
    {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            register_input(index, detail::sum_port_name(index), Type, nullptr);
        }
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        value_type sum = Ops::zero();
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            sum = Ops::add(sum, Ops::multiply(read_input<value_type>(index), weights_[index], overflow_), overflow_);
        }
        output_value_ = sum;
    }

private:
    std::vector<value_type> weights_;
    Overflow overflow_;
    value_type output_value_{};
};

/// Multiply: product of N inputs, each scaled by its factor. Arithmetic types.
template <SignalType Type>
class MultiplyT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using Ops = SignalOps<Type>;

    explicit MultiplyT(std::vector<value_type> factors = {Ops::one(), Ops::one()},
                       Overflow overflow = Overflow::Saturate)
        : Element("Multiply")
        , factors_(factors.empty() ? std::vector<value_type>{Ops::one(), Ops::one()} : std::move(factors))
        , overflow_(overflow)
    {
        for (std::size_t index = 0; index < factors_.size(); ++index) {
            register_input(index, detail::sum_port_name(index), Type, nullptr);
        }
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        value_type product = Ops::one();
        for (std::size_t index = 0; index < factors_.size(); ++index) {
            product = Ops::multiply(product, Ops::multiply(read_input<value_type>(index), factors_[index], overflow_),
                                    overflow_);
        }
        output_value_ = product;
    }

private:
    std::vector<value_type> factors_;
    Overflow overflow_;
    value_type output_value_{};
};

/// Unit delay (z^-1), causal memory. Any type. "Delay" and "PreviousValue"
/// are the same element under two names.
template <SignalType Type>
class DelayT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    explicit DelayT(value_type initial_value = {}, std::string_view type_name = "Delay")
        : Element(type_name, true, Feedthrough::None)
        , output_value_(initial_value)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    // Writes the next step's output; NodeRuntime publishes it after the step.
    void step() override
    {
        output_value_ = read_input<value_type>(0);
    }

private:
    value_type output_value_;
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

using Constant = ConstantT<SignalType::F64>;
using ComplexConstant = ConstantT<SignalType::C64>;

using Gain = GainT<SignalType::F64>;
using ComplexGain = GainT<SignalType::C64>;

using Adder = AdderT<SignalType::F64>;
using ComplexAdder = AdderT<SignalType::C64>;

using Multiply = MultiplyT<SignalType::F64>;
using ComplexMultiply = MultiplyT<SignalType::C64>;

using Delay = DelayT<SignalType::F64>;
using ComplexDelay = DelayT<SignalType::C64>;

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

/// PreviousValue: the F64 unit delay under its own element name.
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
    signal_storage_t<SignalType::I12> output_value_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENTS_HPP
