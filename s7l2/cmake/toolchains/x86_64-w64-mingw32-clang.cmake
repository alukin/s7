# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

# Cross-builds S7 for 64-bit Windows on Linux: Clang 22 targeting MinGW-w64
# (UCRT), against the sysroot that tools/windows/fetch_msys2_sysroot.py
# unpacks (MSYS2 UCRT64 packages: mingw-w64 CRT, GCC's libstdc++, Boost,
# OpenSSL, zlib, Qt6, FFTW). Tests run under Wine; see docs/WINDOWS_BUILD.md.
#
#   tools/windows/fetch_msys2_sysroot.py
#   cmake -S . -B build-win -G Ninja --toolchain cmake/toolchains/x86_64-w64-mingw32-clang.cmake
#   cmake --build build-win
#   ctest --test-dir build-win --output-on-failure
#
# Cache variables: S7_MINGW_SYSROOT (default ~/.cache/s7/msys2-ucrt64),
# S7_LLVM_SUFFIX (default -22: clang-22, clang++-22), S7_QT_HOST_PATH (default
# /usr: the host Qt 6 whose moc/uic/rcc generate code for the Windows Qt).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(S7_MINGW_SYSROOT "$ENV{HOME}/.cache/s7/msys2-ucrt64" CACHE PATH "MSYS2 UCRT64 sysroot for the Windows cross build")
set(S7_LLVM_SUFFIX "-22" CACHE STRING "Suffix of the LLVM tool names (clang++-22)")
set(S7_QT_HOST_PATH "/usr" CACHE PATH "Prefix of the host Qt 6 whose tools generate code for the target Qt")
if (NOT EXISTS "${S7_MINGW_SYSROOT}/include/c++")
	message(FATAL_ERROR "No Windows sysroot at ${S7_MINGW_SYSROOT}; run tools/windows/fetch_msys2_sysroot.py "
		"or pass -DS7_MINGW_SYSROOT=<dir>.")
endif()

set(S7_WINDOWS_TARGET x86_64-w64-windows-gnu)
set(CMAKE_C_COMPILER clang${S7_LLVM_SUFFIX})
set(CMAKE_CXX_COMPILER clang++${S7_LLVM_SUFFIX})
set(CMAKE_C_COMPILER_TARGET ${S7_WINDOWS_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${S7_WINDOWS_TARGET})
set(CMAKE_RC_COMPILER llvm-windres${S7_LLVM_SUFFIX})
find_program(S7_LLVM_AR NAMES llvm-ar${S7_LLVM_SUFFIX} llvm-ar ar REQUIRED)
find_program(S7_LLVM_RANLIB NAMES llvm-ranlib${S7_LLVM_SUFFIX} llvm-ranlib ranlib REQUIRED)
set(CMAKE_AR "${S7_LLVM_AR}" CACHE FILEPATH "Archiver for the Windows cross build")
set(CMAKE_RANLIB "${S7_LLVM_RANLIB}" CACHE FILEPATH "Archive indexer for the Windows cross build")
set(CMAKE_LINKER_TYPE LLD)
# libstdc++ (thread model posix) calls winpthreads from inline code; GCC's
# driver links it implicitly, Clang's does not.
foreach (kind EXE SHARED MODULE)
	set(CMAKE_${kind}_LINKER_FLAGS_INIT "-lpthread")
endforeach()

set(CMAKE_SYSROOT ${S7_MINGW_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${S7_MINGW_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The .pc files of MSYS2 say prefix=/ucrt64.
set(ENV{PKG_CONFIG_LIBDIR} "${S7_MINGW_SYSROOT}/lib/pkgconfig:${S7_MINGW_SYSROOT}/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
set(PKG_CONFIG_ARGN "--define-variable=prefix=${S7_MINGW_SYSROOT}")

# Qt's moc/uic/rcc come from the host Qt; it may be an older 6.x than the target.
set(QT_HOST_PATH ${S7_QT_HOST_PATH})
set(QT_NO_PACKAGE_VERSION_CHECK TRUE)
set(QT_NO_QTPATHS_DEPLOYMENT_WARNING TRUE)
