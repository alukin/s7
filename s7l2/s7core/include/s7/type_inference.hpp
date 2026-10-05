#ifndef S7CORE_INCLUDE_S7_TYPE_INFERENCE_HPP
#define S7CORE_INCLUDE_S7_TYPE_INFERENCE_HPP

// Type inference for generic elements (ELEMENT_DEV_GUIDE.md, "Type system").
//
// A generic element has type variables, each with a set of allowed types and
// optionally a pinned type, and describes every port's type in terms of them.
// Before validation and compilation the resolver narrows each variable's set
// across connections until nothing changes; a variable left open takes F64
// (or C64 if only complex types are allowed), and narrowing continues. An
// empty set is a conflict and is reported with the element and port names.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "s7/element.hpp"

namespace s7 {

class Schema;

// A set of signal types (bit i stands for kAllSignalTypes[i]).
class SignalTypeSet {
public:
    constexpr SignalTypeSet() = default;
    constexpr SignalTypeSet(std::initializer_list<SignalType> types)
    {
        for (const auto type : types) {
            insert(type);
        }
    }

    [[nodiscard]] static constexpr SignalTypeSet all()
    {
        SignalTypeSet set;
        set.bits_ = (1u << kAllSignalTypes.size()) - 1u;
        return set;
    }

    template <typename Predicate>
    [[nodiscard]] static constexpr SignalTypeSet where(Predicate predicate)
    {
        SignalTypeSet set;
        for (const auto type : kAllSignalTypes) {
            if (predicate(type)) {
                set.insert(type);
            }
        }
        return set;
    }

    constexpr void insert(const SignalType type) { bits_ |= bit(type); }
    [[nodiscard]] constexpr bool contains(const SignalType type) const { return (bits_ & bit(type)) != 0; }
    [[nodiscard]] constexpr bool empty() const { return bits_ == 0; }
    [[nodiscard]] constexpr std::size_t size() const { return static_cast<std::size_t>(__builtin_popcount(bits_)); }
    [[nodiscard]] constexpr SignalTypeSet intersect(const SignalTypeSet other) const
    {
        SignalTypeSet set;
        set.bits_ = bits_ & other.bits_;
        return set;
    }
    [[nodiscard]] constexpr std::optional<SignalType> single() const
    {
        if (size() != 1) {
            return std::nullopt;
        }
        return kAllSignalTypes[static_cast<std::size_t>(__builtin_ctz(bits_))];
    }
    [[nodiscard]] std::vector<SignalType> types() const;
    [[nodiscard]] std::string to_string() const;

    friend constexpr bool operator==(SignalTypeSet, SignalTypeSet) = default;

private:
    [[nodiscard]] static constexpr std::uint32_t bit(const SignalType type)
    {
        for (std::size_t index = 0; index < kAllSignalTypes.size(); ++index) {
            if (kAllSignalTypes[index] == type) {
                return 1u << index;
            }
        }
        return 0;
    }

    std::uint32_t bits_{};
};

// Common sets.
namespace signal_type_sets {
inline constexpr auto all = SignalTypeSet::all();
// Everything except CU8, which has no arithmetic.
inline constexpr auto arithmetic = SignalTypeSet::where([](SignalType t) {
    return signal_type_info(t).kind != SignalKind::ComplexUnsigned;
});
inline constexpr auto real = SignalTypeSet::where([](SignalType t) { return !signal_type_info(t).is_complex(); });
inline constexpr auto floating = SignalTypeSet::where([](SignalType t) { return signal_type_info(t).kind == SignalKind::Float; });
inline constexpr auto complex_floating =
    SignalTypeSet::where([](SignalType t) { return signal_type_info(t).kind == SignalKind::ComplexFloat; });
inline constexpr auto complex_all = SignalTypeSet::where([](SignalType t) { return signal_type_info(t).is_complex(); });
} // namespace signal_type_sets

// The type of one port in terms of the element's type variables.
struct PortTypeRule {
    enum class Kind : std::uint8_t {
        Fixed,       // `fixed`
        Variable,    // the variable itself
        ComponentOf, // component type of the (complex) variable: C64 -> F64
        ComplexOf,   // complex type with the variable as component: F64 -> C64
    };

    Kind kind{Kind::Fixed};
    SignalType fixed{SignalType::F64};
    std::size_t variable{};

    [[nodiscard]] static PortTypeRule of_fixed(const SignalType type) { return {Kind::Fixed, type, 0}; }
    [[nodiscard]] static PortTypeRule of_variable(const std::size_t index) { return {Kind::Variable, SignalType::F64, index}; }
    // "T", "component(T)", "complex(T)" with T one of the variable names; throws otherwise.
    [[nodiscard]] static PortTypeRule parse(std::string_view rule, std::span<const std::string> variable_names);
};

struct TypeVariable {
    std::string name;
    SignalTypeSet allowed;
    std::optional<SignalType> pinned; // from a parameter such as "type"
};

// An element whose port types are resolved by inference. Ports beyond the
// listed rules (variadic inputs) use the last input/output rule.
struct GenericElement {
    std::vector<TypeVariable> variables;
    std::vector<PortTypeRule> input_rules;
    std::vector<PortTypeRule> output_rules;
    // Creates the element for the resolved variable types (one per variable).
    std::function<std::unique_ptr<Element>(std::span<const SignalType>)> create;
};

struct ResolvedTypes {
    // Per schema element, in declaration order: resolved variable types
    // (empty for concrete elements).
    std::vector<std::vector<SignalType>> variables;
    // Per schema element: factory producing the element with resolved types.
    std::vector<ElementFactory> factories;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const { return errors.empty(); }
};

class TypeResolver {
public:
    [[nodiscard]] static ResolvedTypes resolve(const Schema& schema);
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_TYPE_INFERENCE_HPP
