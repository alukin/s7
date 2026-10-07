#ifndef S7CORE_INCLUDE_S7_FLOAT_TYPES_HPP
#define S7CORE_INCLUDE_S7_FLOAT_TYPES_HPP

// The extended floating types of F16/F128 (and C16/C128), and the math S7
// does on any floating signal type.
//
// GCC provides std::float16_t and std::float128_t with full library support.
// Clang (22) with libstdc++ does not: libstdc++ declares them only when the
// compiler defines __STDCPP_FLOAT16_T__ / __STDCPP_FLOAT128_T__, which Clang
// does not. There, s7::float16_t is the builtin _Float16 and s7::float128_t is
// __float128, and this header supplies what the library lacks for them:
//   - std::numeric_limits<_Float16> (otherwise unspecialized: max() == 0),
//   - <cmath> functions (otherwise ambiguous calls), in s7::math,
//   - std::numbers constants for _Float16, in s7::math.
// libstdc++ already treats __float128 as a floating-point type (traits,
// numeric_limits, std::numbers, std::complex).
//
// Code that does math on a signal value calls s7::math::f(x), never std::f(x).

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdfloat>

#if defined(__STDCPP_FLOAT16_T__) && defined(__STDCPP_FLOAT128_T__)
#define S7_NATIVE_EXTENDED_FLOATS 1
#elif !defined(__clang__) || !defined(__GLIBCXX__) || !defined(__FLT16_MAX__) || !defined(__SIZEOF_FLOAT128__)
#error "S7 needs std::float16_t and std::float128_t (GCC 15+), or Clang 22+ with libstdc++ on a target with _Float16 and __float128."
#endif

namespace s7 {

#if defined(S7_NATIVE_EXTENDED_FLOATS)
using float16_t = std::float16_t;
using float128_t = std::float128_t;
#else
using float16_t = _Float16;
__extension__ typedef __float128 float128_t;
#endif

} // namespace s7

#if !defined(S7_NATIVE_EXTENDED_FLOATS)
// Mirrors libstdc++'s specialization for std::float16_t under GCC.
template <>
struct std::numeric_limits<_Float16> {
    static constexpr bool is_specialized = true;
    static constexpr _Float16 min() noexcept { return __FLT16_MIN__; }
    static constexpr _Float16 max() noexcept { return __FLT16_MAX__; }
    static constexpr _Float16 lowest() noexcept { return -__FLT16_MAX__; }
    static constexpr int digits = __FLT16_MANT_DIG__;
    static constexpr int digits10 = __FLT16_DIG__;
    static constexpr int max_digits10 = __FLT16_DECIMAL_DIG__;
    static constexpr bool is_signed = true;
    static constexpr bool is_integer = false;
    static constexpr bool is_exact = false;
    static constexpr int radix = 2;
    static constexpr _Float16 epsilon() noexcept { return __FLT16_EPSILON__; }
    static constexpr _Float16 round_error() noexcept { return 0.5F16; }
    static constexpr int min_exponent = __FLT16_MIN_EXP__;
    static constexpr int min_exponent10 = __FLT16_MIN_10_EXP__;
    static constexpr int max_exponent = __FLT16_MAX_EXP__;
    static constexpr int max_exponent10 = __FLT16_MAX_10_EXP__;
    static constexpr bool has_infinity = __FLT16_HAS_INFINITY__;
    static constexpr bool has_quiet_NaN = __FLT16_HAS_QUIET_NAN__;
    static constexpr bool has_signaling_NaN = has_quiet_NaN;
    static constexpr float_denorm_style has_denorm = denorm_present;
    static constexpr bool has_denorm_loss = false;
    static constexpr _Float16 infinity() noexcept { return __builtin_inff16(); }
    static constexpr _Float16 quiet_NaN() noexcept { return __builtin_nanf16(""); }
    static constexpr _Float16 signaling_NaN() noexcept { return __builtin_nansf16(""); }
    static constexpr _Float16 denorm_min() noexcept { return __FLT16_DENORM_MIN__; }
    static constexpr bool is_iec559 = has_infinity && has_quiet_NaN && has_denorm == denorm_present;
    static constexpr bool is_bounded = true;
    static constexpr bool is_modulo = false;
    static constexpr bool traps = false;
    static constexpr bool tinyness_before = false;
    static constexpr float_round_style round_style = round_to_nearest;
};
#endif

namespace s7::math {

using std::abs;
using std::arg;
using std::atan2;
using std::copysign;
using std::fmod;
using std::isinf;
using std::isnan;
using std::ldexp;
using std::nearbyint;
using std::sin;
using std::sqrt;
using std::trunc;

template <typename T>
inline constexpr T pi_v = std::numbers::pi_v<T>;

#if !defined(S7_NATIVE_EXTENDED_FLOATS)
template <>
inline constexpr float16_t pi_v<float16_t> = static_cast<float16_t>(std::numbers::pi_v<float>);
// Without GNU extensions libstdc++'s pi_v<__float128> is the long double value.
template <>
inline constexpr float128_t pi_v<float128_t> = __builtin_bit_cast(
    float128_t, (static_cast<unsigned __int128>(0x4000921FB54442D1ULL) << 64) | 0x8469898CC51701B8ULL);

// float16_t: computed in float, as libstdc++ does under GCC (glibc has no
// _Float16 functions). float128_t: glibc's binary128 functions.
inline bool isnan(const float16_t x) { return __builtin_isnan(x); }
inline bool isinf(const float16_t x) { return __builtin_isinf(x); }
inline float16_t abs(const float16_t x) { return __builtin_fabsf16(x); }
inline float16_t copysign(const float16_t x, const float16_t y) { return __builtin_copysignf16(x, y); }
inline float16_t ldexp(const float16_t x, const int e) { return static_cast<float16_t>(std::ldexp(static_cast<float>(x), e)); }
inline float16_t nearbyint(const float16_t x) { return static_cast<float16_t>(std::nearbyint(static_cast<float>(x))); }
inline float16_t trunc(const float16_t x) { return static_cast<float16_t>(std::trunc(static_cast<float>(x))); }
inline float16_t sin(const float16_t x) { return static_cast<float16_t>(std::sin(static_cast<float>(x))); }
inline float16_t sqrt(const float16_t x) { return static_cast<float16_t>(std::sqrt(static_cast<float>(x))); }
inline float16_t fmod(const float16_t x, const float16_t y)
{
    return static_cast<float16_t>(std::fmod(static_cast<float>(x), static_cast<float>(y)));
}
inline float16_t atan2(const float16_t y, const float16_t x)
{
    return static_cast<float16_t>(std::atan2(static_cast<float>(y), static_cast<float>(x)));
}
inline float16_t abs(const std::complex<float16_t>& z)
{
    return static_cast<float16_t>(std::hypot(static_cast<float>(z.real()), static_cast<float>(z.imag())));
}
inline float16_t arg(const std::complex<float16_t>& z) { return atan2(z.imag(), z.real()); }

inline bool isnan(const float128_t x) { return __builtin_isnan(x); }
inline bool isinf(const float128_t x) { return __builtin_isinf(x); }
inline float128_t copysign(const float128_t x, const float128_t y) { return __builtin_copysignf128(x, y); }
inline float128_t ldexp(const float128_t x, const int e) { return __builtin_ldexpf128(x, e); }
inline float128_t nearbyint(const float128_t x) { return __builtin_nearbyintf128(x); }
inline float128_t trunc(const float128_t x) { return __builtin_truncf128(x); }
inline float128_t sin(const float128_t x) { return __builtin_sinf128(x); }
inline float128_t sqrt(const float128_t x) { return __builtin_sqrtf128(x); }
inline float128_t fmod(const float128_t x, const float128_t y) { return __builtin_fmodf128(x, y); }
inline float128_t atan2(const float128_t y, const float128_t x) { return __builtin_atan2f128(y, x); }
inline float128_t abs(const std::complex<float128_t>& z) { return __builtin_hypotf128(z.real(), z.imag()); }
inline float128_t arg(const std::complex<float128_t>& z) { return atan2(z.imag(), z.real()); }
#endif

} // namespace s7::math

#endif // S7CORE_INCLUDE_S7_FLOAT_TYPES_HPP
