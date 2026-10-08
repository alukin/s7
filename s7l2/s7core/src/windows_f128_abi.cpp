// __float128 soft-float routines for Clang on x86_64 MinGW.
//
// Clang lowers __float128 arithmetic and conversions to libgcc calls
// (__addtf3, __extenddftf2, ...). Both compilers pass __float128 arguments by
// reference on Win64, but Clang expects a __float128 result in XMM0 while
// GCC, which built libgcc, returns it through a hidden pointer. These
// definitions take the place of libgcc's routines that return __float128:
// Clang calls them with its convention, and they call GCC's implementation
// in libgcc_s_seh-1.dll (always loaded, by libstdc++) with GCC's. Routines
// that return anything else are compatible and come from libgcc unchanged.
//
// Every module linking s7core links this library (it is listed before
// libgcc). s7core_signal_types_smoke checks the arithmetic.

#include <windows.h>

#include <cstdlib>

namespace {

using F128 = __float128;

[[nodiscard]] void* gcc_routine(const char* name)
{
    HMODULE libgcc = ::GetModuleHandleW(L"libgcc_s_seh-1.dll");
    if (!libgcc) {
        libgcc = ::LoadLibraryW(L"libgcc_s_seh-1.dll");
    }
    void* routine = libgcc ? reinterpret_cast<void*>(::GetProcAddress(libgcc, name)) : nullptr;
    if (!routine) {
        std::abort(); // no __float128 arithmetic without libgcc
    }
    return routine;
}

template <typename Signature>
[[nodiscard]] Signature* resolve(const char* name)
{
    return reinterpret_cast<Signature*>(gcc_routine(name));
}

// GCC's convention: result through the first (hidden) pointer, __float128 arguments by reference.
using Binary = F128*(F128*, const F128*, const F128*);
using Unary = F128*(F128*, const F128*);

} // namespace

extern "C" {

F128 __addtf3(const F128 a, const F128 b)
{
    static const auto routine = resolve<Binary>("__addtf3");
    F128 result;
    return *routine(&result, &a, &b);
}

F128 __subtf3(const F128 a, const F128 b)
{
    static const auto routine = resolve<Binary>("__subtf3");
    F128 result;
    return *routine(&result, &a, &b);
}

F128 __multf3(const F128 a, const F128 b)
{
    static const auto routine = resolve<Binary>("__multf3");
    F128 result;
    return *routine(&result, &a, &b);
}

F128 __divtf3(const F128 a, const F128 b)
{
    static const auto routine = resolve<Binary>("__divtf3");
    F128 result;
    return *routine(&result, &a, &b);
}

F128 __negtf2(const F128 a)
{
    static const auto routine = resolve<Unary>("__negtf2");
    F128 result;
    return *routine(&result, &a);
}

F128 __powitf2(const F128 a, const int b)
{
    static const auto routine = resolve<F128*(F128*, const F128*, int)>("__powitf2");
    F128 result;
    return *routine(&result, &a, b);
}

F128 __extendsftf2(const float a)
{
    static const auto routine = resolve<F128*(F128*, float)>("__extendsftf2");
    F128 result;
    return *routine(&result, a);
}

F128 __extenddftf2(const double a)
{
    static const auto routine = resolve<F128*(F128*, double)>("__extenddftf2");
    F128 result;
    return *routine(&result, a);
}

F128 __extendxftf2(const long double a)
{
    static const auto routine = resolve<F128*(F128*, const long double*)>("__extendxftf2");
    F128 result;
    return *routine(&result, &a);
}

// Not exported by libgcc_s; every _Float16 is exact as a float.
F128 __extendhftf2(const _Float16 a)
{
    return __extendsftf2(static_cast<float>(a));
}

F128 __floatsitf(const int a)
{
    static const auto routine = resolve<F128*(F128*, int)>("__floatsitf");
    F128 result;
    return *routine(&result, a);
}

F128 __floatunsitf(const unsigned int a)
{
    static const auto routine = resolve<F128*(F128*, unsigned int)>("__floatunsitf");
    F128 result;
    return *routine(&result, a);
}

F128 __floatditf(const long long a)
{
    static const auto routine = resolve<F128*(F128*, long long)>("__floatditf");
    F128 result;
    return *routine(&result, a);
}

F128 __floatunditf(const unsigned long long a)
{
    static const auto routine = resolve<F128*(F128*, unsigned long long)>("__floatunditf");
    F128 result;
    return *routine(&result, a);
}

F128 __floattitf(const __int128 a)
{
    static const auto routine = resolve<F128*(F128*, const __int128*)>("__floattitf");
    F128 result;
    return *routine(&result, &a);
}

// Clang passes this one's unsigned __int128 in two registers, low half first,
// not by reference as for __floattitf.
F128 __floatuntitf(const unsigned long long low, const unsigned long long high)
{
    static const auto routine = resolve<F128*(F128*, const unsigned __int128*)>("__floatuntitf");
    const unsigned __int128 a = (static_cast<unsigned __int128>(high) << 64) | low;
    F128 result;
    return *routine(&result, &a);
}

} // extern "C"
