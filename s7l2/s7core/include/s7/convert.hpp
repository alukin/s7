#ifndef S7CORE_INCLUDE_S7_CONVERT_HPP
#define S7CORE_INCLUDE_S7_CONVERT_HPP

// The Convert element: the only way a signal changes type (there are no
// implicit conversions). See ELEMENT_DEV_GUIDE.md, "Type system".
//
// mode "value": the numeric value is kept (int16 1000 -> float 1000.0).
// mode "normalized": fixed-point scaling. A signed code c of b bits means
//   c / 2^(b-1) in [-1, 1); an unsigned code means c / 2^b in [0, 1); CU8
//   components are offset binary, (c - 128) / 128. Integer -> integer
//   rescales between bit widths of the same kind (shifting, rounded).
// rounding: "half_even" or "toward_zero", for results that are not exact.
// overflow: "saturate" (clamp to the target range; NaN -> 0) or "fault"
//   (throw, which stops the step).
// Complex types convert component-wise; real <-> complex is not a
// conversion (use ReImToComplex / ComplexToReIm).

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

#include "s7/element.hpp"
#include "s7/signal_types.hpp"

namespace s7 {

enum class ConvertMode : std::uint8_t { Value, Normalized };
enum class ConvertRounding : std::uint8_t { HalfEven, TowardZero };
enum class ConvertOverflow : std::uint8_t { Saturate, Fault };

struct ConvertOptions {
    ConvertMode mode{ConvertMode::Value};
    ConvertRounding rounding{ConvertRounding::HalfEven};
    ConvertOverflow overflow{ConvertOverflow::Saturate};
};

[[nodiscard]] ConvertMode convert_mode_from_name(std::string_view name);
[[nodiscard]] ConvertRounding convert_rounding_from_name(std::string_view name);
[[nodiscard]] ConvertOverflow convert_overflow_from_name(std::string_view name);

namespace detail {

// An integer as sign and magnitude, so that every pair of integer types
// (including U128 <-> I128) can be compared and converted exactly.
struct WideInteger {
    bool negative{};
    uint128_t magnitude{};
};

template <typename C>
[[nodiscard]] constexpr WideInteger to_wide(const C value)
{
    if constexpr (is_signed_integer_v<C>) {
        if (value < 0) {
            return {true, static_cast<uint128_t>(-(static_cast<int128_t>(value) + 1)) + 1};
        }
    }
    return {false, static_cast<uint128_t>(value)};
}

[[noreturn]] inline void conversion_fault(const std::string_view from, const std::string_view to, const char* reason)
{
    throw std::range_error("Convert " + std::string(from) + " -> " + std::string(to) + ": " + reason + ".");
}

// Exact wide integer -> component of To, applying the overflow rule.
template <SignalType To>
[[nodiscard]] auto integer_to_integer(const WideInteger value, const ConvertOverflow overflow, const std::string_view from)
    -> typename IntegerRange<To>::component
{
    using Range = IntegerRange<To>;
    using C = typename Range::component;
    const auto hi = to_wide(Range::max);
    const auto lo = to_wide(Range::min);
    const bool above = !value.negative && value.magnitude > hi.magnitude;
    const bool below = value.negative && (!lo.negative || value.magnitude > lo.magnitude);
    if (above || below) {
        if (overflow == ConvertOverflow::Fault) {
            conversion_fault(from, signal_type_name(To), "value out of range");
        }
        return above ? Range::max : Range::min;
    }
    if (value.negative) {
        return static_cast<C>(-static_cast<int128_t>(value.magnitude - 1) - 1);
    }
    return static_cast<C>(value.magnitude);
}

// Floating value -> component of To (rounding, then range and NaN rules).
template <SignalType To, typename F>
[[nodiscard]] auto float_to_integer(F value, const ConvertOptions& options, const std::string_view from)
    -> typename IntegerRange<To>::component
{
    using Range = IntegerRange<To>;
    if (std::isnan(value)) {
        if (options.overflow == ConvertOverflow::Fault) {
            conversion_fault(from, signal_type_name(To), "NaN has no integer value");
        }
        return typename Range::component{};
    }
    value = options.rounding == ConvertRounding::HalfEven ? std::nearbyint(value) : std::trunc(value);
    // Compare in the float type; the bounds 2^k are exact in every IEEE format
    // whose range covers them, otherwise the value cannot exceed them either.
    const auto upper = std::ldexp(F(1), static_cast<int>(Range::is_signed ? Range::bits - 1 : Range::bits)); // max + 1
    const auto lower = Range::is_signed ? -upper : F(0);
    if (!(value < upper) || value < lower) {
        if (options.overflow == ConvertOverflow::Fault) {
            conversion_fault(from, signal_type_name(To), "value out of range");
        }
        return value < lower ? Range::min : Range::max;
    }
    if (value < F(0)) {
        return static_cast<typename Range::component>(-static_cast<int128_t>(static_cast<uint128_t>(-value)));
    }
    return static_cast<typename Range::component>(static_cast<uint128_t>(value));
}

template <typename To, typename F>
[[nodiscard]] To float_to_float(const F value, const ConvertOverflow overflow, const std::string_view from,
                                const std::string_view to)
{
    const auto result = static_cast<To>(value);
    if (std::isinf(result) && !std::isinf(value)) {
        if (overflow == ConvertOverflow::Fault) {
            conversion_fault(from, to, "value out of range");
        }
        return std::copysign(std::numeric_limits<To>::max(), result);
    }
    return result;
}

// Divides an exact integer by 2^shift with the rounding rule.
[[nodiscard]] inline WideInteger shift_right_rounded(const WideInteger value, const unsigned shift, const ConvertRounding rounding)
{
    if (shift == 0) {
        return value;
    }
    auto quotient = value.magnitude >> shift;
    if (rounding == ConvertRounding::HalfEven) {
        const auto remainder = value.magnitude & ((static_cast<uint128_t>(1) << shift) - 1);
        const auto half = static_cast<uint128_t>(1) << (shift - 1);
        if (remainder > half || (remainder == half && (quotient & 1) != 0)) {
            ++quotient;
        }
    }
    return {value.negative && quotient != 0, quotient};
}

} // namespace detail

// Converts one real (component) value of type From to To.
template <SignalType From, SignalType To>
[[nodiscard]] auto convert_component(const signal_storage_t<component_signal_type(From)>& value,
                                     const ConvertOptions& options) -> signal_storage_t<component_signal_type(To)>
{
    constexpr auto from_part = component_signal_type(From);
    constexpr auto to_part = component_signal_type(To);
    constexpr auto from_kind = signal_type_info(from_part).kind;
    constexpr auto to_kind = signal_type_info(to_part).kind;
    constexpr bool from_integer = from_kind == SignalKind::SignedInteger || from_kind == SignalKind::UnsignedInteger;
    constexpr bool to_integer = to_kind == SignalKind::SignedInteger || to_kind == SignalKind::UnsignedInteger;
    // CU8 components are offset binary: in normalized mode they act as signed 8-bit codes.
    constexpr bool from_offset = signal_type_info(From).kind == SignalKind::ComplexUnsigned;
    constexpr bool to_offset = signal_type_info(To).kind == SignalKind::ComplexUnsigned;
    using ToC = signal_storage_t<to_part>;
    const auto from_name = signal_type_name(From);
    const auto to_name = signal_type_name(To);
    const bool normalized = options.mode == ConvertMode::Normalized;

    if constexpr (from_integer && to_integer) {
        auto wide = detail::to_wide(value);
        if (!normalized) {
            return detail::integer_to_integer<to_part>(wide, options.overflow, from_name);
        }
        if (from_offset) {
            wide = detail::to_wide(static_cast<int128_t>(value) - 128);
        }
        constexpr bool from_signed = from_offset || IntegerRange<from_part>::is_signed;
        constexpr bool to_signed = to_offset || IntegerRange<to_part>::is_signed;
        if constexpr (from_signed != to_signed) {
            throw std::invalid_argument("Convert " + std::string(from_name) + " -> " + std::string(to_name) +
                                        ": normalized mode needs both signed or both unsigned codes.");
        } else {
            constexpr unsigned from_bits = IntegerRange<from_part>::bits;
            constexpr unsigned to_bits = IntegerRange<to_part>::bits;
            if constexpr (to_bits >= from_bits) {
                wide.magnitude <<= (to_bits - from_bits);
            } else {
                wide = detail::shift_right_rounded(wide, from_bits - to_bits, options.rounding);
            }
            if constexpr (to_offset) {
                // signed 8-bit code -> offset binary
                const auto code = detail::integer_to_integer<SignalType::I8>(wide, options.overflow, from_name);
                return static_cast<ToC>(code + 128);
            } else {
                return detail::integer_to_integer<to_part>(wide, options.overflow, from_name);
            }
        }
    } else if constexpr (from_integer) {
        // integer -> float
        auto code = static_cast<ToC>(from_offset && normalized ? static_cast<int128_t>(value) - 128 : value);
        if (normalized) {
            constexpr unsigned bits = IntegerRange<from_part>::bits;
            constexpr bool is_signed = from_offset || IntegerRange<from_part>::is_signed;
            code = std::ldexp(code, -static_cast<int>(is_signed ? bits - 1 : bits));
        }
        if (std::isinf(code)) {
            if (options.overflow == ConvertOverflow::Fault) {
                detail::conversion_fault(from_name, to_name, "value out of range");
            }
            return std::copysign(std::numeric_limits<ToC>::max(), code);
        }
        return code;
    } else if constexpr (to_integer) {
        // float -> integer
        auto scaled = value;
        if (normalized) {
            constexpr unsigned bits = IntegerRange<to_part>::bits;
            constexpr bool is_signed = to_offset || IntegerRange<to_part>::is_signed;
            scaled = std::ldexp(value, static_cast<int>(is_signed ? bits - 1 : bits));
        }
        if constexpr (to_offset) {
            const auto code = detail::float_to_integer<SignalType::I8>(scaled, options, from_name);
            return static_cast<ToC>(code + 128);
        } else {
            return detail::float_to_integer<to_part>(scaled, options, from_name);
        }
    } else {
        return detail::float_to_float<ToC>(value, options.overflow, from_name, to_name);
    }
}

// Converts a whole value; complex types component-wise.
template <SignalType From, SignalType To>
[[nodiscard]] signal_storage_t<To> convert_value(const signal_storage_t<From>& value, const ConvertOptions& options)
{
    constexpr auto from_kind = signal_type_info(From).kind;
    constexpr auto to_kind = signal_type_info(To).kind;
    static_assert(signal_type_info(From).is_complex() == signal_type_info(To).is_complex(),
                  "real <-> complex is not a conversion");
    if constexpr (!signal_type_info(From).is_complex()) {
        return convert_component<From, To>(value, options);
    } else {
        auto parts = [](const signal_storage_t<From>& v) {
            if constexpr (from_kind == SignalKind::ComplexFloat) {
                return std::pair{v.real(), v.imag()};
            } else {
                return std::pair{v.re, v.im};
            }
        }(value);
        const auto re = convert_component<From, To>(parts.first, options);
        const auto im = convert_component<From, To>(parts.second, options);
        if constexpr (to_kind == SignalKind::ComplexFloat) {
            return signal_storage_t<To>{re, im};
        } else {
            return signal_storage_t<To>{re, im};
        }
    }
}

template <SignalType From, SignalType To>
class ConvertT final : public Element {
public:
    using from_type = signal_storage_t<From>;
    using to_type = signal_storage_t<To>;

    explicit ConvertT(const ConvertOptions options)
        : Element("Convert")
        , options_(options)
    {
        register_input(0, port_names::in, From, nullptr);
        register_output(0, port_names::out, To, &output_value_);
    }

    void step() override
    {
        output_value_ = convert_value<From, To>(read_input<from_type>(0), options_);
    }

private:
    ConvertOptions options_;
    to_type output_value_{};
};

// Creates the Convert element for a resolved type pair; throws for real <-> complex.
[[nodiscard]] std::unique_ptr<Element> make_convert(SignalType from, SignalType to, const ConvertOptions& options);

} // namespace s7

#endif // S7CORE_INCLUDE_S7_CONVERT_HPP
