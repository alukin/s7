// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_CONTROL_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_CONTROL_ELEMENTS_HPP

#include "s7/element.hpp"
#include "s7/float_types.hpp"
#include <limits>
#include <type_traits>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace s7 {
namespace detail {
template <SignalType T> void control_finite(signal_storage_t<T> value) {
    if constexpr (signal_type_info(T).kind == SignalKind::Float) {
        if (math::isnan(value) || math::isinf(value))
            throw std::domain_error("Control element requires finite values.");
    }
}
}

/// Numeric control: zero selects off, nonzero selects on. Direct feedthrough.
template <SignalType Type> class SwitchT final : public Element {
public:
    using V = signal_storage_t<Type>;
    SwitchT() : Element("Switch") {
        register_input(0,"off",Type,nullptr);
        register_input(1,"on",Type,nullptr);
        register_input(2,"control",Type,nullptr);
        register_output(0,port_names::out,Type,&output_);
    }
    void step() override {
        const V control=read_input<V>(2);
        detail::control_finite<Type>(control);
        output_=read_input<V>(control==V(0) ? 0 : 1);
    }
private:
    V output_{};
};

/// Turn on at/above upper, off at/below lower, hold state between thresholds.
/// State alone is not causal memory: the current input affects current output.
template <SignalType Type> class HysteresisRelayT final : public Element {
public:
    using V = signal_storage_t<Type>;
    HysteresisRelayT(V lower=V(-1), V upper=V(1), V low=V(0), V high=V(1), bool initial_on=false)
        : Element("HysteresisRelay",true), lower_(lower), upper_(upper), low_(low), high_(high),
          on_(initial_on), output_(initial_on ? high : low) {
        for (const auto value : {lower,upper,low,high}) detail::control_finite<Type>(value);
        if (!(lower<upper)) throw std::invalid_argument("HysteresisRelay lower_threshold must be less than upper_threshold.");
        register_input(0,port_names::in,Type,nullptr);
        register_output(0,port_names::out,Type,&output_);
    }
    void step() override {
        const V value=read_input<V>(0);
        detail::control_finite<Type>(value);
        if (value>=upper_) on_=true;
        else if (value<=lower_) on_=false;
        output_=on_ ? high_ : low_;
    }
private:
    V lower_,upper_,low_,high_;
    bool on_;
    V output_;
};

enum class IntegratorReset { Rising, Level };

/// Forward-rectangle state element. Reset overrides enable; enable=0 holds.
/// Limits bound the state itself, so there is no hidden accumulated windup.
/// Every update, including reset, is published at the next causal step.
template <SignalType Type> class ControlledIntegratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    ControlledIntegratorT(V dt=V(0.001), V initial=V(0),
        V lower=std::numeric_limits<V>::lowest(), V upper=std::numeric_limits<V>::max(),
        IntegratorReset reset=IntegratorReset::Rising)
        : Element("ControlledIntegrator",true,Feedthrough::None), dt_(dt), lower_(lower), upper_(upper),
          output_(initial), reset_(reset) {
        for (const auto value : {dt,initial,lower,upper}) detail::control_finite<Type>(value);
        if (!(dt>V(0))) throw std::invalid_argument("ControlledIntegrator dt must be positive.");
        if (!(lower<=initial && initial<=upper))
            throw std::invalid_argument("ControlledIntegrator requires lower_limit <= initial_value <= upper_limit.");
        (void)Rate::from_seconds(static_cast<double>(dt));
        register_input(0,"derivative",Type,nullptr);
        register_input(1,"enable",Type,nullptr);
        register_input(2,"reset",Type,nullptr);
        register_input(3,"reset_value",Type,nullptr);
        register_output(0,port_names::out,Type,&output_);
    }
    void step() override {
        const V reset_value=read_input<V>(2), enable=read_input<V>(1);
        detail::control_finite<Type>(reset_value); detail::control_finite<Type>(enable);
        const bool requested=reset_value!=V(0);
        const bool trigger=requested && (reset_==IntegratorReset::Level || !previous_reset_);
        V next=output_;
        if (trigger) {
            next=read_input<V>(3);
            detail::control_finite<Type>(next);
        } else if (enable!=V(0)) {
            const V derivative=read_input<V>(0);
            detail::control_finite<Type>(derivative);
            next=output_+dt_*derivative;
            detail::control_finite<Type>(next);
        }
        output_=next<lower_ ? lower_ : (next>upper_ ? upper_ : next);
        previous_reset_=requested;
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_seconds(static_cast<double>(dt_)); }
private:
    V dt_,lower_,upper_,output_;
    IntegratorReset reset_;
    bool previous_reset_{};
};

/// Wrap to [lower, lower+period); default radians in [-pi, pi).
template <SignalType Type> class PhaseWrapT final : public Element {
public:
    using V = signal_storage_t<Type>;
    PhaseWrapT(V period=V(2)*math::pi_v<V>, V lower=-math::pi_v<V>)
        : Element("PhaseWrap"), period_(period), lower_(lower) {
        detail::control_finite<Type>(period); detail::control_finite<Type>(lower);
        detail::control_finite<Type>(V(lower+period));
        if (!(period>V(0)) || !(lower+period>lower))
            throw std::invalid_argument("PhaseWrap requires a positive representable period.");
        register_input(0,port_names::in,Type,nullptr);
        register_output(0,port_names::out,Type,&output_);
    }
    void step() override {
        const V input=read_input<V>(0);
        detail::control_finite<Type>(input);
        // Reduce separately first, avoiding overflow from input-lower for large
        // inputs. The difference lies in (-2*period, 2*period); fmod (exact)
        // brings it into (-period, period) for the single correction below.
        V phase=math::fmod(math::fmod(input,period_)-math::fmod(lower_,period_),period_);
        detail::control_finite<Type>(phase);
        if (phase<V(0)) phase+=period_;
        if (phase>=period_) phase-=period_;
        output_=lower_+phase;
        if (output_>=lower_+period_) output_=lower_;
    }
private:
    V period_,lower_,output_{};
};

/// Rising-zero-crossing period meter. Linear interpolation is a local
/// measurement between two samples, not an event scheduler or solver.
/// Frequency stays zero/invalid until two crossings, then holds the last estimate.
template <SignalType Type> class FrequencyMeterT final : public Element {
public:
    using V = signal_storage_t<Type>;
    using P = std::conditional_t<Type==SignalType::F128,V,double>;
    explicit FrequencyMeterT(double sample_rate_hz=1000) : Element("FrequencyMeter",true), rate_(sample_rate_hz) {
        (void)Rate::from_hz(rate_);
        register_input(0,port_names::in,Type,nullptr);
        register_output(0,"frequency_hz",Type,&frequency_);
        register_output(1,"valid",Type,&valid_);
    }
    void step() override {
        const V input=read_input<V>(0);
        detail::control_finite<Type>(input);
        if (crossed_) {
            if (elapsed_==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("FrequencyMeter crossing interval overflow.");
            ++elapsed_;
        }
        if (have_previous_ && previous_<V(0) && input>=V(0)) {
            const P a=previous_, b=input;
            // Both terms are scaled to avoid overflow for opposite large values.
            const P scale=math::abs(a)>math::abs(b) ? math::abs(a) : math::abs(b);
            const P fraction=-(a/scale)/(b/scale-a/scale);
            if (crossed_) {
                const P period=P(elapsed_)+fraction-last_fraction_;
                frequency_=V(P(rate_)/period);
                detail::control_finite<Type>(frequency_);
                valid_=V(1);
            }
            crossed_=true; elapsed_=0; last_fraction_=fraction;
        }
        previous_=input; have_previous_=true;
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    double rate_;
    V previous_{},frequency_{},valid_{};
    P last_fraction_{};
    std::uint64_t elapsed_{};
    bool have_previous_{},crossed_{};
};

using Switch = SwitchT<SignalType::F64>;
using HysteresisRelay = HysteresisRelayT<SignalType::F64>;
using ControlledIntegrator = ControlledIntegratorT<SignalType::F64>;
using PhaseWrap = PhaseWrapT<SignalType::F64>;
using FrequencyMeter = FrequencyMeterT<SignalType::F64>;
}
#endif
