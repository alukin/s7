# Linux build dependencies

Run the installer for your distribution from `s7l2/`:

```sh
tools/install_deps_ubuntu.sh        # Ubuntu 26.04 LTS or 26.10
tools/install_deps_fedora.sh        # Fedora 45
```

Both scripts use the distribution repositories, run as root or through `sudo`,
and support `--help` and `--dry-run`. Ubuntu refreshes APT metadata first.
The scripts print configure/build commands matching the selected options.
Use a fresh build directory when changing the compiler.

S7 requires **GCC 15+ or Clang 22+ with libstdc++**, C++26 support in CMake,
and **Qt 6.9+** for Studio. The supported distributions provide a suitable
default GCC. A different supported compiler can be selected with `CXX` at the
first CMake configuration; Windows cross builds have a separate toolchain.

| Dependency group | Installed by default | How to omit it |
| --- | --- | --- |
| Core/runtime | Compiler, CMake, make, Git, pkg-config, Python, Boost.JSON/headers, OpenSSL and zlib | Required |
| Sample-frame compression | LZ4 development files and Zstandard headers/static library | Installed with the core dependencies; CMake can disable codecs with `S7_FRAME_LZ4=OFF` and `S7_FRAME_ZSTD=OFF` |
| Studio | Qt Widgets, QML/Quick, Graphs, Quick 3D, Wayland and SVG support, fonts, double-precision FFTW | `--no-studio` |
| Native Studio GUI regression suite | Qt Test/Quick Test support, Xvfb, Mesa software OpenGL and xdotool | `--no-gui-tests`, or `--no-studio` |
| External spectral plugin | Single-precision FFTW development files | `--no-spectral`; Studio still needs FFTW |
| Public hardware providers | PortAudio, native PipeWire/SPA and SoapySDR development files | `--no-hardware` |
| Extra tools | Ninja, gnuplot, curl, patchelf and binutils for build/plotting/AppDir preparation | `--no-extras` |

GUI test dependencies are independent of `--no-extras`, because CMake enables
the native Linux GUI regression suite by default. `--no-gui-tests` prints
`-DS7_BUILD_GUI_REGRESSION_TESTS=OFF`. `--no-hardware` prints all three backend
switches as `OFF`; otherwise they are `ON`, so configuration verifies that all
three public providers are available. The scripts install no private provider
and do not discover or activate hardware.

For distribution-native RPM/DEB builds, add `--native-packages`. This installs
the package generator's tools independently of `--no-extras`; see
[native Linux packages](NATIVE_LINUX_PACKAGES.md) for the build and installed
verification workflow.

Ubuntu supplies the static Zstandard archive in `libzstd-dev`. Fedora requires
both `libzstd-devel` and `libzstd-static`; the frame codec uses the static-only
workspace API. Installing only the headers can silently omit this codec.

For a minimal headless development environment:

```sh
tools/install_deps_ubuntu.sh --no-studio --no-spectral --no-hardware --no-extras
# Or use install_deps_fedora.sh with the same options.
cmake -S . -B build-headless -DS7_BUILD_STUDIO=OFF \
  -DS7_PORTAUDIO=OFF -DS7_PIPEWIRE=OFF -DS7_SOAPYSDR=OFF
cmake --build build-headless --parallel 2
```

For Windows cross-build dependencies on Ubuntu, add `--windows`. This installs
Clang/LLVM/lld 22, Wine and zstd; the separate MSYS2 sysroot download and build
steps are in [Windows builds](WINDOWS_BUILD.md). It does not install that
sysroot or build Windows binaries automatically. AppDir preparation tools do
not qualify a release package on another distribution.

## Verification record

On 2026-11-17 the default installers completed in fresh `ubuntu:26.04`,
`ubuntu:26.10` and `fedora:45` Docker containers. All three configured the S7
project with Studio, GUI regression tests and all public hardware providers
explicitly enabled. LZ4 and the static Zstandard archive were found.

An independent small CMake program compiled and ran in each container. It
checked native F16/F128 C++26 headers, linked Boost.JSON, Qt, FFTW, the public
provider libraries and both codecs, and loaded the QtQuick, QtGraphs,
QtQuick3D/Helpers and QtTest QML modules with the offscreen platform. The
installer's option handling, dry runs and printed CMake flags were also checked.
This is dependency qualification, not a full S7 build, GUI rendering test or
hardware experiment on each distribution.
