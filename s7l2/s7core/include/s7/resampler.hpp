// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_RESAMPLER_HPP
#define S7CORE_INCLUDE_S7_RESAMPLER_HPP

// The rate-changing element (doc/S7_Sample_Rate_Model.md section 8.3, ADR-022).
// A Resampler is the only element whose input and output rates may differ; it
// joins two rate domains, and S7 never inserts one implicitly.

#include "s7/element.hpp"
#include "s7/float_types.hpp"
#include "s7/rate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace s7 {

// An element whose input runs in another rate domain than its output.
// Rate inference does not join its input connection to its own domain;
// validation gives it its input domain's rate, and the runtime calls
// capture_input() after every step of that domain.
class RateChangingElement {
public:
    virtual ~RateChangingElement() = default;
    // The rate of the domain feeding input 0; called after adopt_rate(), which
    // gives the element's own (output) rate. May throw std::invalid_argument.
    virtual void adopt_input_rate(const Rate& input) = 0;
    // Takes the input's newest sample, published by the input domain's step.
    // A throw faults the run because capture can already have changed state.
    virtual void capture_input() = 0;
    [[nodiscard]] virtual bool changes_rate() const = 0;
};

namespace resampling {
// Linear-phase Kaiser-windowed-sinc low-pass for lowering the rate: passband
// edge 0.4 * output rate, stopband edge 0.5 * output rate, the given stopband
// attenuation, unit DC gain. An odd number of taps; its group delay is
// (taps - 1) / 2 input samples. Throws std::invalid_argument if it would need
// more than max_taps taps.
[[nodiscard]] std::vector<long double> design_anti_aliasing(const Rate& input, const Rate& output,
                                                            double attenuation_db, std::size_t max_taps = 1u << 20);

// Position of output sample `step` (time step / output) in input samples,
// split into a whole index and the fraction in [0, 1), exactly.
struct Position {
    std::uint64_t index{};
    long double fraction{};
};
[[nodiscard]] Position input_position(std::uint64_t step, const Rate& input, const Rate& output);

// Samples of `rate` at or before time step / model (the count to have taken
// by the end of model step `step`): floor(step * rate / model) + 1, exactly.
[[nodiscard]] std::uint64_t samples_through(std::uint64_t step, const Rate& rate, const Rate& model);

// samples_through for one fixed (rate, model) pair, reduced once to a 64-bit
// ratio so each step costs one 128-bit multiply and divide. Pairs whose
// reduced ratio needs more than 64 bits use the exact general function.
class SampleCounter {
public:
    SampleCounter(const Rate& rate, const Rate& model);
    [[nodiscard]] std::uint64_t samples_through(std::uint64_t step) const;

private:
    Rate rate_, model_;
    std::uint64_t numerator_{}, denominator_{};
    bool fast_{};
};
} // namespace resampling

/// Resampler: converts a signal from its input domain's rate to its output
/// domain's rate. At equal rates it passes values through unchanged. Otherwise
/// output sample j (time j / output rate) interpolates the input samples taken
/// at or before that time:
///   order 0 (default): the latest input sample (sample and hold);
///   order 1: linear, and order 2: quadratic (three samples) interpolation,
///   both one input period late, so they interpolate between samples already
///   received instead of extrapolating.
/// When it lowers the rate, the input first passes a linear-phase
/// anti-aliasing low-pass (resampling::design_anti_aliasing), adding its group
/// delay. Before enough input exists, the first input sample is held.
/// Floating-point and complex floating-point types.
template <SignalType Type>
class ResamplerT final : public Element, public RateChangingElement {
public:
    using value_type = signal_storage_t<Type>;
    using weight_type = signal_storage_t<component_signal_type(Type)>;

    explicit ResamplerT(int order = 0, double stopband_attenuation_db = 60)
        : Element("Resampler", true), order_(order), attenuation_db_(stopband_attenuation_db)
    {
        if (order < 0 || order > 2) throw std::invalid_argument("Resampler order must be 0, 1 or 2.");
        if (!(stopband_attenuation_db >= 20 && stopband_attenuation_db <= 200))
            throw std::invalid_argument("Resampler stopband_attenuation_db must be between 20 and 200.");
        register_input(0, port_names::in, Type, nullptr);
        register_output(0, port_names::out, Type, &output_);
    }

    void adopt_rate(const Rate& rate) override { output_rate_ = rate; }
    void adopt_input_rate(const Rate& input) override
    {
        input_rate_ = input;
        taps_.clear();
        if (output_rate_ && rateless_than(*output_rate_, input)) {
            for (const auto tap : resampling::design_anti_aliasing(input, *output_rate_, attenuation_db_)) {
                taps_.push_back(static_cast<weight_type>(tap));
            }
        }
    }
    [[nodiscard]] bool changes_rate() const override { return input_rate_ && output_rate_ && *input_rate_ != *output_rate_; }

    void capture_input() override
    {
        // Reclaim the consumed prefix in one move. Keep any samples the
        // scheduler delivered ahead of the next output tick.
        if (samples_.size() == samples_.capacity() && first_ > 0 && first_ >= samples_.size() / 2) {
            std::move(samples_.begin() + first_, samples_.end(), samples_.begin());
            samples_.resize(samples_.size() - first_);
            first_ = 0;
        }
        samples_.push_back(read_input<value_type>(0));
        ++captured_;
    }

    void step() override
    {
        if (!changes_rate()) {
            output_ = read_input<value_type>(0); // equal rates: unchanged
            return;
        }
        if (captured_ == 0) throw std::logic_error("Resampler stepped before its input domain produced a sample.");
        const auto position = resampling::input_position(step_++, *input_rate_, *output_rate_);
        const auto u = static_cast<weight_type>(position.fraction);
        std::int64_t n = static_cast<std::int64_t>(position.index);
        if (order_ == 0) {
            output_ = sample(n);
        } else {
            --n; // one input period late: interpolate between received samples
            if (order_ == 1) {
                output_ = sample(n) * (weight_type(1) - u) + sample(n + 1) * u;
            } else {
                output_ = sample(n - 1) * (u * (u - weight_type(1)) / weight_type(2)) +
                          sample(n) * ((weight_type(1) - u) * (weight_type(1) + u)) +
                          sample(n + 1) * (u * (u + weight_type(1)) / weight_type(2));
            }
        }
        discard_before(n - 1 - static_cast<std::int64_t>(taps_.size()));
    }

    [[nodiscard]] int order() const { return order_; }
    // Input samples of filter group delay (0 without anti-aliasing).
    [[nodiscard]] std::size_t filter_delay() const { return taps_.empty() ? 0 : (taps_.size() - 1) / 2; }

private:
    static bool rateless_than(const Rate& a, const Rate& b)
    {
        // a < b for positive rationals, compared exactly in 128 bits.
        return static_cast<uint128_t>(a.numerator) * b.denominator < static_cast<uint128_t>(b.numerator) * a.denominator;
    }
    // Input sample i (filtered when anti-aliasing), holding the first sample
    // before the start and never reading past the newest captured sample.
    value_type sample(std::int64_t i) const
    {
        if (i >= 0 && static_cast<std::uint64_t>(i) >= captured_)
            throw std::logic_error("Resampler read an input sample it has not received.");
        const auto oldest = oldest_sample();
        const auto offset = i > oldest ? static_cast<std::size_t>(i - oldest) : 0;
        const auto* first = samples_.data() + first_;
        const auto* current = first + offset;
        const auto tap_count = taps_.size();
        if (!tap_count) return *current;
        const auto* taps = taps_.data();
        const auto available = i < oldest ? 0 : std::min(tap_count, offset + 1);
        value_type sum{};
        for (std::size_t k = 0; k < available; ++k)
            sum = sum + current[-static_cast<std::ptrdiff_t>(k)] * taps[k];
        for (std::size_t k = available; k < tap_count; ++k)
            sum = sum + *first * taps[k];
        return sum;
    }
    std::int64_t oldest_sample() const
    {
        return static_cast<std::int64_t>(captured_ - (samples_.size() - first_));
    }
    void discard_before(std::int64_t i)
    {
        const auto oldest = oldest_sample();
        if (i > oldest) first_ += std::min(static_cast<std::size_t>(i - oldest), samples_.size() - first_ - 1);
    }

    int order_;
    double attenuation_db_;
    std::optional<Rate> input_rate_, output_rate_;
    std::vector<weight_type> taps_;
    std::vector<value_type> samples_;
    std::size_t first_{};
    std::uint64_t captured_{}, step_{};
    value_type output_{};
};

using Resampler = ResamplerT<SignalType::F64>;

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RESAMPLER_HPP
