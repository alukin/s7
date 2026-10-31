// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_SIGNAL_VALUES_HPP
#define S7CORE_INCLUDE_S7_SIGNAL_VALUES_HPP

// Typed values for generic elements: uniform arithmetic over every signal
// type (with the element's integer Overflow rule) and exact conversion of
// JSON parameter values into a type's storage.

#include <boost/json.hpp>

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

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

// Whether every value of From is exactly representable in To (used for an
// element's accumulator_type). Real -> complex and CU8 never widen.
[[nodiscard]] constexpr bool widens_exactly(const SignalType from, const SignalType to)
{
    if (from == to) {
        return true;
    }
    const auto f = signal_type_info(from);
    const auto t = signal_type_info(to);
    if (f.is_complex() != t.is_complex() || f.kind == SignalKind::ComplexUnsigned || t.kind == SignalKind::ComplexUnsigned) {
        return false;
    }
    const auto fc = signal_type_info(component_signal_type(from));
    const auto tc = signal_type_info(component_signal_type(to));
    const auto mantissa_digits = [](const unsigned bits) -> unsigned {
        switch (bits) {
            case 16: return 11;
            case 32: return 24;
            case 64: return 53;
            default: return 113;
        }
    };
    const bool f_int = fc.is_integer();
    const bool t_int = tc.is_integer();
    const bool f_signed = fc.kind == SignalKind::SignedInteger;
    const bool t_signed = tc.kind == SignalKind::SignedInteger;
    if (f_int && t_int) {
        if (f_signed) {
            return t_signed && tc.component_bits >= fc.component_bits;
        }
        return t_signed ? tc.component_bits > fc.component_bits : tc.component_bits >= fc.component_bits;
    }
    if (f_int) {
        return mantissa_digits(tc.component_bits) >= (f_signed ? fc.component_bits - 1 : fc.component_bits);
    }
    if (t_int) {
        return false;
    }
    return tc.component_bits >= fc.component_bits;
}

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

[[nodiscard]] inline std::string format_number(const double value)
{
    std::ostringstream text;
    text << std::setprecision(6) << value;
    return text.str();
}

// The nearest value of F, as for a literal, rounded once: JSON integers
// convert directly (exact in F128 up to 2^113), other numbers from the
// double the JSON parser read. A value whose nearest F is infinite, or a
// nonzero value that becomes 0, is rejected: either would silently change
// the parameter's meaning.
template <typename F>
[[nodiscard]] F float_from_json(const boost::json::value& json, const std::string_view what, const std::string_view type)
{
    if (!json.is_number()) {
        bad_value(what, "expected a number");
    }
    const double number = json.to_number<double>();
    if (!std::isfinite(number)) { // a literal beyond double's range parses as infinity
        bad_value(what, "is not a finite number");
    }
    // Integers go through a type that holds every int64/uint64 exactly, so F
    // still rounds once. Not directly into F16/F32: GCC 15/16 fold isinf() of
    // an integer converted to _Float16 to false although the conversion can
    // overflow (65520 -> inf), so the range check below would never fire.
    using Exact = std::conditional_t<(sizeof(F) >= sizeof(double)), F, float128_t>;
    const F value = json.is_int64()    ? static_cast<F>(static_cast<Exact>(json.get_int64()))
                    : json.is_uint64() ? static_cast<F>(static_cast<Exact>(json.get_uint64()))
                                       : static_cast<F>(number);
    if (math::isinf(value)) {
        bad_value(what, format_number(number) + " is out of range for " + std::string(type) + " (largest finite " +
                            format_number(static_cast<double>(std::numeric_limits<F>::max())) + ")");
    }
    if (value == F(0) && number != 0) {
        bad_value(what, format_number(number) + " underflows to 0 in " + std::string(type) + " (smallest positive " +
                            format_number(static_cast<double>(std::numeric_limits<F>::denorm_min())) + ")");
    }
    return value;
}

} // namespace detail

// Converts a JSON parameter value into Type's storage. Integers must be exact
// and in range; floating-point values take the nearest representable value,
// which must be finite and, for a nonzero value, nonzero (JSON numbers other
// than integers have double precision, also for F128); complex values are a
// number (imaginary part 0), [re, im] or {"re", "im"}.
template <SignalType Type>
[[nodiscard]] signal_storage_t<Type> signal_value_from_json(const boost::json::value& json, const std::string_view what)
{
    using value_type = signal_storage_t<Type>;
    constexpr auto kind = SignalTraits<Type>::kind;
    if constexpr (kind == SignalKind::SignedInteger || kind == SignalKind::UnsignedInteger) {
        return detail::integer_from_json<Type>(json, what);
    } else if constexpr (kind == SignalKind::Float) {
        return detail::float_from_json<value_type>(json, what, signal_type_name(Type));
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
            const auto type = signal_type_name(Type);
            return value_type{detail::float_from_json<F>(*re, what, type), im ? detail::float_from_json<F>(*im, what, type) : F{}};
        } else {
            return value_type{detail::integer_from_json<Type>(*re, what),
                              im ? detail::integer_from_json<Type>(*im, what) : typename IntegerRange<Type>::component{}};
        }
    }
}

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SIGNAL_VALUES_HPP
