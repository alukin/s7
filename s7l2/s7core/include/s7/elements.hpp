// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ELEMENTS_HPP

#include "s7/element.hpp"
#include "s7/sources.hpp"
#include "s7/control_elements.hpp"
#include "s7/trapezoid.hpp"
#include "s7/coupled_integration.hpp"
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

/// Divider: out = numerator / denominator. Real floating-point types.
/// A zero denominator is an execution error rather than an infinite output.
template <SignalType Type>
class DividerT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    DividerT() : Element("Divider")
    {
        register_input(0, "numerator", Type, nullptr);
        register_input(1, "denominator", Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto numerator = read_input<value_type>(0);
        const auto denominator = read_input<value_type>(1);
        if (denominator == value_type{}) {
            throw std::domain_error("Divider denominator is zero.");
        }
        output_value_ = numerator / denominator;
    }

private:
    value_type output_value_{};
};

enum class MathFunctionOperation : std::uint8_t { Sqrt, Exp, Log, Pow, Abs, Reciprocal };

/// MathFunction: one common real floating-point function selected by a parameter.
template <SignalType Type>
class MathFunctionT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    explicit MathFunctionT(const MathFunctionOperation function, const value_type exponent = value_type{2})
        : Element("MathFunction"), function_(function), exponent_(exponent)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto input = read_input<value_type>(0);
        switch (function_) {
            case MathFunctionOperation::Sqrt:
                if (input < value_type{}) {
                    throw std::domain_error("MathFunction sqrt input is negative.");
                }
                output_value_ = math::sqrt(input);
                break;
            case MathFunctionOperation::Exp: output_value_ = math::exp(input); break;
            case MathFunctionOperation::Log:
                if (input <= value_type{}) {
                    throw std::domain_error("MathFunction log input must be positive.");
                }
                output_value_ = math::log(input);
                break;
            case MathFunctionOperation::Pow: output_value_ = math::pow(input, exponent_); break;
            case MathFunctionOperation::Abs: output_value_ = math::abs(input); break;
            case MathFunctionOperation::Reciprocal:
                if (input == value_type{}) {
                    throw std::domain_error("MathFunction reciprocal input is zero.");
                }
                output_value_ = value_type{1} / input;
                break;
        }
    }

private:
    MathFunctionOperation function_;
    value_type exponent_;
    value_type output_value_{};
};

/// Power: out = base ^ exponent. Real floating-point types.
template <SignalType Type>
class PowerT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    PowerT() : Element("Power")
    {
        register_input(0, "base", Type, nullptr);
        register_input(1, "exponent", Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override { output_value_ = math::pow(read_input<value_type>(0), read_input<value_type>(1)); }

private:
    value_type output_value_{};
};

/// Saturation: clamp a real numeric input to inclusive lower and upper limits.
template <SignalType Type>
class SaturationT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    SaturationT(const value_type lower, const value_type upper)
        : Element("Saturation"), lower_(lower), upper_(upper)
    {
        if (!(lower_ <= upper_)) {
            throw std::invalid_argument("Saturation upper_limit must be at least lower_limit.");
        }
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto input = read_input<value_type>(0);
        output_value_ = input < lower_ ? lower_ : (upper_ < input ? upper_ : input);
    }

private:
    value_type lower_;
    value_type upper_;
    value_type output_value_{};
};

/// MinMax: minimum or maximum of two or more real numeric inputs.
template <SignalType Type>
class MinMaxT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    MinMaxT(const std::size_t input_count, const bool maximum)
        : Element("MinMax"), maximum_(maximum)
    {
        if (input_count < 2 || input_count > 64) {
            throw std::invalid_argument("MinMax input_count must be between 2 and 64.");
        }
        for (std::size_t index = 0; index < input_count; ++index) {
            register_input(index, "in" + std::to_string(index), Type, nullptr);
        }
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        output_value_ = read_input<value_type>(0);
        for (std::size_t index = 1; index < input_ports().size(); ++index) {
            const auto value = read_input<value_type>(index);
            if constexpr (signal_type_info(Type).kind == SignalKind::Float) {
                // NaN is ignored while another input is valid; it propagates
                // only if every input seen so far has been NaN.
                if (math::isnan(value)) {
                    continue;
                }
                if (math::isnan(output_value_)) {
                    output_value_ = value;
                    continue;
                }
            }
            if ((maximum_ && output_value_ < value) || (!maximum_ && value < output_value_)) {
                output_value_ = value;
            }
        }
    }

private:
    bool maximum_;
    value_type output_value_{};
};

/// Sign: -1, 0 or 1 for real values (unsigned values produce 0 or 1).
template <SignalType Type>
class SignT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    SignT() : Element("Sign")
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto input = read_input<value_type>(0);
        if constexpr (signal_type_info(Type).kind == SignalKind::Float) {
            if (math::isnan(input)) {
                output_value_ = input;
                return;
            }
        }
        if (value_type{} < input) {
            output_value_ = value_type{1};
        } else if constexpr (signal_type_info(Type).kind == SignalKind::SignedInteger ||
                             signal_type_info(Type).kind == SignalKind::Float) {
            output_value_ = input < value_type{} ? value_type{-1} : value_type{};
        } else {
            output_value_ = value_type{};
        }
    }

private:
    value_type output_value_{};
};

enum class ComparatorOperation : std::uint8_t { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };

/// Comparator: out = 1 if lhs `operation` rhs holds, else 0, in the input
/// type. Real numeric types. Floating-point comparison follows IEEE 754: a
/// comparison against NaN is false for lt/le/gt/ge and for eq, true for ne.
template <SignalType Type>
class ComparatorT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    explicit ComparatorT(ComparatorOperation operation = ComparatorOperation::Less)
        : Element("Comparator"), operation_(operation)
    {
        register_input(0, port_names::lhs, Type, nullptr);
        register_input(1, port_names::rhs, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto lhs = read_input<value_type>(0);
        const auto rhs = read_input<value_type>(1);
        bool result = false;
        switch (operation_) {
            case ComparatorOperation::Less: result = lhs < rhs; break;
            case ComparatorOperation::LessEqual: result = lhs <= rhs; break;
            case ComparatorOperation::Greater: result = lhs > rhs; break;
            case ComparatorOperation::GreaterEqual: result = lhs >= rhs; break;
            case ComparatorOperation::Equal: result = lhs == rhs; break;
            case ComparatorOperation::NotEqual: result = lhs != rhs; break;
        }
        output_value_ = result ? value_type{1} : value_type{};
    }

private:
    ComparatorOperation operation_;
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

/// Adder: weighted sum of N inputs (N = weights.size(), 2 to 64), computed
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
        , weights_(std::move(weights))
        , overflow_(overflow)
    {
        if (weights_.size() < 2 || weights_.size() > 64) {
            throw std::invalid_argument("Adder weights must define between 2 and 64 inputs.");
        }
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

/// Multiply: product of 2 to 64 inputs, each scaled by its factor, computed in the
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
        , factors_(std::move(factors))
        , overflow_(overflow)
    {
        if (factors_.size() < 2 || factors_.size() > 64) {
            throw std::invalid_argument("Multiply factors must define between 2 and 64 inputs.");
        }
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
class EulerIntegratorT final : public Element, public CoupledIntegration {
public:
    using value_type = signal_storage_t<Type>;
    using step_type = component_storage_t<Type>;

    // Without dt the integrator follows its domain's rate (adopt_rate).
    explicit EulerIntegratorT(std::optional<step_type> dt = step_type(1), value_type initial_value = {},
                              int substeps = 1)
        : Element("EulerIntegrator", true, Feedthrough::None)
        , follows_rate_(!dt)
        , dt_(dt ? *dt : std::numeric_limits<step_type>::quiet_NaN())
        , output_value_(initial_value)
        , substeps_(substeps)
    {
        if (substeps_ < 1 || substeps_ > 32)
            throw std::invalid_argument("EulerIntegrator substeps must be between 1 and 32.");
        if constexpr (Type != SignalType::F64) {
            if (substeps_ != 1)
                throw std::invalid_argument("Coupled Euler substeps require F64 integrator states.");
        }
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    // output_value_ holds y[n] here; NodeRuntime publishes y[n+1] at the next step.
    void step() override
    {
        if (substeps_ != 1)
            throw std::logic_error("EulerIntegrator substeps require NodeRuntime's coupled numerical region.");
        output_value_ = output_value_ + read_input<value_type>(0) * dt_;
    }

    [[nodiscard]] std::optional<Rate> declared_rate() const override
    {
        if (follows_rate_) return std::nullopt;
        return Rate::from_seconds(static_cast<double>(dt_));
    }
    void adopt_rate(const Rate& rate) override
    {
        if (follows_rate_) dt_ = detail::rate_period<step_type>(rate);
    }
    [[nodiscard]] CoupledMethod coupled_method() const override
    {
        return substeps_ == 1 ? CoupledMethod::None : CoupledMethod::EulerSubsteps;
    }
    [[nodiscard]] double coupled_step_seconds() const override { return static_cast<double>(dt_); }
    [[nodiscard]] int coupled_substeps() const override { return substeps_; }

private:
    bool follows_rate_;
    step_type dt_;
    value_type output_value_;
    int substeps_;
};

// Fourth-order, two-stage implicit Gauss-Legendre state. NodeRuntime solves
// all coupled F64 states in the compiled region simultaneously.
class GaussLegendreIntegrator final : public Element, public CoupledIntegration {
public:
    explicit GaussLegendreIntegrator(std::optional<double> dt = 1.0, double initial_value = 0.0)
        : Element("GaussLegendreIntegrator", true, Feedthrough::None), follows_rate_(!dt)
        , dt_(dt ? *dt : std::numeric_limits<double>::quiet_NaN()), output_value_(initial_value)
    {
        if (dt && (!std::isfinite(dt_) || dt_ <= 0))
            throw std::invalid_argument("GaussLegendreIntegrator dt must be finite and positive.");
        if (!std::isfinite(output_value_))
            throw std::invalid_argument("GaussLegendreIntegrator initial_value must be finite.");
        register_input(0, port_names::in, SignalType::F64, nullptr);
        register_output(0, port_names::out, SignalType::F64, &output_value_);
    }

    void step() override
    {
        throw std::logic_error("GaussLegendreIntegrator requires NodeRuntime's coupled numerical region.");
    }
    [[nodiscard]] CoupledMethod coupled_method() const override { return CoupledMethod::GaussLegendre2; }
    [[nodiscard]] double coupled_step_seconds() const override { return dt_; }
    [[nodiscard]] int coupled_substeps() const override { return 1; }
    [[nodiscard]] std::optional<Rate> declared_rate() const override
    {
        if (follows_rate_) return std::nullopt;
        return Rate::from_seconds(dt_);
    }
    void adopt_rate(const Rate& rate) override
    {
        if (follows_rate_) dt_ = detail::rate_period<double>(rate);
    }

private:
    bool follows_rate_;
    double dt_;
    double output_value_;
};

/// Implicit trapezoidal integration: y[n+1] = y[n] + dt/2 *
/// (f(y[n]) + f(y[n+1])). NodeRuntime solves coupled states together;
/// outputs remain causal memory and need no artificial feedback Delay.
/// In sampled mode this instead integrates supplied samples with direct
/// feedthrough; its first call only records the initial input sample.
/// Floating-point and complex floating-point types.
template <SignalType Type>
class TrapezoidIntegratorT final : public Element, public TrapezoidIntegration {
public:
    using value_type = signal_storage_t<Type>;
    using step_type = component_storage_t<Type>;

    // Without dt the integrator follows its domain's rate (adopt_rate).
    explicit TrapezoidIntegratorT(std::optional<step_type> dt = step_type(1), value_type initial_value = {},
                                 TrapezoidMode mode = TrapezoidMode::Implicit)
        : Element("TrapezoidIntegrator", true, mode == TrapezoidMode::Sampled ? Feedthrough::Direct : Feedthrough::None)
        , follows_rate_(!dt)
        , half_dt_(dt ? *dt / step_type(2) : std::numeric_limits<step_type>::quiet_NaN())
        , output_value_(initial_value)
        , sampled_(mode == TrapezoidMode::Sampled)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        if (!sampled_) {
            throw std::logic_error("Implicit TrapezoidIntegrator requires NodeRuntime's coupled numerical stage.");
        }
        const auto x = read_input<value_type>(0);
        if (has_previous_) {
            output_value_ = output_value_ + (x + previous_input_) * half_dt_;
        }
        previous_input_ = x;
        has_previous_ = true;
    }

    [[nodiscard]] bool implicit_trapezoid_enabled() const override { return !sampled_; }

    void prepare_trapezoid_step() override
    {
        base_value_ = output_value_;
        base_input_ = read_input<value_type>(0);
        candidate_ = base_value_ + base_input_ * (half_dt_ * step_type(2));
    }

    [[nodiscard]] bool prepare_trapezoid_correction() override
    {
        candidate_ = base_value_ + (base_input_ + read_input<value_type>(0)) * half_dt_;
        const auto magnitude = math::abs(candidate_);
        const auto difference = math::abs(candidate_ - output_value_);
        if (math::isnan(magnitude) || math::isinf(magnitude) || math::isnan(difference) || math::isinf(difference)) {
            throw std::runtime_error("TrapezoidIntegrator produced a non-finite candidate.");
        }
        // Do not reduce F128 or complex components to double for convergence.
        constexpr auto component = component_signal_type(Type);
        constexpr int digits = component == SignalType::F16 ? 11 : component == SignalType::F32 ? 24 :
                               component == SignalType::F64 ? 53 : 113;
        const step_type tolerance = math::ldexp(step_type(1), 4 - digits); // 8 * machine epsilon
        return difference <= tolerance * (step_type(1) + magnitude);
    }

    void publish_trapezoid_candidate() override { output_value_ = candidate_; }

    [[nodiscard]] std::optional<Rate> declared_rate() const override
    {
        if (follows_rate_) return std::nullopt;
        return Rate::from_seconds(static_cast<double>(half_dt_) * 2.0);
    }
    void adopt_rate(const Rate& rate) override
    {
        if (follows_rate_) half_dt_ = detail::rate_period<step_type>(rate) / step_type(2);
    }

private:
    bool follows_rate_;
    step_type half_dt_;
    value_type output_value_;
    bool sampled_;
    value_type base_value_{};
    value_type base_input_{};
    value_type candidate_{};
    value_type previous_input_{};
    bool has_previous_{false};
};

/// SymplecticIntegrator (semi-implicit Euler, a.k.a. Euler-Cromer): for a
/// second-order system x'' = a(x), updates velocity from the current
/// (not yet advanced) state, then position from the freshly updated
/// velocity:
///   v[n+1] = v[n] + dt*a[n]
///   x[n+1] = x[n] + dt*v[n+1]
/// Unlike EulerIntegrator applied independently to position and velocity,
/// this bounds the energy of a conservative oscillator instead of letting
/// it drift secularly (docs/OPEN_PROBLEMS.md). The bound holds when
/// acceleration depends only on position (and time); an acceleration that
/// also depends on velocity (damping) loses the symplectic property. Still
/// only first-order accurate, not an exact conservation law.
/// Causal memory (outputs hold [n] during the step; NodeRuntime publishes
/// [n+1] after). Floating-point and complex floating-point types.
template <SignalType Type>
class SymplecticIntegratorT final : public Element {
public:
    using value_type = signal_storage_t<Type>;
    using step_type = component_storage_t<Type>;

    // Without dt the integrator follows its domain's rate (adopt_rate).
    explicit SymplecticIntegratorT(std::optional<step_type> dt = step_type(1), value_type initial_position = {},
                                   value_type initial_velocity = {})
        : Element("SymplecticIntegrator", true, Feedthrough::None)
        , follows_rate_(!dt)
        , dt_(dt ? *dt : std::numeric_limits<step_type>::quiet_NaN())
        , position_(initial_position)
        , velocity_(initial_velocity)
    {
        register_input(0, "acceleration", Type, nullptr);
        register_output(0, "position", Type, &position_);
        register_output(1, "velocity", Type, &velocity_);
    }

    // Writes the next step's outputs; NodeRuntime publishes them after the step.
    void step() override
    {
        velocity_ = velocity_ + read_input<value_type>(0) * dt_;
        position_ = position_ + velocity_ * dt_;
    }

    [[nodiscard]] std::optional<Rate> declared_rate() const override
    {
        if (follows_rate_) return std::nullopt;
        return Rate::from_seconds(static_cast<double>(dt_));
    }
    void adopt_rate(const Rate& rate) override
    {
        if (follows_rate_) dt_ = detail::rate_period<step_type>(rate);
    }

private:
    bool follows_rate_;
    step_type dt_;
    value_type position_;
    value_type velocity_;
};

/// The rate a rate-designed element's coefficients were written for
/// (doc/S7_Sample_Rate_Model.md section 8.2). Raw coefficients keep no design
/// intent (cutoff, prototype), so they cannot be recalculated for another
/// rate: adopting a different rate is refused. Without a recorded design rate
/// the coefficients are used as given, as before.
class DesignRate {
public:
    explicit DesignRate(const std::optional<double> hz)
    {
        if (hz) rate_ = Rate::from_hz(*hz);
    }
    void check(const Rate& rate, const std::string_view coefficients) const
    {
        if (rate_ && *rate_ != rate) {
            throw std::invalid_argument(std::string(coefficients) + " were designed for " + rate_->to_string() +
                                        " and cannot be recalculated for " + rate.to_string() +
                                        "; redesign them for this rate, or add a Resampler.");
        }
    }

private:
    std::optional<Rate> rate_;
};

/// FIR filter: y[n] = sum_k taps[k] * x[n-k], computed in the accumulator type
/// Acc (default: the input type), as a hardware MAC with a wide accumulator.
/// Rate-designed: an optional design rate is checked against the rate it runs at.
/// Arithmetic types.
template <SignalType Type, SignalType Acc = Type>
class FirFilterT final : public Element {
public:
    using input_type = signal_storage_t<Type>;
    using value_type = signal_storage_t<Acc>;
    using Ops = SignalOps<Acc>;

    explicit FirFilterT(std::vector<value_type> taps, Overflow overflow = Overflow::Saturate,
                        std::optional<double> design_rate_hz = std::nullopt)
        : Element("FirFilter", true)
        , taps_(std::move(taps))
        , history_(taps_.size(), value_type{})
        , overflow_(overflow)
        , design_rate_(design_rate_hz)
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

    void adopt_rate(const Rate& rate) override { design_rate_.check(rate, "FirFilter taps"); }

private:
    std::vector<value_type> taps_;
    std::vector<value_type> history_;
    Overflow overflow_;
    DesignRate design_rate_;
    value_type output_value_{};
};

/// First-order IIR filter: y[n] = b0 x[n] + b1 x[n-1] - a1 y[n-1].
/// Rate-designed: an optional design rate is checked against the rate it runs at.
/// Floating-point and complex floating-point types.
template <SignalType Type>
class IirFilterT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    IirFilterT(value_type b0, value_type b1, value_type a1, std::optional<double> design_rate_hz = std::nullopt)
        : Element("IirFilter", true)
        , b0_(b0), b1_(b1), a1_(a1), design_rate_(design_rate_hz)
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

    void adopt_rate(const Rate& rate) override { design_rate_.check(rate, "IirFilter coefficients"); }

private:
    value_type b0_, b1_, a1_;
    DesignRate design_rate_;
    value_type x1_{}, y1_{};
    value_type output_value_{};
};

/// Trailing-edge PWM. Duty is sampled at each carrier boundary and held for
/// the whole period. Output is numeric 0/1, suitable for a switched plant.
template <SignalType Type>
class PwmT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    // period: carrier period in seconds, 2..65504 whole samples. Without a
    // sample rate the PWM follows its domain's rate (adopt_rate).
    PwmT(double period, std::optional<double> sample_rate_hz)
        : Element("PWM", true), period_(period), follows_rate_(!sample_rate_hz)
    {
        if (sample_rate_hz) {
            if (!std::isfinite(*sample_rate_hz) || *sample_rate_hz <= 0) {
                throw std::invalid_argument("PWM requires a finite positive sample_rate_hz.");
            }
            rate_ = Rate::from_hz(*sample_rate_hz);
            configure();
        }
        register_input(0, "duty", Type, nullptr);
        register_output(0, "gate", Type, &output_value_);
    }

    void step() override
    {
        if (!rate_) throw std::logic_error("PWM follows its domain's rate and has not adopted one.");
        if (sample_ == 0) {
            duty_ = read_input<value_type>(0);
            if (duty_ - duty_ != value_type(0)) throw std::domain_error("PWM duty must be finite.");
            duty_ = duty_ < value_type(0) ? value_type(0) : (duty_ > value_type(1) ? value_type(1) : duty_);
        }
        output_value_ = value_type(sample_) / value_type(period_samples_) < duty_ ? value_type(1) : value_type(0);
        if (++sample_ == period_samples_) sample_ = 0;
    }

    [[nodiscard]] std::optional<Rate> declared_rate() const override
    {
        if (follows_rate_) return std::nullopt;
        return rate_;
    }
    void adopt_rate(const Rate& rate) override
    {
        if (!follows_rate_) return;
        rate_ = rate;
        configure();
    }

private:
    void configure()
    {
        period_samples_ = detail::duration_samples(*rate_, period_, "period");
        if (period_samples_ < 2 || period_samples_ > 65504) {
            throw std::invalid_argument("PWM period must be 2..65504 samples; it is " + std::to_string(period_samples_) + ".");
        }
    }
    double period_;
    bool follows_rate_;
    std::optional<Rate> rate_;
    std::size_t period_samples_{}, sample_{};
    value_type duty_{}, output_value_{};
};

/// Sine source: amplitude * sin(phase), phase advancing by
/// 2 pi frequency / sample_rate per step, computed in the element's type.
/// The sample rate is kept as a double, like the other sources; F16 computes
/// the increment in float because common rates (44100 Hz) are not F16 values.
/// Floating-point types.
template <SignalType Type>
class AnalogOscillatorT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    // Without a sample rate the oscillator follows its domain's rate (adopt_rate).
    AnalogOscillatorT(value_type amplitude, value_type frequency_hz, std::optional<double> sample_rate_hz,
                      value_type phase = {})
        : Element("AnalogOscillator", true)
        , amplitude_(amplitude)
        , frequency_hz_(frequency_hz)
        , phase_(phase)
        , follows_rate_(!sample_rate_hz)
        , sample_rate_hz_(sample_rate_hz ? *sample_rate_hz : 0.0)
    {
        if (sample_rate_hz) {
            if (!(*sample_rate_hz > 0.0) || !std::isfinite(*sample_rate_hz)) {
                throw std::invalid_argument("AnalogOscillator requires a positive, finite sample rate.");
            }
            configure();
        } else {
            phase_increment_ = std::numeric_limits<value_type>::quiet_NaN();
        }
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

    [[nodiscard]] std::optional<Rate> declared_rate() const override
    {
        if (follows_rate_) return std::nullopt;
        return Rate::from_hz(sample_rate_hz_);
    }
    void adopt_rate(const Rate& rate) override
    {
        if (!follows_rate_) return;
        sample_rate_hz_ = rate.to_hz();
        configure();
    }

private:
    void configure()
    {
        using Increment = std::conditional_t<(sizeof(value_type) < sizeof(float)), float, value_type>;
        phase_increment_ = static_cast<value_type>(Increment(2) * math::pi_v<Increment> * static_cast<Increment>(frequency_hz_)
                                                   / static_cast<Increment>(sample_rate_hz_));
    }
    value_type amplitude_;
    value_type frequency_hz_;
    value_type phase_;
    bool follows_rate_;
    double sample_rate_hz_;
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

/// Trig's "function" parameter. "T", "f(T)" in element docs/messages below.
enum class TrigFunction : std::uint8_t { Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh };

// "sin", "cos", ...; throws std::invalid_argument otherwise.
[[nodiscard]] TrigFunction trig_function_from_name(std::string_view name);

/// Trig: y = f(x), f selected by the "function" parameter (sin, cos, tan,
/// asin, acos, atan, sinh, cosh, tanh), computed in the element's type.
/// Real floating-point types: s7::math has no complex-analytic version of
/// these yet (s7/float_types.hpp).
template <SignalType Type>
class TrigT final : public Element {
public:
    using value_type = signal_storage_t<Type>;

    explicit TrigT(TrigFunction function = TrigFunction::Sin) : Element("Trig"), function_(function)
    {
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        const auto x = read_input<value_type>(0);
        switch (function_) {
            case TrigFunction::Sin: output_value_ = math::sin(x); break;
            case TrigFunction::Cos: output_value_ = math::cos(x); break;
            case TrigFunction::Tan: output_value_ = math::tan(x); break;
            case TrigFunction::Asin: output_value_ = math::asin(x); break;
            case TrigFunction::Acos: output_value_ = math::acos(x); break;
            case TrigFunction::Atan: output_value_ = math::atan(x); break;
            case TrigFunction::Sinh: output_value_ = math::sinh(x); break;
            case TrigFunction::Cosh: output_value_ = math::cosh(x); break;
            case TrigFunction::Tanh: output_value_ = math::tanh(x); break;
        }
    }

private:
    TrigFunction function_;
    value_type output_value_{};
};

using Accumulator = AccumulatorT<SignalType::F64>;
using Divider = DividerT<SignalType::F64>;
using MathFunction = MathFunctionT<SignalType::F64>;
using Power = PowerT<SignalType::F64>;
using Saturation = SaturationT<SignalType::F64>;
using MinMax = MinMaxT<SignalType::F64>;
using Sign = SignT<SignalType::F64>;
using Comparator = ComparatorT<SignalType::F64>;
using EulerIntegrator = EulerIntegratorT<SignalType::F64>;
using TrapezoidIntegrator = TrapezoidIntegratorT<SignalType::F64>;
using SymplecticIntegrator = SymplecticIntegratorT<SignalType::F64>;
using FirFilter = FirFilterT<SignalType::F64>;
using IirFilter = IirFilterT<SignalType::F64>;
using PWM = PwmT<SignalType::F64>;
using AnalogOscillator = AnalogOscillatorT<SignalType::F64>;
using ReImToComplex = ReImToComplexT<SignalType::C64>;
using ComplexToReIm = ComplexToReImT<SignalType::C64>;
using ComplexToMagnitudePhase = ComplexToMagnitudePhaseT<SignalType::C64>;
using Trig = TrigT<SignalType::F64>;

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
