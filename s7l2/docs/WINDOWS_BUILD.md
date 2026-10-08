# Building S7 for Windows

S7 builds 64-bit Windows binaries on Linux (Ubuntu 26.04): Clang 22 cross-compiles for MinGW-w64 (UCRT) against libstdc++ and the dependencies from MSYS2's UCRT64 packages, and the tests run under Wine. No Windows machine is needed to build or test; the result runs on Windows 10 and later.

## Setup (once)

```bash
tools/install_deps_ubuntu.sh --windows      # clang-22, lld-22, llvm-22, wine, zstd (and the Linux build's deps)
tools/windows/fetch_msys2_sysroot.py        # ~150 MB into ~/.cache/s7/msys2-ucrt64
```

The fetch script downloads the MSYS2 UCRT64 packages S7 needs (mingw-w64 CRT and headers, GCC's libstdc++ and runtime DLLs, Boost, OpenSSL, zlib, Qt6, FFTW) and everything they depend on, checks their SHA-256 sums against the repository database and unpacks them into one sysroot. `--no-studio` skips Qt, `--no-spectral` skips FFTW; another directory can be given as an argument (then pass `-DS7_MINGW_SYSROOT=<dir>` to CMake). MSYS2 is a rolling distribution: rerunning the script updates the sysroot to the packages it ships now.

## Build and test

```bash
cmake -S . -B build-win -G Ninja --toolchain cmake/toolchains/x86_64-w64-mingw32-clang.cmake
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

Test without `-DCMAKE_BUILD_TYPE=Release`: several smoke tests check with `assert()`, which Release (`NDEBUG`) compiles out. Configure a separate Release build (`-B build-win-release -DCMAKE_BUILD_TYPE=Release`) for the binaries you package.

`-DS7_BUILD_STUDIO=OFF` builds without Qt. The toolchain file's cache variables: `S7_MINGW_SYSROOT` (the sysroot), `S7_LLVM_SUFFIX` (`-22`: the tools are `clang++-22`, `llvm-objdump-22`, ...), `S7_QT_HOST_PATH` (`/usr`: the host Qt whose moc, uic and rcc generate code for the Windows Qt).

ctest runs every test program under Wine through `build-win/wine-run.sh`, which puts the build's DLL directories and the sysroot's `bin/` on `WINEPATH`. The Wine prefix is `build-win/wine-prefix`, created on the first run. The shell-script tests get the runner as `S7_RUN` (see `S7_SCRIPT_TEST_ENV` in `CMakeLists.txt`). To run a program by hand: `build-win/wine-run.sh build-win/s7runtime/s7runtime.exe --help`.

## Package

```bash
tools/windows/package.sh build-win-release dist/s7-windows
```

The directory holds `s7runtime.exe`, `s7ctl.exe`, `s7studio.exe`, `libs7core.dll`, `plugins/libs7noise.dll`, Qt's `platforms/` and `styles/` plugins, `examples/`, and every DLL these import from the sysroot (found with `llvm-objdump`); the build's own modules are stripped. Copy or zip it to a Windows machine. Start the programs in that directory (Explorer does for `s7studio.exe`), so that they find `./plugins`.

On Windows:

- The default state directory is `%USERPROFILE%\.s7runtime\default-node`; `~/.s7/plugins` is `%USERPROFILE%\.s7\plugins`. There is no `/opt/s7/plugins`. Plugins are `libs7*.dll` or `s7*.dll`.
- Windows reports no error bound for its clock, so a node never counts as NTP-synchronized (`read_kernel_sync`): give its schema `"clock": { "assume_synchronized": true }` to deploy diagrams across nodes.
- Ctrl+C stops `s7runtime` cleanly; Windows has no SIGTERM.

## How the port works

Platform code is behind `s7core/include/s7/platform.hpp` (POSIX and Windows implementations in `s7core/src/platform.cpp`): crash-safe file replacement (`write_file_atomically`: `FlushFileBuffers` and `MoveFileExW` on Windows, which has no directory sync), the state-directory lock (`FileLock`: a file opened without sharing on Windows, `flock` on POSIX), the home directory, and loading plugins (`LoadLibraryExW` with the plugin's directory on the DLL search path, so a plugin finds the DLLs next to it). Elsewhere `#if defined(_WIN32)` covers the clock state (`s7transport/src/clock_sync.cpp`) and a test's interface lookup.

Why each piece of the toolchain is as it is:

- **Clang, not MinGW GCC.** Ubuntu's MinGW GCC is 13; S7 needs GCC 15 or Clang 22. Clang uses the libstdc++ 16 of the MSYS2 sysroot (not libc++, which has no `<stdfloat>`) and lld. Clang's driver does not link winpthreads, which libstdc++ calls from inline code, so the toolchain file adds `-lpthread`.
- **`__float128` across the Clang/GCC boundary.** On Win64 both compilers pass `__float128` arguments by reference, but Clang returns a `__float128` in XMM0 while GCC returns it through a hidden pointer. Code that GCC compiled therefore cannot simply be called with Clang's convention:
  - libquadmath (the F128 math: `sinq`, `ldexpq`, ...): `s7/float_types.hpp` declares those functions with GCC's convention spelled out.
  - libgcc's soft-float routines that return `__float128` (`__addtf3`, `__extenddftf2`, `__floatditf`, ...), which Clang calls for every F128 operation: `s7core/src/windows_f128_abi.cpp` replaces them in every module that links s7core (the `s7_f128_abi` library), forwarding to libgcc with GCC's convention. Routines that return anything else (comparisons, conversions from F128) are compatible.
  `s7core_signal_types_smoke` exercises each of these at run time; it is the test that fails if a new GCC or Clang changes either convention.
- **DLLs.** MinGW exports every symbol of a DLL that marks none for export, so `libs7core.dll` and plugins need no export macros; the `s7_f128_abi` symbols are excluded (`--exclude-symbols`), each module keeps its own. Boost is linked as DLLs so that the executables, `libs7core.dll` and plugins share one Boost.JSON.
- **Qt.** MSYS2's Qt is a native Windows build: its moc, uic and rcc are Windows programs. `CMakeLists.txt` points the `Qt6::moc`/`uic`/`rcc` targets at the host Qt's tools (Ubuntu's Qt 6.10 generates code for MSYS2's Qt 6.11; `QT_NO_PACKAGE_VERSION_CHECK`).
- **Wine.** `wine-run.sh` disables `winedbg`, so a crashing test fails instead of waiting in the debugger. Windows programs write `\r\n` to stdout; the shell-script tests strip the `\r` before matching lines.
- **Third-party plugins.** `s7spectral_sdk_smoke` builds s7spectral against the installed SDK with the same toolchain file; a plugin built out of tree for Windows does the same (`--toolchain ... -Ds7core_DIR=<sdk>/lib/cmake/s7core`, since a cross build finds packages only inside the sysroot).
