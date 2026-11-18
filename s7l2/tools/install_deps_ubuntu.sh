#!/usr/bin/env bash
# Install development dependencies for s7l2 on Ubuntu 26.04 LTS and 26.10.
#
# Usage: tools/install_deps_ubuntu.sh [--no-studio] [--no-gui-tests] [--no-spectral] [--no-hardware] [--no-extras] [--native-packages] [--windows] [--dry-run]
#   --no-studio    skip Qt6 packages (for headless builds with -DS7_BUILD_STUDIO=OFF)
#   --no-gui-tests skip Xvfb/Mesa/drag tools and disable the native GUI regression suite
#   --no-spectral  skip plugin prerequisites; Studio still requires FFTW
#   --no-hardware skip public PortAudio, PipeWire and SoapySDR development packages
#   --no-extras    skip optional build, plotting and AppDir packaging tools
#   --windows      also the Windows cross toolchain: Clang/LLVM 22, Wine (docs/WINDOWS_BUILD.md)
#   --dry-run      print the apt command instead of running it
#   --native-packages add DEB build tools (independent of --no-extras)

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

WITH_STUDIO=1
WITH_SPECTRAL=1
WITH_EXTRAS=1
WITH_HARDWARE=1
WITH_GUI_TESTS=1
WITH_WINDOWS=0
WITH_NATIVE_PACKAGES=0
DRY_RUN=0

for arg in "$@"; do
  case "$arg" in
    --no-studio) WITH_STUDIO=0 ;;
    --no-gui-tests) WITH_GUI_TESTS=0 ;;
    --no-spectral) WITH_SPECTRAL=0 ;;
    --no-extras) WITH_EXTRAS=0 ;;
    --no-hardware) WITH_HARDWARE=0 ;;
    --windows) WITH_WINDOWS=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --native-packages) WITH_NATIVE_PACKAGES=1 ;;
    -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ "${ID:-}" != "ubuntu" ]]; then
    echo "Warning: this script targets Ubuntu, detected '${PRETTY_NAME:-unknown}'." >&2
  fi
fi

# C++26 needs GCC 15+ (the default on these releases) and a CMake version
# supporting C++26. Clang 22+ with libstdc++ is also supported via CXX.
PACKAGES=(
  build-essential
  cmake
  git              # QSchematic fetches its GPDS dependency via FetchContent
  pkg-config
  ca-certificates
  python3          # wire/control fixture tests (skipped without it)
  diffutils        # cmp, used by the fixture tests
)

# Core/runtime libraries: Boost.JSON, Boost.System/Asio/Beast/UUID, OpenSSL, zlib.
PACKAGES+=(
  libboost-dev
  libboost-json-dev
  libssl-dev
  openssl          # CLI used by tools/generate_test_runtime_certs.sh
  zlib1g-dev
  liblz4-dev
  libzstd-dev      # includes libzstd.a for the static-workspace frame codec
)

# Public hardware providers; private AD936x dependencies are never fetched here.
if (( WITH_HARDWARE )); then
  PACKAGES+=(portaudio19-dev libpipewire-0.3-dev libspa-0.2-dev libsoapysdr-dev)
fi

# s7studio: Qt6 Core, Gui, Widgets (+ OpenGL headers required by Qt6Gui); the
# observation viewers' Qt Quick, Qt Graphs and Qt Quick 3D (Qt >= 6.9) and the
# QML modules their scenes import at run time.
if (( WITH_STUDIO )); then
  PACKAGES+=(
    qt6-base-dev
    qt6-base-dev-tools
    libgl-dev
    qt6-declarative-dev
    qt6-graphs-dev
    qt6-quick3d-dev
    qt6-wayland
    libqt6svg6
    fonts-dejavu-core
    qml6-module-qtquick
    qml6-module-qtquick-window
    qml6-module-qtqml-workerscript
    qml6-module-qtgraphs
    qml6-module-qtquick3d
    qml6-module-qtquick3d-helpers
  )
fi

# The native Linux GUI suite defaults ON in CMake, independently of extras.
# Xvfb gives each test an isolated display; Mesa supplies software OpenGL.
if (( WITH_STUDIO && WITH_GUI_TESTS )); then
  PACKAGES+=(qml6-module-qttest xvfb xdotool libgl1-mesa-dri)
fi

# Studio uses binary64 FFTW; s7spectral independently uses single precision.
if (( WITH_STUDIO || WITH_SPECTRAL )); then
  PACKAGES+=(libfftw3-dev)
fi

# Optional helpers: faster builds, sample plotting and tools/linux/package.sh.
if (( WITH_EXTRAS )); then
  PACKAGES+=(
    ninja-build
    gnuplot
    curl
    patchelf
    binutils
  )
fi
if (( WITH_NATIVE_PACKAGES )); then
  PACKAGES+=(dpkg-dev file ninja-build)
fi

# Windows cross build: Clang targeting MinGW, lld and the LLVM binutils, Wine
# for the tests, zstd for tools/windows/fetch_msys2_sysroot.py. The host Qt's
# moc/uic/rcc (qt6-base-dev-tools) generate code for the Windows Qt.
if (( WITH_WINDOWS )); then
  PACKAGES+=(
    clang-22
    lld-22
    llvm-22
    wine
    wine64
    zstd
    ninja-build
  )
fi

SUDO=()
if (( EUID != 0 )); then
  SUDO=(sudo)
fi

if (( DRY_RUN )); then
  echo "${SUDO[*]} apt-get update"
  echo "${SUDO[*]} apt-get install -y --no-install-recommends ${PACKAGES[*]}"
  exit 0
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y --no-install-recommends "${PACKAGES[@]}"

echo
echo "Dependencies installed. Build with:"
BUILD_ARGS=(-DS7_BUILD_STUDIO=OFF)
if (( WITH_STUDIO )); then
  BUILD_ARGS=(-DS7_BUILD_STUDIO=ON -DS7_BUILD_GUI_REGRESSION_TESTS=OFF)
  if (( WITH_GUI_TESTS )); then
    BUILD_ARGS[1]=-DS7_BUILD_GUI_REGRESSION_TESTS=ON
  fi
fi
BACKEND=OFF
if (( WITH_HARDWARE )); then BACKEND=ON; fi
BUILD_ARGS+=(-DS7_PORTAUDIO="$BACKEND" -DS7_PIPEWIRE="$BACKEND" -DS7_SOAPYSDR="$BACKEND")
echo "  cmake -S . -B build ${BUILD_ARGS[*]}"
echo "  cmake --build build --parallel 2"
echo "  cmake --build build --target build-tests --parallel 2"
echo "  ctest --test-dir build --output-on-failure"
if (( WITH_WINDOWS )); then
  echo
  echo "Windows binaries (docs/WINDOWS_BUILD.md):"
  echo "  tools/windows/fetch_msys2_sysroot.py$( (( WITH_STUDIO )) || echo ' --no-studio')"
  echo "  cmake -S . -B build-win -G Ninja --toolchain cmake/toolchains/x86_64-w64-mingw32-clang.cmake$( (( WITH_STUDIO )) || echo ' -DS7_BUILD_STUDIO=OFF')"
  echo "  cmake --build build-win"
  echo "  cmake --build build-win --target build-tests --parallel"
  echo "  ctest --test-dir build-win --output-on-failure"
fi
