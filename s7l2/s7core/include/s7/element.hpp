#ifndef S7CORE_INCLUDE_S7_ELEMENT_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_HPP

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

// Port is a lightweight reference to storage owned by the Element
// (Inspired by s7pp's TypedOutput<T> holding T& val_)
struct Port {
    std::size_t index;
    std::string_view name;
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

/// Signal processing element base class.
/// Elements own their storage as member variables (like s7pp).
/// Ports are lightweight references to that storage.
class Element {
public:
    virtual ~Element() = default;

    [[nodiscard]] std::string_view type_name() const { return type_name_; }
    [[nodiscard]] bool has_state() const { return has_state_; }
    [[nodiscard]] bool has_causal_memory() const { return has_state_; }

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

    // Process one sample: read from inputs, write to outputs
    virtual void step() = 0;

protected:
    Element(std::string_view type_name, bool has_state = false)
        : type_name_(type_name), has_state_(has_state)
    {}

    // Read input value - FAST! No checks (validated at compile time)
    template <typename T>
    [[nodiscard]] T read_input(std::size_t idx) const {
        return *static_cast<const T*>(input_bindings_[idx]);
    }

    // Register ports - called by subclass constructor after members are initialized
    void register_input(std::size_t idx, std::string_view name, SignalType type, void* ptr) {
        input_ports_.push_back({idx, name, type, ptr});
    }

    void register_output(std::size_t idx, std::string_view name, SignalType type, void* ptr) {
        output_ports_.push_back({idx, name, type, ptr});
    }

private:
    std::string_view type_name_;
    bool has_state_;
    std::vector<Port> input_ports_;
    std::vector<Port> output_ports_;
    std::vector<InputBinding> input_bindings_;
};

using ElementFactory = std::function<std::unique_ptr<Element>()>;

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENT_HPP
