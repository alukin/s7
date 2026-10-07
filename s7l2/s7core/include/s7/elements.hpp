#ifndef S7CORE_INCLUDE_S7_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ELEMENTS_HPP

#include "s7/element.hpp"
#include "s7/convert.hpp"
#include "s7/signal_values.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
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

// Exact widening of a value into an accumulator type (see widens_exactly()).
template <SignalType From, SignalType To>
[[nodiscard]] constexpr signal_storage_t<To> widen(const signal_storage_t<From>& value)
{
    static_assert(widens_exactly(From, To), "accumulator type must hold every input value exactly");
    if constexpr (From == To) {
        return value;
    } else {
        return convert_value<From, To>(value, ConvertOptions{});
    }
}

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

/// Adder: weighted sum of N inputs (N = weights.size(), at least 2), computed
/// in the accumulator type Acc (default: the input type). Arithmetic types.
template <SignalType Type, SignalType Acc = Type>
class AdderT final : public Element {
public:
    using input_type = signal_storage_t<Type>;
    using value_type = signal_storage_t<Acc>;
    using Ops = SignalOps<Acc>;

    explicit AdderT(std::vector<value_type> weights = {Ops::one(), Ops::one()},
                    Overflow overflow = Overflow::Saturate)
        : Element("Adder")
        , weights_(weights.empty() ? std::vector<value_type>{Ops::one(), Ops::one()} : std::move(weights))
        , overflow_(overflow)
    {
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            register_input(index, detail::sum_port_name(index), Type, nullptr);
        }
        register_output(0, port_names::out, Acc, &output_value_);
    }

    void step() override
    {
        value_type sum = Ops::zero();
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            const auto x = widen<Type, Acc>(read_input<input_type>(index));
            sum = Ops::add(sum, Ops::multiply(x, weights_[index], overflow_), overflow_);
        }
        output_value_ = sum;
    }

private:
    std::vector<value_type> weights_;
    Overflow overflow_;
    value_type output_value_{};
};

/// Multiply: product of N inputs, each scaled by its factor, computed in the
/// accumulator type Acc (default: the input type). Arithmetic types.
template <SignalType Type, SignalType Acc = Type>
class MultiplyT final : public Element {
public:
    using input_type = signal_storage_t<Type>;
    using value_type = signal_storage_t<Acc>;
    using Ops = SignalOps<Acc>;

    explicit MultiplyT(std::vector<value_type> factors = {Ops::one(), Ops::one()},
                       Overflow overflow = Overflow::Saturate)
        : Element("Multiply")
        , factors_(factors.empty() ? std::vector<value_type>{Ops::one(), Ops::one()} : std::move(factors))
        , overflow_(overflow)
    {
        for (std::size_t index = 0; index < factors_.size(); ++index) {
            register_input(index, detail::sum_port_name(index), Type, nullptr);
        }
        register_output(0, port_names::out, Acc, &output_value_);
    }

    void step() override
    {
        value_type product = Ops::one();
        for (std::size_t index = 0; index < factors_.size(); ++index) {
            const auto x = widen<Type, Acc>(read_input<input_type>(index));
            product = Ops::multiply(product, Ops::multiply(x, factors_[index], overflow_), overflow_);
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

// Real component type of a type: the type itself for real types, the
// component of a complex type (C64 -> double).
template <SignalType Type>
using component_storage_t = signal_storage_t<component_signal_type(Type)>;

/// Accumulator: running sum y[n] = y[n-1] + x[n], without a time step (an
/// accumulator), in the accumulator type Acc (default: the input type).
/// Arithmetic types. For numerical integration over time use
/// EulerIntegrator or TrapezoidIntegrator.
template <SignalType Type, SignalType Acc = Type>
class AccumulatorT final : public Element {
public:
    using input_type = signal_storage_t<Type>;
    using value_type = signal_storage_t<Acc>;
    using Ops = SignalOps<Acc>;

    explicit AccumulatorT(value_type initial_value = {}, Overflow overflow = Overflow::Saturate)
        : Element("Accumulator", true)
        , overflow_(overflow)
        , output_value_(initial_value)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Acc, &output_value_);
    }

    void step() override
    {
        output_value_ = Ops::add(output_value_, widen<Type, Acc>(read_input<input_type>(0)), overflow_);
    }

private:
    Overflow overflow_;
    value_type output_value_;
};

/// EulerIntegrator: forward Euler, rectangle rule: y[n+1] = y[n] + dt * x[n],
/// y[0] = initial_value. The output depends only on earlier inputs, so this is
/// causal memory: it can close a feedback loop (ODE models) without a Delay.
/// Floating-point and complex floating-point types.
template <SignalType Type>
class EulerIntegratorT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using step_type = component_storage_t<Type>;

    explicit EulerIntegratorT(step_type dt = step_type(1), value_type initial_value = {})
        : Element("EulerIntegrator", true, Feedthrough::None)
        , dt_(dt)
        , output_value_(initial_value)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    // output_value_ holds y[n] here; NodeRuntime publishes y[n+1] at the next step.
    void step() override
    {
        output_value_ = output_value_ + read_input<value_type>(0) * dt_;
    }

private:
    step_type dt_;
    value_type output_value_;
};

/// TrapezoidIntegrator: trapezoidal rule: y[n] = y[n-1] + dt/2 * (x[n] + x[n-1]),
/// y[0] = initial_value (the first step only records x[0]). Direct
/// feedthrough: a feedback loop through it still needs causal memory.
/// Floating-point and complex floating-point types.
template <SignalType Type>
class TrapezoidIntegratorT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using step_type = component_storage_t<Type>;

    explicit TrapezoidIntegratorT(step_type dt = step_type(1), value_type initial_value = {})
        : Element("TrapezoidIntegrator", true)
        , half_dt_(dt / step_type(2))
        , output_value_(initial_value)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto x = read_input<value_type>(0);
        if (has_previous_) {
            output_value_ = output_value_ + (x + previous_input_) * half_dt_;
        }
        previous_input_ = x;
        has_previous_ = true;
    }

private:
    step_type half_dt_;
    value_type output_value_;
    value_type previous_input_{};
    bool has_previous_{false};
};

/// FIR filter: y[n] = sum_k taps[k] * x[n-k], computed in the accumulator type
/// Acc (default: the input type), as a hardware MAC with a wide accumulator.
/// Arithmetic types.
template <SignalType Type, SignalType Acc = Type>
class FirFilterT final : public Element {
public:
    using input_type = signal_storage_t<Type>;
    using value_type = signal_storage_t<Acc>;
    using Ops = SignalOps<Acc>;

    explicit FirFilterT(std::vector<value_type> taps, Overflow overflow = Overflow::Saturate)
        : Element("FirFilter", true)
        , taps_(std::move(taps))
        , history_(taps_.size(), value_type{})
        , overflow_(overflow)
    {
        if (taps_.empty()) {
            throw std::invalid_argument("FirFilter requires at least one tap.");
        }
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Acc, &output_value_);
    }

    void step() override
    {
        for (std::size_t index = history_.size() - 1; index > 0; --index) {
            history_[index] = history_[index - 1];
        }
        history_[0] = widen<Type, Acc>(read_input<input_type>(0));
        value_type sum = Ops::zero();
        for (std::size_t index = 0; index < taps_.size(); ++index) {
            sum = Ops::add(sum, Ops::multiply(taps_[index], history_[index], overflow_), overflow_);
        }
        output_value_ = sum;
    }

private:
    std::vector<value_type> taps_;
    std::vector<value_type> history_;
    Overflow overflow_;
    value_type output_value_{};
};

/// First-order IIR filter: y[n] = b0 x[n] + b1 x[n-1] - a1 y[n-1].
/// Floating-point and complex floating-point types.
template <SignalType Type>
class IirFilterT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    IirFilterT(value_type b0, value_type b1, value_type a1)
        : Element("IirFilter", true)
        , b0_(b0), b1_(b1), a1_(a1)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto x = read_input<value_type>(0);
        const value_type y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x;
        y1_ = y;
        output_value_ = y;
    }

private:
    value_type b0_, b1_, a1_;
    value_type x1_{}, y1_{};
    value_type output_value_{};
};

/// Sine source: amplitude * sin(phase), phase advancing by
/// 2 pi frequency / sample_rate per step, computed in the element's type.
/// Floating-point types.
template <SignalType Type>
class AnalogOscillatorT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    AnalogOscillatorT(value_type amplitude, value_type frequency_hz, value_type sample_rate_hz, value_type phase = {})
        : Element("AnalogOscillator", true)
        , amplitude_(amplitude)
        , phase_(phase)
    {
        if (!(sample_rate_hz > value_type(0))) {
            throw std::invalid_argument("AnalogOscillator requires a positive sample rate.");
        }
        phase_increment_ = value_type(2) * math::pi_v<value_type> * frequency_hz / sample_rate_hz;
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        constexpr auto two_pi = value_type(2) * math::pi_v<value_type>;
        output_value_ = amplitude_ * math::sin(phase_);
        phase_ += phase_increment_;
        if (phase_ > two_pi || phase_ < -two_pi) {
            phase_ = math::fmod(phase_, two_pi);
        }
    }

private:
    value_type amplitude_;
    value_type phase_;
    value_type phase_increment_{};
    value_type output_value_{};
};

/// Joins real and imaginary parts: inputs component(T), output T. Complex types.
template <SignalType Type>
class ReImToComplexT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using part_type = component_storage_t<Type>;

    ReImToComplexT() : Element("ReImToComplex")
    {
        register_input(0, port_names::re, component_signal_type(Type), nullptr);
        register_input(1, port_names::im, component_signal_type(Type), nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        output_value_ = value_type{read_input<part_type>(0), read_input<part_type>(1)};
    }

private:
    value_type output_value_{};
};

/// Splits a complex value: input T, outputs component(T). Complex types.
template <SignalType Type>
class ComplexToReImT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using part_type = component_storage_t<Type>;

    ComplexToReImT() : Element("ComplexToReIm")
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::re, component_signal_type(Type), &re_);
        register_output(1, port_names::im, component_signal_type(Type), &im_);
    }

    void step() override
    {
        const auto value = read_input<value_type>(0);
        if constexpr (signal_type_info(Type).kind == SignalKind::ComplexFloat) {
            re_ = value.real();
            im_ = value.imag();
        } else {
            re_ = value.re;
            im_ = value.im;
        }
    }

private:
    part_type re_{};
    part_type im_{};
};

/// Magnitude and phase (radians) of a complex value, in its component type.
/// Complex floating-point types.
template <SignalType Type>
class ComplexToMagnitudePhaseT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using part_type = component_storage_t<Type>;

    ComplexToMagnitudePhaseT() : Element("ComplexToMagnitudePhase")
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, "magnitude", component_signal_type(Type), &magnitude_);
        register_output(1, "phase", component_signal_type(Type), &phase_);
    }

    void step() override
    {
        const auto value = read_input<value_type>(0);
        magnitude_ = math::abs(value);
        phase_ = math::arg(value);
    }

private:
    part_type magnitude_{};
    part_type phase_{};
};

using Accumulator = AccumulatorT<SignalType::F64>;
using EulerIntegrator = EulerIntegratorT<SignalType::F64>;
using TrapezoidIntegrator = TrapezoidIntegratorT<SignalType::F64>;
using FirFilter = FirFilterT<SignalType::F64>;
using IirFilter = IirFilterT<SignalType::F64>;
using AnalogOscillator = AnalogOscillatorT<SignalType::F64>;
using ReImToComplex = ReImToComplexT<SignalType::C64>;
using ComplexToReIm = ComplexToReImT<SignalType::C64>;
using ComplexToMagnitudePhase = ComplexToMagnitudePhaseT<SignalType::C64>;

/// PreviousValue: the F64 unit delay under its own element name.
class PreviousValue final : public Element {
public:
    explicit PreviousValue(RealValue initial_value = 0.0);
    void step() override;

private:
    RealValue output_value_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENTS_HPP
