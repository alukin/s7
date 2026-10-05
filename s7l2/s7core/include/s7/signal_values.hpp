#ifndef S7CORE_INCLUDE_S7_SIGNAL_VALUES_HPP
#define S7CORE_INCLUDE_S7_SIGNAL_VALUES_HPP

// Typed values for generic elements: uniform arithmetic over every signal
// type (with the element's integer Overflow rule) and exact conversion of
// JSON parameter values into a type's storage.

#include <boost/json.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

#include "s7/signal_arithmetic.hpp"
#include "s7/signal_types.hpp"

namespace s7 {

template <SignalType Type>
struct SignalOps {
    using value_type = signal_storage_t<Type>;
    static constexpr SignalKind kind = SignalTraits<Type>::kind;

    [[nodiscard]] static constexpr value_type zero() { return value_type{}; }

    [[nodiscard]] static constexpr value_type one()
    {
        if constexpr (kind == SignalKind::ComplexInteger || kind == SignalKind::ComplexUnsigned) {
            return value_type{1, 0};
        } else {
            return value_type(1);
        }
    }

    [[nodiscard]] static constexpr value_type add(const value_type& a, const value_type& b, const Overflow overflow)
    {
        if constexpr (kind == SignalKind::SignedInteger || kind == SignalKind::UnsignedInteger) {
            return integer_add<Type>(a, b, overflow);
        } else if constexpr (kind == SignalKind::ComplexInteger) {
            return complex_integer_add<Type>(a, b, overflow);
        } else {
            static_assert(kind == SignalKind::Float || kind == SignalKind::ComplexFloat, "no arithmetic for this type");
            return a + b;
        }
    }

    [[nodiscard]] static constexpr value_type multiply(const value_type& a, const value_type& b, const Overflow overflow)
    {
        if constexpr (kind == SignalKind::SignedInteger || kind == SignalKind::UnsignedInteger) {
            return integer_multiply<Type>(a, b, overflow);
        } else if constexpr (kind == SignalKind::ComplexInteger) {
            return complex_integer_multiply<Type>(a, b, overflow);
        } else {
            static_assert(kind == SignalKind::Float || kind == SignalKind::ComplexFloat, "no arithmetic for this type");
            return a * b;
        }
    }
};

// Whether a type has arithmetic (everything except CU8).
[[nodiscard]] constexpr bool has_arithmetic(const SignalType type)
{
    return signal_type_info(type).kind != SignalKind::ComplexUnsigned;
}

namespace detail {

[[noreturn]] inline void bad_value(const std::string_view what, const std::string& reason)
{
    throw std::invalid_argument(std::string(what) + ": " + reason);
}

// An integer component from a JSON integer, or from a decimal string (for
// values beyond 64 bits); fractions and out-of-range values are rejected.
template <SignalType Type>
[[nodiscard]] auto integer_from_json(const boost::json::value& json, const std::string_view what)
    -> typename IntegerRange<Type>::component
{
    using Range = IntegerRange<Type>;
    using C = typename Range::component;
    const auto type_name = std::string(signal_type_name(Type));
    if (json.is_int64() || json.is_uint64()) {
        if (json.is_int64()) {
            const auto value = json.get_int64();
            if constexpr (!Range::is_signed) {
                if (value < 0) {
                    bad_value(what, "negative value for unsigned " + type_name);
                }
            }
            if (static_cast<int128_t>(value) < static_cast<int128_t>(Range::min) ||
                (value > 0 && static_cast<uint128_t>(value) > static_cast<uint128_t>(Range::max))) {
                bad_value(what, std::to_string(value) + " is out of range for " + type_name);
            }
            return static_cast<C>(value);
        }
        const auto value = json.get_uint64();
        if (static_cast<uint128_t>(value) > static_cast<uint128_t>(Range::max)) {
            bad_value(what, std::to_string(value) + " is out of range for " + type_name);
        }
        return static_cast<C>(value);
    }
    if (json.is_string()) {
        const std::string_view text = json.get_string();
        bool negative = false;
        std::size_t index = 0;
        if (!text.empty() && (text[0] == '-' || text[0] == '+')) {
            negative = text[0] == '-';
            index = 1;
        }
        if (index == text.size()) {
            bad_value(what, "'" + std::string(text) + "' is not an integer");
        }
        uint128_t magnitude = 0;
        for (; index < text.size(); ++index) {
            const char ch = text[index];
            if (ch < '0' || ch > '9') {
                bad_value(what, "'" + std::string(text) + "' is not an integer");
            }
            const auto digit = static_cast<uint128_t>(ch - '0');
            if (magnitude > (kUint128Max - digit) / 10) {
                bad_value(what, "'" + std::string(text) + "' is out of range for " + type_name);
            }
            magnitude = magnitude * 10 + digit;
        }
        if (negative) {
            if (!Range::is_signed || magnitude > static_cast<uint128_t>(-(static_cast<int128_t>(Range::min) + 1)) + 1) {
                bad_value(what, "'" + std::string(text) + "' is out of range for " + type_name);
            }
            return static_cast<C>(-static_cast<int128_t>(magnitude - 1) - 1);
        }
        if (magnitude > static_cast<uint128_t>(Range::max)) {
            bad_value(what, "'" + std::string(text) + "' is out of range for " + type_name);
        }
        return static_cast<C>(magnitude);
    }
    bad_value(what, "expected an integer for " + type_name);
}

template <typename F>
[[nodiscard]] F float_from_json(const boost::json::value& json, const std::string_view what)
{
    if (!json.is_number()) {
        bad_value(what, "expected a number");
    }
    // Nearest representable value of the target type, as for a literal.
    return static_cast<F>(json.to_number<double>());
}

} // namespace detail

// Converts a JSON parameter value into Type's storage. Integers must be exact
// and in range; floating-point values take the nearest representable value;
// complex values are a number (imaginary part 0), [re, im] or {"re", "im"}.
template <SignalType Type>
[[nodiscard]] signal_storage_t<Type> signal_value_from_json(const boost::json::value& json, const std::string_view what)
{
    using value_type = signal_storage_t<Type>;
    constexpr auto kind = SignalTraits<Type>::kind;
    if constexpr (kind == SignalKind::SignedInteger || kind == SignalKind::UnsignedInteger) {
        return detail::integer_from_json<Type>(json, what);
    } else if constexpr (kind == SignalKind::Float) {
        return detail::float_from_json<value_type>(json, what);
    } else {
        const boost::json::value* re = &json;
        const boost::json::value* im = nullptr;
        if (const auto* array = json.if_array()) {
            if (array->size() != 2) {
                detail::bad_value(what, "a complex value is [re, im]");
            }
            re = &(*array)[0];
            im = &(*array)[1];
        } else if (const auto* object = json.if_object()) {
            re = object->if_contains("re");
            im = object->if_contains("im");
            if (!re) {
                detail::bad_value(what, "a complex value object needs \"re\"");
            }
        }
        if constexpr (kind == SignalKind::ComplexFloat) {
            using F = typename value_type::value_type;
            return value_type{detail::float_from_json<F>(*re, what), im ? detail::float_from_json<F>(*im, what) : F{}};
        } else {
            return value_type{detail::integer_from_json<Type>(*re, what),
                              im ? detail::integer_from_json<Type>(*im, what) : typename IntegerRange<Type>::component{}};
        }
    }
}

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SIGNAL_VALUES_HPP
