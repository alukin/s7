#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "s7/signal_arithmetic.hpp"
#include "s7/signal_types.hpp"

namespace {

using s7::Overflow;
using s7::SignalType;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

// Checked at compile time as well.
static_assert(s7::signal_type_info(SignalType::F64).wire_type_id() == 0x000F);
static_assert(s7::signal_type_info(SignalType::CI16).storage_bytes == 4);
static_assert(s7::IntegerRange<SignalType::I12>::min == -2048 && s7::IntegerRange<SignalType::I12>::max == 2047);
static_assert(s7::integer_add<SignalType::I8>(100, 100, Overflow::Saturate) == 127);
static_assert(s7::kInt128Min < 0 && s7::kInt128Max > 0 && s7::kInt128Min + s7::kInt128Max == -1);

void test_catalog()
{
    expect(s7::kAllSignalTypes.size() == 26, "26 types");
    for (const auto type : s7::kAllSignalTypes) {
        const auto info = s7::signal_type_info(type);
        expect(s7::signal_type_from_name(info.name) == type, "name round trip " + std::string(info.name));
        expect(s7::signal_type_from_wire_id(info.wire_type_id()) == type, "wire id round trip " + std::string(info.name));
        if (info.is_complex()) {
            expect(s7::complex_signal_type_of(s7::component_signal_type(type)) == type, "component round trip");
            expect(info.storage_bytes == 2 * s7::signal_type_info(s7::component_signal_type(type)).storage_bytes,
                   "complex storage is two components");
        }
    }
    expect(!s7::signal_type_from_name("DOUBLE"), "old names are gone");
    expect(s7::signal_type_info(SignalType::I12).storage_bytes == 2 && s7::signal_type_info(SignalType::I12).component_bits == 12,
           "I12 stored in 16 bits");
    expect(s7::signal_type_info(SignalType::F128).storage_bytes == 16, "binary128 storage");
    expect(s7::signal_type_info(SignalType::CI128).storage_bytes == 32, "CI128 storage");
    expect(s7::signal_type_info(SignalType::C16).is_floating() && !s7::signal_type_info(SignalType::CU8).is_floating(), "kinds");

    const auto bytes = s7::visit_signal_type(SignalType::C128, []<SignalType T>() { return sizeof(s7::signal_storage_t<T>); });
    expect(bytes == 32, "dispatch reaches the static type");
}

void test_integer_rules()
{
    using s7::integer_add;
    using s7::integer_multiply;
    using s7::integer_subtract;
    constexpr auto S = Overflow::Saturate;
    constexpr auto W = Overflow::Wrap;

    expect(integer_add<SignalType::I8>(100, 100, W) == -56, "I8 wrap");
    expect(integer_add<SignalType::U8>(200, 100, S) == 255 && integer_add<SignalType::U8>(200, 100, W) == 44, "U8");
    expect(integer_subtract<SignalType::U8>(10, 20, S) == 0 && integer_subtract<SignalType::U8>(10, 20, W) == 246, "U8 sub");
    expect(integer_add<SignalType::I12>(2000, 100, S) == 2047, "I12 saturates at 12 bits");
    expect(integer_add<SignalType::I12>(2000, 100, W) == 2100 - 4096, "I12 wraps at 12 bits");
    expect(integer_add<SignalType::U12>(4000, 200, S) == 4095 && integer_add<SignalType::U12>(4000, 200, W) == 104, "U12");
    expect(integer_multiply<SignalType::I16>(300, 300, S) == 32767 && integer_multiply<SignalType::I16>(300, 300, W) == 24464, "I16 mul");

    constexpr auto i64min = s7::IntegerRange<SignalType::I64>::min;
    expect(integer_subtract<SignalType::I64>(i64min, 1, S) == i64min, "I64 sub saturates");
    expect(integer_multiply<SignalType::I64>(i64min, -1, S) == s7::IntegerRange<SignalType::I64>::max, "I64 min * -1 saturates");
    expect(integer_multiply<SignalType::I64>(i64min, -1, W) == i64min, "I64 min * -1 wraps");

    constexpr auto u64max = s7::IntegerRange<SignalType::U64>::max;
    expect(integer_multiply<SignalType::U64>(u64max, 2, S) == u64max, "U64 mul saturates");
    expect(integer_multiply<SignalType::U64>(u64max, 2, W) == u64max - 1, "U64 mul wraps");

    expect(integer_add<SignalType::I128>(s7::kInt128Max, 1, S) == s7::kInt128Max, "I128 add saturates");
    expect(integer_add<SignalType::I128>(s7::kInt128Max, 1, W) == s7::kInt128Min, "I128 add wraps");
    expect(integer_add<SignalType::U128>(s7::kUint128Max, 1, S) == s7::kUint128Max, "U128 add saturates");
    expect(integer_add<SignalType::U128>(s7::kUint128Max, 1, W) == 0, "U128 add wraps");
    expect(integer_multiply<SignalType::I128>(s7::kInt128Max, -2, S) == s7::kInt128Min, "I128 mul saturates negative");
}

void test_complex_integer_rules()
{
    using CI8 = s7::signal_storage_t<SignalType::CI8>;
    const CI8 a{100, 100};
    // (100 + 100i)^2 = 0 + 20000i
    expect(s7::complex_integer_multiply<SignalType::CI8>(a, a, Overflow::Saturate) == CI8{0, 127}, "CI8 mul saturates");
    expect(s7::complex_integer_multiply<SignalType::CI8>(a, a, Overflow::Wrap) == CI8{0, 32}, "CI8 mul wraps");
    expect(s7::complex_integer_add<SignalType::CI8>(a, a, Overflow::Saturate) == CI8{127, 127}, "CI8 add saturates");

    using CI64 = s7::signal_storage_t<SignalType::CI64>;
    constexpr auto m = s7::IntegerRange<SignalType::CI64>::min;
    const CI64 corner{m, m};
    // (m + mi)^2 = (m^2 - m^2) + 2m^2 i = 0 + 2^127 i: the imaginary part overflows 128 bits.
    const auto saturated = s7::complex_integer_multiply<SignalType::CI64>(corner, corner, Overflow::Saturate);
    expect(saturated == CI64{0, s7::IntegerRange<SignalType::CI64>::max}, "CI64 corner saturates exactly");
    const auto wrapped = s7::complex_integer_multiply<SignalType::CI64>(corner, corner, Overflow::Wrap);
    expect(wrapped == CI64{0, 0}, "CI64 corner wraps modulo 2^64");

    using CI16 = s7::signal_storage_t<SignalType::CI16>;
    expect(s7::complex_integer_multiply<SignalType::CI16>(CI16{3, 4}, CI16{1, -2}, Overflow::Saturate) == CI16{11, -2},
           "CI16 exact product");
}

// F16/F128 limits and math: native std types on GCC, builtins plus
// s7/float_types.hpp on Clang (where numeric_limits<_Float16> is otherwise 0).
void test_extended_floats()
{
    using F16 = s7::float16_t;
    using F128 = s7::float128_t;
    static_assert(sizeof(F16) == 2 && sizeof(F128) == 16);
    static_assert(std::numeric_limits<F16>::is_specialized && std::numeric_limits<F16>::digits == 11);
    static_assert(std::numeric_limits<F16>::max() == F16(65504) && std::numeric_limits<F16>::lowest() == F16(-65504));
    static_assert(std::numeric_limits<F16>::epsilon() == F16(1) / F16(1024));
    static_assert(std::numeric_limits<F128>::is_specialized && std::numeric_limits<F128>::digits == 113);

    expect(s7::math::ldexp(F16(1), 10) == F16(1024), "F16 ldexp");
    expect(s7::math::isinf(F16(65504) * F16(2)), "F16 overflows to infinity");
    expect(s7::math::isnan(std::numeric_limits<F16>::quiet_NaN()), "F16 NaN");
    expect(s7::math::copysign(F16(2), F16(-1)) == F16(-2), "F16 copysign");
    expect(s7::math::nearbyint(F16(2.5)) == F16(2) && s7::math::trunc(F16(-2.5)) == F16(-2), "F16 rounding");
    expect(s7::math::abs(std::complex<F16>{3, 4}) == F16(5), "C16 abs");
    expect(s7::math::abs(s7::math::sin(s7::math::pi_v<F16>)) < F16(0.01), "F16 sin(pi)");

    // 2^100 + 1 needs 101 significant bits: exact in binary128 only.
    const auto big = s7::math::ldexp(F128(1), 100);
    expect(big + F128(1) != big && s7::math::trunc(big + F128(0.5)) == big, "F128 ldexp, precision and trunc");
    expect(s7::math::isinf(std::numeric_limits<F128>::infinity()) && s7::math::isnan(std::numeric_limits<F128>::quiet_NaN()),
           "F128 infinity and NaN");
    expect(s7::math::abs(std::complex<F128>{3, 4}) == F128(5), "C128 abs");
    expect(s7::math::abs(s7::math::sin(s7::math::pi_v<F128>)) < F128(1e-30), "F128 sin(pi) at binary128 precision");

    // Arithmetic and conversions at run time, not folded: on MinGW these go
    // through s7core/src/windows_f128_abi.cpp.
    volatile int i = -3;
    volatile unsigned u = 5;
    volatile long long ll = -(1LL << 40);
    volatile unsigned long long ull = 1ULL << 63;
    __extension__ volatile __int128 i128 = -(static_cast<__int128>(1) << 100);
    __extension__ volatile unsigned __int128 u128 = static_cast<unsigned __int128>(1) << 127;
    volatile float f = 0.25F;
    volatile double d = 0.125;
    volatile long double ld = 0.0625L;
    volatile F16 h = F16(1.5);
    const F128 a = F128(i);
    const F128 b = F128(u);
    expect(a + b == F128(2) && a - b == F128(-8) && a * b == F128(-15) && F128(-15) / b == a, "F128 + - * /");
    expect(-a == F128(3), "F128 negation");
    expect(F128(ll) == -s7::math::ldexp(F128(1), 40) && F128(ull) == s7::math::ldexp(F128(1), 63), "F128 from 64-bit integers");
    expect(F128(i128) == -big && F128(u128) == s7::math::ldexp(F128(1), 127), "F128 from 128-bit integers");
    expect(F128(f) + F128(d) + F128(ld) == F128(0.4375) && F128(F16(h)) == F128(1.5), "F128 from float, double, long double and F16");
    expect(static_cast<double>(a / b) == -0.6 && static_cast<long long>(F128(ll)) == ll &&
               (__extension__ static_cast<__int128>(F128(i128)) == i128) && static_cast<F16>(F128(h)) == F16(1.5) &&
               static_cast<long double>(F128(ld)) == 0.0625L,
           "F128 to double, integers, F16 and long double");
}

} // namespace

int main()
try {
    test_catalog();
    test_integer_rules();
    test_complex_integer_rules();
    test_extended_floats();
    expect(s7::overflow_from_name("wrap") == Overflow::Wrap, "overflow names");
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7core signal types smoke: " << ex.what() << '\n';
    return 1;
}
