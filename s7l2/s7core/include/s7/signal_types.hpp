#ifndef S7CORE_INCLUDE_S7_SIGNAL_TYPES_HPP
#define S7CORE_INCLUDE_S7_SIGNAL_TYPES_HPP

// The S7 signal type catalog: one SignalType per wire type of the data-stream
// protocol (S7_Network_Wire_Protocol_v1.md section 8), each with its native
// C++ storage type. Values are never converted implicitly; a type change is an
// explicit Convert element.

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <stdfloat>
#include <string_view>
#include <type_traits>

namespace s7 {

__extension__ typedef __int128 int128_t;
__extension__ typedef unsigned __int128 uint128_t;

// 128-bit limits, defined here because std::numeric_limits and the std type
// traits know __int128 only in GNU mode, and plugins may build in strict mode.
inline constexpr uint128_t kUint128Max = ~static_cast<uint128_t>(0);
inline constexpr int128_t kInt128Max = static_cast<int128_t>(kUint128Max >> 1);
inline constexpr int128_t kInt128Min = -kInt128Max - 1;

template <typename T>
inline constexpr bool is_signed_integer_v = static_cast<T>(-1) < static_cast<T>(0);

// Complex integers (I/Q samples): two components of one integer type. Unlike
// std::complex, this is well defined for integer components.
template <typename T>
struct ComplexInt {
    T re{};
    T im{};

    friend constexpr bool operator==(const ComplexInt&, const ComplexInt&) = default;
};

enum class SignalKind : std::uint8_t {
    SignedInteger,
    UnsignedInteger,
    Float,
    ComplexFloat,
    ComplexInteger,  // signed components
    ComplexUnsigned, // CU8: offset-binary components, transport and Convert only
};

// X(name, storage type, kind, value bits per component, wire id)
// I12/U12 are stored in 16-bit integers restricted to the 12-bit range.
#define S7_SIGNAL_TYPES(X)                                                           \
    X(I8, std::int8_t, SignedInteger, 8, 0x0001)                                     \
    X(U8, std::uint8_t, UnsignedInteger, 8, 0x0002)                                  \
    X(I12, std::int16_t, SignedInteger, 12, 0x0003)                                  \
    X(U12, std::uint16_t, UnsignedInteger, 12, 0x0004)                               \
    X(I16, std::int16_t, SignedInteger, 16, 0x0005)                                  \
    X(U16, std::uint16_t, UnsignedInteger, 16, 0x0006)                               \
    X(I32, std::int32_t, SignedInteger, 32, 0x0007)                                  \
    X(U32, std::uint32_t, UnsignedInteger, 32, 0x0008)                               \
    X(I64, std::int64_t, SignedInteger, 64, 0x0009)                                  \
    X(U64, std::uint64_t, UnsignedInteger, 64, 0x000A)                               \
    X(I128, ::s7::int128_t, SignedInteger, 128, 0x000B)                              \
    X(U128, ::s7::uint128_t, UnsignedInteger, 128, 0x000C)                           \
    X(F16, std::float16_t, Float, 16, 0x000D)                                        \
    X(F32, float, Float, 32, 0x000E)                                                 \
    X(F64, double, Float, 64, 0x000F)                                                \
    X(F128, std::float128_t, Float, 128, 0x0010)                                     \
    X(C16, std::complex<std::float16_t>, ComplexFloat, 16, 0x0101)                   \
    X(C32, std::complex<float>, ComplexFloat, 32, 0x0102)                            \
    X(C64, std::complex<double>, ComplexFloat, 64, 0x0103)                           \
    X(C128, std::complex<std::float128_t>, ComplexFloat, 128, 0x0104)                \
    X(CI8, ::s7::ComplexInt<std::int8_t>, ComplexInteger, 8, 0x0201)                 \
    X(CU8, ::s7::ComplexInt<std::uint8_t>, ComplexUnsigned, 8, 0x0202)               \
    X(CI16, ::s7::ComplexInt<std::int16_t>, ComplexInteger, 16, 0x0203)              \
    X(CI32, ::s7::ComplexInt<std::int32_t>, ComplexInteger, 32, 0x0204)              \
    X(CI64, ::s7::ComplexInt<std::int64_t>, ComplexInteger, 64, 0x0205)              \
    X(CI128, ::s7::ComplexInt<::s7::int128_t>, ComplexInteger, 128, 0x0206)

// The enumerator value is the wire type id.
enum class SignalType : std::uint16_t {
#define S7_SIGNAL_TYPE_ENUM(name, storage, kind, bits, id) name = id,
    S7_SIGNAL_TYPES(S7_SIGNAL_TYPE_ENUM)
#undef S7_SIGNAL_TYPE_ENUM
};

inline constexpr std::array<SignalType, 26> kAllSignalTypes{
#define S7_SIGNAL_TYPE_LIST(name, storage, kind, bits, id) SignalType::name,
    S7_SIGNAL_TYPES(S7_SIGNAL_TYPE_LIST)
#undef S7_SIGNAL_TYPE_LIST
};

template <SignalType Type>
struct SignalTraits;

#define S7_SIGNAL_TYPE_TRAITS(name, storage, kind_, bits, id)                        \
    template <>                                                                      \
    struct SignalTraits<SignalType::name> {                                          \
        using type = storage;                                                        \
        static constexpr SignalType signal_type = SignalType::name;                  \
        static constexpr SignalKind kind = SignalKind::kind_;                        \
        static constexpr unsigned component_bits = bits;                             \
        static constexpr std::string_view name_view = #name;                         \
    };
S7_SIGNAL_TYPES(S7_SIGNAL_TYPE_TRAITS)
#undef S7_SIGNAL_TYPE_TRAITS

template <SignalType Type>
using signal_storage_t = typename SignalTraits<Type>::type;

struct SignalTypeInfo {
    SignalType type;
    SignalKind kind;
    unsigned component_bits; // value bits of one component (12 for I12)
    std::size_t storage_bytes;
    std::string_view name;

    [[nodiscard]] constexpr std::uint16_t wire_type_id() const { return static_cast<std::uint16_t>(type); }
    [[nodiscard]] constexpr bool is_integer() const
    {
        return kind == SignalKind::SignedInteger || kind == SignalKind::UnsignedInteger;
    }
    [[nodiscard]] constexpr bool is_complex() const
    {
        return kind == SignalKind::ComplexFloat || kind == SignalKind::ComplexInteger ||
               kind == SignalKind::ComplexUnsigned;
    }
    [[nodiscard]] constexpr bool is_floating() const
    {
        return kind == SignalKind::Float || kind == SignalKind::ComplexFloat;
    }
};

// Calls f.template operator()<SignalType::X>() for the runtime value type: the
// bridge from a runtime type to statically typed code.
template <typename F>
constexpr decltype(auto) visit_signal_type(const SignalType type, F&& f)
{
    switch (type) {
#define S7_SIGNAL_TYPE_VISIT(name, storage, kind, bits, id)                          \
    case SignalType::name:                                                           \
        return std::forward<F>(f).template operator()<SignalType::name>();
        S7_SIGNAL_TYPES(S7_SIGNAL_TYPE_VISIT)
#undef S7_SIGNAL_TYPE_VISIT
    }
    __builtin_unreachable();
}

[[nodiscard]] constexpr SignalTypeInfo signal_type_info(const SignalType type)
{
    return visit_signal_type(type, []<SignalType T>() {
        using Traits = SignalTraits<T>;
        return SignalTypeInfo{T, Traits::kind, Traits::component_bits, sizeof(typename Traits::type), Traits::name_view};
    });
}

[[nodiscard]] constexpr std::string_view signal_type_name(const SignalType type)
{
    return signal_type_info(type).name;
}

[[nodiscard]] constexpr std::optional<SignalType> signal_type_from_name(const std::string_view name)
{
    for (const auto type : kAllSignalTypes) {
        if (signal_type_name(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

// Parses a type name ("F64", "CI16", ...); throws std::invalid_argument otherwise.
[[nodiscard]] inline SignalType parse_signal_type(const std::string_view name)
{
    if (const auto type = signal_type_from_name(name)) {
        return *type;
    }
    std::string known;
    for (const auto type : kAllSignalTypes) {
        known += known.empty() ? "" : ", ";
        known += signal_type_name(type);
    }
    throw std::invalid_argument("Unknown signal type '" + std::string(name) + "' (known: " + known + ").");
}

[[nodiscard]] constexpr std::optional<SignalType> signal_type_from_wire_id(const std::uint16_t id)
{
    for (const auto type : kAllSignalTypes) {
        if (static_cast<std::uint16_t>(type) == id) {
            return type;
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool is_complex_signal_type(const SignalType type)
{
    return signal_type_info(type).is_complex();
}

// The type of one component of a complex type (C64 -> F64, CI16 -> I16,
// CU8 -> U8); real types map to themselves.
[[nodiscard]] constexpr SignalType component_signal_type(const SignalType type)
{
    switch (type) {
        case SignalType::C16: return SignalType::F16;
        case SignalType::C32: return SignalType::F32;
        case SignalType::C64: return SignalType::F64;
        case SignalType::C128: return SignalType::F128;
        case SignalType::CI8: return SignalType::I8;
        case SignalType::CU8: return SignalType::U8;
        case SignalType::CI16: return SignalType::I16;
        case SignalType::CI32: return SignalType::I32;
        case SignalType::CI64: return SignalType::I64;
        case SignalType::CI128: return SignalType::I128;
        default: return type;
    }
}

// The complex type whose components have this real type, if there is one.
[[nodiscard]] constexpr std::optional<SignalType> complex_signal_type_of(const SignalType component)
{
    for (const auto type : kAllSignalTypes) {
        if (is_complex_signal_type(type) && component_signal_type(type) == component) {
            return type;
        }
    }
    return std::nullopt;
}

template <typename T>
struct integer_component {
    using type = T;
};

template <typename T>
struct integer_component<ComplexInt<T>> {
    using type = T;
};

// Value range of an integer type (or of the components of a complex integer
// type). For I12/U12 this is the 12-bit range, narrower than the storage.
template <SignalType Type>
struct IntegerRange {
    using component = typename integer_component<signal_storage_t<Type>>::type;
    static constexpr unsigned bits = SignalTraits<Type>::component_bits;
    static constexpr bool is_signed = is_signed_integer_v<component>;
    static constexpr component min = !is_signed ? component{0}
        : bits == 128 ? static_cast<component>(kInt128Min)
                      : static_cast<component>(-(static_cast<int128_t>(1) << (bits - 1)));
    static constexpr component max = is_signed
        ? static_cast<component>(bits == 128 ? kInt128Max : (static_cast<int128_t>(1) << (bits - 1)) - 1)
        : static_cast<component>(bits == 128 ? kUint128Max : (static_cast<uint128_t>(1) << bits) - 1);
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SIGNAL_TYPES_HPP
