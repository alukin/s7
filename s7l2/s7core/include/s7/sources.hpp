#ifndef S7CORE_INCLUDE_S7_SOURCES_HPP
#define S7CORE_INCLUDE_S7_SOURCES_HPP

#include "s7/element.hpp"
#include "s7/float_types.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace s7 {
namespace detail {
template <typename V> void require_source_finite(V value) {
    if (value - value != V(0)) throw std::invalid_argument("Source parameters must be finite.");
}
inline void require_source_rate(double rate) {
    if (!std::isfinite(rate) || rate <= 0) throw std::invalid_argument("Source sample_rate_hz must be finite and positive.");
    (void)Rate::from_hz(rate);
}

// Keep carrier timing accurate even for F16 outputs. F128 retains its precision.
template <SignalType T>
using source_phase_t = std::conditional_t<T == SignalType::F128, signal_storage_t<T>, double>;

template <typename P> P source_phase(P phase) {
    require_source_finite(phase);
    phase = math::fmod(phase, P(1));
    return phase < P(0) ? phase + P(1) : phase;
}
}

/// Triangle and sawtooth source. Phase is in cycles; amplitude is half the
/// peak-to-peak range. rise_fraction=0/1 gives falling/rising sawtooth.
template <SignalType Type>
class TriangleGeneratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    using P = detail::source_phase_t<Type>;
    TriangleGeneratorT(V amplitude = V(1), V offset = V(0), V frequency_hz = V(1),
                       double sample_rate_hz = 1000, double phase = 0, double rise_fraction = 0.5)
        : Element("TriangleGenerator", true), amplitude_(amplitude), offset_(offset),
          rate_(sample_rate_hz), phase_(detail::source_phase(P(phase))), rise_(rise_fraction)
    {
        detail::require_source_rate(rate_);
        detail::require_source_finite(amplitude); detail::require_source_finite(offset);
        detail::require_source_finite(frequency_hz);
        if (!std::isfinite(rise_fraction) || rise_fraction < 0 || rise_fraction > 1)
            throw std::invalid_argument("TriangleGenerator rise_fraction must be in [0,1].");
        increment_ = detail::source_phase(P(frequency_hz) / P(rate_));
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        const P unit = rise_ == P(0) ? P(1)-P(2)*phase_ :
                       rise_ == P(1) ? P(2)*phase_-P(1) :
                       phase_ < rise_ ? P(2)*phase_/rise_-P(1) :
                       P(1)-P(2)*(phase_-rise_)/(P(1)-rise_);
        output_ = offset_ + amplitude_ * V(unit);
        phase_ = detail::source_phase(phase_ + increment_);
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    V amplitude_, offset_, output_{};
    double rate_;
    P phase_, rise_, increment_{};
};

/// Periodic rectangular source; phase in cycles, high while phase < duty.
template <SignalType Type>
class PulseGeneratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    using P = detail::source_phase_t<Type>;
    PulseGeneratorT(V low = V(0), V high = V(1), V frequency_hz = V(1),
                    double sample_rate_hz = 1000, double duty = 0.5, double phase = 0)
        : Element("PulseGenerator", true), low_(low), high_(high), rate_(sample_rate_hz),
          phase_(detail::source_phase(P(phase))), duty_(duty)
    {
        detail::require_source_rate(rate_);
        detail::require_source_finite(low); detail::require_source_finite(high);
        detail::require_source_finite(frequency_hz);
        if (!std::isfinite(duty) || duty < 0 || duty > 1)
            throw std::invalid_argument("PulseGenerator duty must be in [0,1].");
        increment_ = detail::source_phase(P(frequency_hz) / P(rate_));
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        output_ = phase_ < duty_ ? high_ : low_;
        phase_ = detail::source_phase(phase_ + increment_);
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    V low_, high_, output_{};
    double rate_;
    P phase_, duty_, increment_{};
};

/// One sample with height area / dt at start_sample (zero-based).
template <SignalType Type>
class ImpulseGeneratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    using P = detail::source_phase_t<Type>;
    ImpulseGeneratorT(V area = V(1), double sample_rate_hz = 1000, std::uint64_t start_sample = 0)
        : Element("ImpulseGenerator", true), rate_(sample_rate_hz), remaining_(start_sample)
    {
        detail::require_source_rate(rate_); detail::require_source_finite(area);
        height_ = V(P(area) * P(rate_));
        detail::require_source_finite(height_);
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        output_ = V(0);
        if (remaining_) --remaining_;
        else if (!fired_) { output_ = height_; fired_ = true; }
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    double rate_;
    std::uint64_t remaining_;
    bool fired_{};
    V height_{}, output_{};
};

/// Sigma/step source: initial for n < start_sample, final thereafter.
template <SignalType Type>
class StepGeneratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    StepGeneratorT(V initial = V(0), V final = V(1), double sample_rate_hz = 1000,
                   std::uint64_t start_sample = 0)
        : Element("StepGenerator", true), initial_(initial), final_(final), rate_(sample_rate_hz), remaining_(start_sample)
    {
        detail::require_source_rate(rate_);
        detail::require_source_finite(initial); detail::require_source_finite(final);
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        output_ = remaining_ ? initial_ : final_;
        if (remaining_) --remaining_;
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    V initial_, final_, output_{};
    double rate_;
    std::uint64_t remaining_;
};

/// Sine VCO. Frequency input is Hz; phase parameter is radians, as for
/// AnalogOscillator. Sample n emits its current phase, then advances by f[n]/fs.
template <SignalType Type>
class ControlledOscillatorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    using P = detail::source_phase_t<Type>;
    ControlledOscillatorT(V amplitude = V(1), V offset = V(0), double sample_rate_hz = 1000, V phase = V(0))
        : Element("ControlledOscillator", true), amplitude_(amplitude), offset_(offset), rate_(sample_rate_hz)
    {
        detail::require_source_rate(rate_);
        detail::require_source_finite(amplitude); detail::require_source_finite(offset);
        phase_ = detail::source_phase(P(phase) / (P(2)*math::pi_v<P>));
        register_input(0, "frequency_hz", Type, nullptr);
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        const auto frequency = read_input<V>(0);
        if (frequency-frequency != V(0)) throw std::domain_error("ControlledOscillator frequency_hz must be finite.");
        output_ = offset_ + amplitude_ * V(math::sin(P(2)*math::pi_v<P>*phase_));
        phase_ = detail::source_phase(phase_ + detail::source_phase(P(frequency)/P(rate_)));
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    V amplitude_, offset_, output_{};
    double rate_;
    P phase_{};
};

/// Sampled sine sweep: frequency[n] runs from start_hz to end_hz over
/// sweep_samples intervals. Phase follows the same recurrence as the VCO.
enum class ChirpSweep { Linear, Logarithmic };
template <SignalType Type>
class ChirpGeneratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    using P = detail::source_phase_t<Type>;
    ChirpGeneratorT(V start_hz = V(1), V end_hz = V(100), std::uint64_t sweep_samples = 1000,
                    double sample_rate_hz = 1000, V amplitude = V(1), V offset = V(0),
                    V phase = V(0), ChirpSweep sweep = ChirpSweep::Linear, bool repeat = false)
        : Element("ChirpGenerator", true), start_(start_hz), end_(end_hz), count_(sweep_samples),
          rate_(sample_rate_hz), amplitude_(amplitude), offset_(offset), sweep_(sweep), repeat_(repeat)
    {
        detail::require_source_rate(rate_);
        detail::require_source_finite(start_hz); detail::require_source_finite(end_hz);
        detail::require_source_finite(amplitude); detail::require_source_finite(offset);
        phase_ = detail::source_phase(P(phase)/(P(2)*math::pi_v<P>));
        if (!count_) throw std::invalid_argument("ChirpGenerator sweep_samples must be positive.");
        if (sweep_ == ChirpSweep::Logarithmic) {
            if (start_ <= P(0) || end_ <= P(0))
                throw std::invalid_argument("Logarithmic chirp frequencies must be positive.");
            log_ratio_ = math::log(end_) - math::log(start_);
        }
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        const P fraction = P(sample_)/P(count_);
        const P frequency = sample_ == count_ ? end_ : sweep_ == ChirpSweep::Linear ?
            (P(1)-fraction)*start_ + fraction*end_ : math::exp(math::log(start_) + fraction*log_ratio_);
        output_ = offset_ + amplitude_*V(math::sin(P(2)*math::pi_v<P>*phase_));
        phase_ = detail::source_phase(phase_ + detail::source_phase(frequency/P(rate_)));
        if (sample_ < count_) ++sample_;
        if (repeat_ && sample_ == count_) sample_ = 0;
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    P start_, end_, phase_{}, log_ratio_{};
    std::uint64_t count_, sample_{};
    double rate_;
    V amplitude_, offset_, output_{};
    ChirpSweep sweep_;
    bool repeat_;
};

/// Maximal-length PRBS7 / PRBS15, left-shifting Fibonacci LFSR. The
/// most-significant bit selects high/low before shifting; no random seed.
template <SignalType Type>
class PrbsGeneratorT final : public Element {
public:
    using V = signal_storage_t<Type>;
    PrbsGeneratorT(unsigned order = 7, std::uint32_t seed = 1, std::uint64_t bit_samples = 1,
                   double sample_rate_hz = 1000, V low = V(-1), V high = V(1))
        : Element("PRBSGenerator", true), order_(order), state_(seed), bit_samples_(bit_samples),
          rate_(sample_rate_hz), low_(low), high_(high)
    {
        detail::require_source_rate(rate_);
        detail::require_source_finite(low); detail::require_source_finite(high);
        if (order_ != 7 && order_ != 15) throw std::invalid_argument("PRBSGenerator order must be 7 or 15.");
        mask_ = (std::uint32_t(1)<<order_)-1;
        if (!seed || seed > mask_) throw std::invalid_argument("PRBSGenerator seed must be nonzero and fit its order.");
        if (!bit_samples_) throw std::invalid_argument("PRBSGenerator bit_samples must be positive.");
        register_output(0, port_names::out, Type, &output_);
    }
    void step() override {
        output_ = state_ & (std::uint32_t(1)<<(order_-1)) ? high_ : low_;
        if (++sample_ == bit_samples_) {
            const auto feedback = ((state_>>(order_-1)) ^ (state_>>(order_-2))) & 1u;
            state_ = ((state_<<1) | feedback) & mask_;
            sample_ = 0;
        }
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return Rate::from_hz(rate_); }
private:
    unsigned order_;
    std::uint32_t state_, mask_{};
    std::uint64_t bit_samples_, sample_{};
    double rate_;
    V low_, high_, output_{};
};

using ChirpGenerator = ChirpGeneratorT<SignalType::F64>;
using PRBSGenerator = PrbsGeneratorT<SignalType::F64>;

using TriangleGenerator = TriangleGeneratorT<SignalType::F64>;
using PulseGenerator = PulseGeneratorT<SignalType::F64>;
using ImpulseGenerator = ImpulseGeneratorT<SignalType::F64>;
using StepGenerator = StepGeneratorT<SignalType::F64>;
using ControlledOscillator = ControlledOscillatorT<SignalType::F64>;

} // namespace s7
#endif
