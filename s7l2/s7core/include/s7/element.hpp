#ifndef S7CORE_INCLUDE_S7_ELEMENT_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_HPP

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace s7 {

using RealValue = double;
using ComplexValue = std::complex<double>;

// Common port names
namespace port_names {
    constexpr std::string_view in = "in";
    constexpr std::string_view out = "out";
    constexpr std::string_view lhs = "lhs";
    constexpr std::string_view rhs = "rhs";
    constexpr std::string_view re = "re";
    constexpr std::string_view im = "im";
}

// Signal types - ports have types, elements don't
enum class SignalType : std::uint8_t {
    DOUBLE,
    COMPLEX,
    INT8,
    UINT8,
    INT12,
    UINT12,
    INT16,
    UINT16,
};

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

[[nodiscard]] inline bool is_complex_signal_type(SignalType type) {
    return type == SignalType::COMPLEX;
}

[[nodiscard]] inline std::string_view signal_type_name(SignalType type) {
    switch (type) {
        case SignalType::DOUBLE: return "DOUBLE";
        case SignalType::COMPLEX: return "COMPLEX";
        case SignalType::INT8: return "INT8";
        case SignalType::UINT8: return "UINT8";
        case SignalType::INT12: return "INT12";
        case SignalType::UINT12: return "UINT12";
        case SignalType::INT16: return "INT16";
        case SignalType::UINT16: return "UINT16";
    }
    return "UNKNOWN";
}

[[nodiscard]] inline constexpr bool is_runtime_convertible(SignalType from, SignalType to) {
    return from == to;
}

// Portable meaning of each SignalType. DOUBLE is IEEE 754 binary64 and
// COMPLEX a pair of binary64 values. Integer types are stored in a RealValue
// (double) that always holds an exact integer within the type's range: every
// integer type here is at most 16 bits wide, so the double is exact. Elements
// producing an integer type must write values that satisfy this; converting a
// real value uses quantize_to_signal_type() (round half to even, saturate,
// NaN to 0). wire_type_id is the data type code of the candidate wire format
// (S7_Network_Wire_Protocol_v1.md section 8).
struct SignalTypeInfo {
    SignalType type;
    bool is_integer;
    bool is_signed;
    unsigned bits;         // value width (64 for DOUBLE, 128 for COMPLEX)
    double min_value;      // integer types only
    double max_value;      // integer types only
    std::uint16_t wire_type_id;
};

[[nodiscard]] inline constexpr SignalTypeInfo signal_type_info(SignalType type) {
    switch (type) {
        case SignalType::DOUBLE: return {type, false, true, 64, 0.0, 0.0, 0x000F};
        case SignalType::COMPLEX: return {type, false, true, 128, 0.0, 0.0, 0x0103};
        case SignalType::INT8: return {type, true, true, 8, -128.0, 127.0, 0x0001};
        case SignalType::UINT8: return {type, true, false, 8, 0.0, 255.0, 0x0002};
        case SignalType::INT12: return {type, true, true, 12, -2048.0, 2047.0, 0x0003};
        case SignalType::UINT12: return {type, true, false, 12, 0.0, 4095.0, 0x0004};
        case SignalType::INT16: return {type, true, true, 16, -32768.0, 32767.0, 0x0005};
        case SignalType::UINT16: return {type, true, false, 16, 0.0, 65535.0, 0x0006};
    }
    return {type, false, false, 0, 0.0, 0.0, 0};
}

// Whether value is a valid stored value of an integer type (or any value for DOUBLE).
[[nodiscard]] inline bool is_valid_signal_value(SignalType type, RealValue value) {
    const auto info = signal_type_info(type);
    if (!info.is_integer) {
        return true;
    }
    return std::nearbyint(value) == value && value >= info.min_value && value <= info.max_value;
}

// Converts a real value to an integer signal type: round half to even, then
// saturate to the type's range; NaN becomes 0. DOUBLE values pass unchanged.
[[nodiscard]] inline RealValue quantize_to_signal_type(SignalType type, RealValue value) {
    const auto info = signal_type_info(type);
    if (!info.is_integer) {
        return value;
    }
    if (std::isnan(value)) {
        return 0.0;
    }
    const auto rounded = std::nearbyint(value); // default rounding mode: half to even
    return std::clamp(rounded, info.min_value, info.max_value);
}

template <SignalType Type>
struct SignalStorage {
    using type = RealValue;
};

template <>
struct SignalStorage<SignalType::COMPLEX> {
    using type = ComplexValue;
};

template <SignalType Type>
using signal_storage_t = typename SignalStorage<Type>::type;

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
/// Direct: outputs are computed from current inputs (Gain, FirFilter, Integrator).
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

    [[nodiscard]] std::string_view type_name() const { return type_name_; }
    [[nodiscard]] bool has_state() const { return has_state_; }
    [[nodiscard]] Feedthrough feedthrough() const { return feedthrough_; }
    [[nodiscard]] bool has_causal_memory() const { return feedthrough_ == Feedthrough::None; }

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
    virtual void step() = 0;

protected:
    Element(std::string_view type_name,
            bool has_state = false,
            Feedthrough feedthrough = Feedthrough::Direct)
        : type_name_(type_name), has_state_(has_state), feedthrough_(feedthrough)
    {}

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

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENT_HPP
