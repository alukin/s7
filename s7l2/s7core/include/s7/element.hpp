// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ELEMENT_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_HPP

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "s7/rate.hpp"
#include "s7/signal_types.hpp"

namespace s7 {

using RealValue = signal_storage_t<SignalType::F64>;
using ComplexValue = signal_storage_t<SignalType::C64>;

// Common port names
namespace port_names {
    constexpr std::string_view in = "in";
    constexpr std::string_view out = "out";
    constexpr std::string_view lhs = "lhs";
    constexpr std::string_view rhs = "rhs";
    constexpr std::string_view re = "re";
    constexpr std::string_view im = "im";
}

// Port names the port and references value storage owned by the Element
// (Inspired by s7pp's TypedOutput<T> holding T& val_)
struct Port {
    std::size_t index;
    std::string name;
    SignalType type;
    void* value_ptr;  // Points to element's member variable

    // Helper to get typed value
    template <typename T>
    T& as() { return *static_cast<T*>(value_ptr); }

    template <typename T>
    const T& as() const { return *static_cast<const T*>(value_ptr); }
};

using InputBinding = const void*;
using OutputBinding = const void*;



template <SignalType Type>
[[nodiscard]] inline InputBinding bind_input(const signal_storage_t<Type>& value) {
    return &value;
}

template <SignalType Type>
[[nodiscard]] inline OutputBinding bind_output(const signal_storage_t<Type>& value) {
    return &value;
}

template <SignalType Type>
[[nodiscard]] inline const signal_storage_t<Type>& bound_value(const void* binding) {
    return *static_cast<const signal_storage_t<Type>*>(binding);
}

/// Whether an element's outputs in a step depend on its inputs in the same step.
/// Direct: outputs are computed from current inputs (Gain, FirFilter, Accumulator).
/// None: outputs depend only on earlier steps (Delay, PreviousValue). Only these
/// elements may close a feedback loop.
enum class Feedthrough : std::uint8_t {
    Direct,
    None,
};

/// Signal processing element base class.
/// Elements own their storage as member variables (like s7pp).
/// Ports are lightweight references to that storage.
class Element {
public:
    virtual ~Element() = default;
    // Declared because the virtual destructor suppresses the implicit moves;
    // moving transfers port and binding storage without allocating. Port
    // pointers are copied unchanged: they still address the source's storage
    // unless the derived class keeps that storage valid (as Schema does) or
    // re-registers its ports. Elements are normally created in place by a
    // factory and owned through unique_ptr, so they are not moved.
    Element(const Element&) = default;
    Element& operator=(const Element&) = default;
    Element(Element&&) noexcept = default;
    Element& operator=(Element&&) noexcept = default;

    [[nodiscard]] std::string_view type_name() const { return type_name_; }
    // Stateless operations may be reevaluated at numerical trial states.
    // Elements with mutable history or side effects must declare has_state.
    [[nodiscard]] bool has_state() const { return has_state_; }
    [[nodiscard]] Feedthrough feedthrough() const { return feedthrough_; }
    [[nodiscard]] bool has_causal_memory() const { return feedthrough_ == Feedthrough::None; }

    // This element's own rate, pinned by one of its parameters (an
    // integrator's "dt", an oscillator's "sample_rate_hz"); nullopt for an
    // element with no such parameter, whose rate is purely inferred from its
    // connections (doc/S7_Sample_Rate_Model.md section 3, s7/rate_inference.hpp).
    [[nodiscard]] virtual std::optional<Rate> declared_rate() const { return std::nullopt; }
    // The rate rate inference resolved for this element, given once by the
    // compiler before inputs are bound (doc/S7_Sample_Rate_Model.md section
    // 8). A rate-following element (declaring no rate) derives its step size,
    // increments and sample counts here; it may throw when a parameter has no
    // exact meaning at that rate (a duration that is not a whole number of
    // samples). Elements with no rate-dependent behavior ignore it.
    virtual void adopt_rate(const Rate& /*rate*/) {}

    [[nodiscard]] std::size_t input_count() const { return input_ports_.size(); }
    [[nodiscard]] std::size_t output_count() const { return output_ports_.size(); }

    [[nodiscard]] std::span<const Port> input_ports() const { return input_ports_; }
    [[nodiscard]] std::span<const Port> output_ports() const { return output_ports_; }

    // Binding for runtime connections - validates once during compilation
    virtual void bind_inputs(std::span<const InputBinding> bindings) {
        if (bindings.size() != input_ports_.size()) {
            throw std::runtime_error("Input binding count mismatch");
        }
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            if (!bindings[i]) {
                throw std::runtime_error("Null input binding at index " + std::to_string(i));
            }
        }
        input_bindings_.assign(bindings.begin(), bindings.end());
    }

    // Get output bindings for connections - just extract from ports
    [[nodiscard]] std::vector<OutputBinding> outputs() const {
        std::vector<OutputBinding> result;
        result.reserve(output_ports_.size());
        for (const auto& port : output_ports_) {
            result.push_back(port.value_ptr);
        }
        return result;
    }

    // Process one sample: read from inputs, write to outputs. Elements with
    // direct feedthrough (stateful or not) run in topological order, so their
    // outputs are visible downstream in the same step. Causal-memory elements
    // (Feedthrough::None) run afterwards: step() writes the output for the
    // next step from the current inputs, and NodeRuntime keeps all causal-memory
    // outputs at their pre-step values until every one has run, then publishes
    // them together. An element's private state may advance during step(), but
    // must not be shared with another element as an implicit communication
    // channel.
    // Implicit integrators can additionally implement TrapezoidIntegration;
    // NodeRuntime uses that coupled stage instead of their ordinary step().
    virtual void step() = 0;

protected:
    Element(std::string_view type_name,
            bool has_state = false,
            Feedthrough feedthrough = Feedthrough::Direct)
        : type_name_(type_name), has_state_(has_state), feedthrough_(feedthrough)
    {}

    // Address of an input's bound value (for type-generic elements).
    [[nodiscard]] InputBinding input_binding(std::size_t idx) const {
        return input_bindings_[idx];
    }

    // Read input value - FAST! No checks (validated at compile time)
    template <typename T>
    [[nodiscard]] T read_input(std::size_t idx) const {
        return *static_cast<const T*>(input_bindings_[idx]);
    }

    // Register ports - called by subclass constructor after members are initialized
    void register_input(std::size_t idx, std::string_view name, SignalType type, void* ptr) {
        input_ports_.push_back({idx, std::string(name), type, ptr});
    }

    void register_output(std::size_t idx, std::string_view name, SignalType type, void* ptr) {
        output_ports_.push_back({idx, std::string(name), type, ptr});
    }

private:
    std::string_view type_name_;
    bool has_state_;
    Feedthrough feedthrough_;
    std::vector<Port> input_ports_;
    std::vector<Port> output_ports_;
    std::vector<InputBinding> input_bindings_;
};

using ElementFactory = std::function<std::unique_ptr<Element>()>;

namespace detail {
// One sample period of `rate` (1 / rate seconds) as T, divided from the exact
// rational in a type at least as wide as double.
template <typename T>
[[nodiscard]] T rate_period(const Rate& rate)
{
    using W = std::conditional_t<(sizeof(T) > sizeof(double)), T, double>;
    return static_cast<T>(static_cast<W>(rate.denominator) / static_cast<W>(rate.numerator));
}

// A duration parameter as a whole number of samples at `rate`; throws
// std::invalid_argument naming the parameter when it has no exact meaning.
[[nodiscard]] inline std::uint64_t duration_samples(const Rate& rate, const double seconds, const std::string_view name)
{
    if (const auto samples = rate.whole_samples(seconds)) return *samples;
    throw std::invalid_argument("'" + std::string(name) + "' of " + std::to_string(seconds) +
                                " s is not a whole number of samples at " + rate.to_string() + ".");
}
} // namespace detail

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENT_HPP
