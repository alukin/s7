// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_SIGNAL_ARITHMETIC_HPP
#define S7CORE_INCLUDE_S7_SIGNAL_ARITHMETIC_HPP

// Arithmetic on signal values with an explicit integer overflow rule.
//
// Integer results are computed exactly and then mapped into the type's value
// range (for I12/U12 the 12-bit range) by the element's Overflow rule:
// saturate to the nearest bound, or wrap modulo 2^bits (two's complement).
// Results are exact before the rule is applied for components up to 64 bits.
// For 128-bit components each operation is checked: an overflowing step
// saturates (or wraps, as the hardware does) before the next one. Floating-point types follow IEEE 754 and ignore the rule.

#include <stdexcept>
#include <string>
#include <string_view>

#include "s7/signal_types.hpp"

namespace s7 {

enum class Overflow : std::uint8_t {
    Saturate,
    Wrap,
};

[[nodiscard]] inline Overflow overflow_from_name(const std::string_view name)
{
    if (name == "saturate") {
        return Overflow::Saturate;
    }
    if (name == "wrap") {
        return Overflow::Wrap;
    }
    throw std::invalid_argument("overflow must be 'saturate' or 'wrap', got '" + std::string(name) + "'.");
}

namespace detail {

// Maps an exact value (held in 128 bits) into the range of Type.
template <SignalType Type>
[[nodiscard]] constexpr auto narrow_integer(const int128_t value, const Overflow overflow)
    -> typename IntegerRange<Type>::component
{
    using Range = IntegerRange<Type>;
    using C = typename Range::component;
    const auto lo = static_cast<int128_t>(Range::min);
    const auto hi = static_cast<int128_t>(Range::max);
    if (value >= lo && value <= hi) {
        return static_cast<C>(value);
    }
    if (overflow == Overflow::Saturate) {
        return value < lo ? Range::min : Range::max;
    }
    // Wrap modulo 2^bits, then reinterpret in the signed or unsigned range.
    const auto modulus = static_cast<uint128_t>(1) << Range::bits;
    auto wrapped = static_cast<uint128_t>(value) & (modulus - 1);
    if (Range::is_signed && (wrapped >> (Range::bits - 1)) != 0) {
        return static_cast<C>(static_cast<int128_t>(wrapped) - static_cast<int128_t>(modulus));
    }
    return static_cast<C>(wrapped);
}

template <SignalType Type>
[[nodiscard]] constexpr auto saturate_signed_overflow(const bool negative) -> typename IntegerRange<Type>::component
{
    return negative ? IntegerRange<Type>::min : IntegerRange<Type>::max;
}

} // namespace detail

// a + b for integer types (also used per component of complex integers).
template <SignalType Type>
[[nodiscard]] constexpr auto integer_add(const typename IntegerRange<Type>::component a,
                                         const typename IntegerRange<Type>::component b,
                                         const Overflow overflow) -> typename IntegerRange<Type>::component
{
    using Range = IntegerRange<Type>;
    using C = typename Range::component;
    if constexpr (Range::bits < 128) {
        return detail::narrow_integer<Type>(static_cast<int128_t>(a) + static_cast<int128_t>(b), overflow);
    } else {
        C result{};
        if (__builtin_add_overflow(a, b, &result) && overflow == Overflow::Saturate) {
            if constexpr (Range::is_signed) {
                return detail::saturate_signed_overflow<Type>(a < 0);
            } else {
                return Range::max;
            }
        }
        return result; // wrapped by the hardware when it overflowed
    }
}

template <SignalType Type>
[[nodiscard]] constexpr auto integer_subtract(const typename IntegerRange<Type>::component a,
                                              const typename IntegerRange<Type>::component b,
                                              const Overflow overflow) -> typename IntegerRange<Type>::component
{
    using Range = IntegerRange<Type>;
    using C = typename Range::component;
    if constexpr (Range::bits < 128) {
        return detail::narrow_integer<Type>(static_cast<int128_t>(a) - static_cast<int128_t>(b), overflow);
    } else {
        C result{};
        if (__builtin_sub_overflow(a, b, &result) && overflow == Overflow::Saturate) {
            if constexpr (Range::is_signed) {
                return detail::saturate_signed_overflow<Type>(a < 0);
            } else {
                return Range::min;
            }
        }
        return result;
    }
}

template <SignalType Type>
[[nodiscard]] constexpr auto integer_multiply(const typename IntegerRange<Type>::component a,
                                              const typename IntegerRange<Type>::component b,
                                              const Overflow overflow) -> typename IntegerRange<Type>::component
{
    using Range = IntegerRange<Type>;
    using C = typename Range::component;
    if constexpr (Range::bits < 64 || (Range::bits == 64 && Range::is_signed)) {
        // |a * b| <= 2^126: exact in 128 bits.
        return detail::narrow_integer<Type>(static_cast<int128_t>(a) * static_cast<int128_t>(b), overflow);
    } else if constexpr (Range::bits == 64) {
        // U64: the product fits unsigned 128 bits.
        const auto product = static_cast<uint128_t>(a) * static_cast<uint128_t>(b);
        if (product <= Range::max) {
            return static_cast<C>(product);
        }
        return overflow == Overflow::Saturate ? Range::max : static_cast<C>(product);
    } else {
        C result{};
        if (__builtin_mul_overflow(a, b, &result) && overflow == Overflow::Saturate) {
            if constexpr (Range::is_signed) {
                return detail::saturate_signed_overflow<Type>((a < 0) != (b < 0));
            } else {
                return Range::max;
            }
        }
        return result;
    }
}

namespace detail {

// x + y or x - y of exact 128-bit products, mapped into Type. On 128-bit
// overflow the true value lies beyond every component range of up to 64
// bits, so saturation goes to the bound of its sign; wrap keeps the low bits,
// which unsigned 128-bit arithmetic computes exactly.
template <SignalType Type>
[[nodiscard]] constexpr auto combine_products(const int128_t x, const int128_t y, const bool subtract,
                                              const Overflow overflow) -> typename IntegerRange<Type>::component
{
    using Range = IntegerRange<Type>;
    int128_t exact{};
    const bool overflowed = subtract ? __builtin_sub_overflow(x, y, &exact) : __builtin_add_overflow(x, y, &exact);
    if (!overflowed) {
        return narrow_integer<Type>(exact, overflow);
    }
    if (overflow == Overflow::Saturate) {
        const bool positive = subtract ? x > y : x > 0;
        return positive ? Range::max : Range::min;
    }
    const auto low = subtract ? static_cast<uint128_t>(x) - static_cast<uint128_t>(y)
                              : static_cast<uint128_t>(x) + static_cast<uint128_t>(y);
    return narrow_integer<Type>(static_cast<int128_t>(low), Overflow::Wrap);
}

} // namespace detail

// Complex integer arithmetic (signed components). Addition is component-wise.
// The product (a + bi)(c + di) = (ac - bd) + (ad + bc)i is exact for
// components up to 64 bits (products are exact in 128 bits); for 128-bit
// components each step is a checked operation in the component type.
template <SignalType Type>
[[nodiscard]] constexpr signal_storage_t<Type> complex_integer_add(const signal_storage_t<Type>& a,
                                                                   const signal_storage_t<Type>& b,
                                                                   const Overflow overflow)
{
    return {integer_add<Type>(a.re, b.re, overflow), integer_add<Type>(a.im, b.im, overflow)};
}

template <SignalType Type>
[[nodiscard]] constexpr signal_storage_t<Type> complex_integer_multiply(const signal_storage_t<Type>& a,
                                                                        const signal_storage_t<Type>& b,
                                                                        const Overflow overflow)
{
    static_assert(SignalTraits<Type>::kind == SignalKind::ComplexInteger, "signed complex integers only");
    using Range = IntegerRange<Type>;
    if constexpr (Range::bits <= 64) {
        const auto ac = static_cast<int128_t>(a.re) * b.re;
        const auto bd = static_cast<int128_t>(a.im) * b.im;
        const auto ad = static_cast<int128_t>(a.re) * b.im;
        const auto bc = static_cast<int128_t>(a.im) * b.re;
        return {detail::combine_products<Type>(ac, bd, true, overflow),
                detail::combine_products<Type>(ad, bc, false, overflow)};
    } else {
        const auto re = integer_subtract<Type>(integer_multiply<Type>(a.re, b.re, overflow),
                                               integer_multiply<Type>(a.im, b.im, overflow), overflow);
        const auto im = integer_add<Type>(integer_multiply<Type>(a.re, b.im, overflow),
                                          integer_multiply<Type>(a.im, b.re, overflow), overflow);
        return {re, im};
    }
}

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SIGNAL_ARITHMETIC_HPP
