#!/usr/bin/env bash
# Install development dependencies for s7l2 on Fedora 45.
#
# Usage: tools/install_deps_fedora.sh [--no-studio] [--no-gui-tests] [--no-spectral] [--no-hardware] [--no-extras] [--native-packages] [--dry-run]
#   --no-studio    skip Qt6 packages (for headless builds with -DS7_BUILD_STUDIO=OFF)
#   --no-gui-tests skip Xvfb/Mesa/drag tools and disable the native GUI regression suite
#   --no-spectral  skip plugin prerequisites; Studio still requires FFTW
#   --no-hardware skip public PortAudio, PipeWire and SoapySDR development packages
#   --no-extras    skip optional build, plotting and AppDir packaging tools
#   --dry-run      print the dnf command instead of running it
#   --native-packages add RPM build tools (independent of --no-extras)
# Windows binaries are cross-built on Ubuntu (tools/install_deps_ubuntu.sh --windows).

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

WITH_STUDIO=1
WITH_SPECTRAL=1
WITH_EXTRAS=1
WITH_HARDWARE=1
WITH_GUI_TESTS=1
DRY_RUN=0
WITH_NATIVE_PACKAGES=0

for arg in "$@"; do
  case "$arg" in
    --no-studio) WITH_STUDIO=0 ;;
    --no-gui-tests) WITH_GUI_TESTS=0 ;;
    --no-spectral) WITH_SPECTRAL=0 ;;
    --no-extras) WITH_EXTRAS=0 ;;
    --no-hardware) WITH_HARDWARE=0 ;;
    --windows)
      echo "Windows cross builds are set up on Ubuntu: tools/install_deps_ubuntu.sh --windows (docs/WINDOWS_BUILD.md)." >&2
      exit 2 ;;
    --dry-run) DRY_RUN=1 ;;
    --native-packages) WITH_NATIVE_PACKAGES=1 ;;
    -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ "${ID:-}" != "fedora" ]]; then
    echo "Warning: this script targets Fedora, detected '${PRETTY_NAME:-unknown}'." >&2
  fi
fi

# C++26 needs GCC 15+ and a CMake version supporting C++26. Fedora 45's
# default compiler is suitable; Clang 22+ with libstdc++ also works via CXX.
PACKAGES=(
  gcc-c++
  cmake
  make
  git-core         # QSchematic fetches its GPDS dependency via FetchContent
  pkgconf-pkg-config
  ca-certificates
  python3          # wire/control fixture tests (skipped without it)
  diffutils        # cmp, used by the fixture tests
)

# Core/runtime libraries: Boost.JSON, Boost.System/Asio/Beast/UUID, OpenSSL, zlib.
PACKAGES+=(
  boost-devel
  openssl-devel
  openssl          # CLI used by tools/generate_test_runtime_certs.sh
  zlib-ng-compat-devel
  lz4-devel
  libzstd-devel
  libzstd-static   # static linkage is required by the frame codec's workspace API
)

# Public hardware providers; private AD936x dependencies are never fetched here.
if (( WITH_HARDWARE )); then
  PACKAGES+=(portaudio-devel pipewire-devel SoapySDR-devel)
fi

# s7studio: Qt6 Core, Gui, Widgets (+ OpenGL headers required by Qt6Gui); the
# observation viewers' Qt Quick, Qt Graphs and Qt Quick 3D (Qt >= 6.9). Their
# QML modules come with the runtime packages these depend on.
if (( WITH_STUDIO )); then
  PACKAGES+=(
    qt6-qtbase-devel
    mesa-libGL-devel
    qt6-qtdeclarative-devel
    qt6-qtgraphs-devel
    qt6-qtquick3d-devel
    qt6-qtwayland
    qt6-qtsvg
    dejavu-sans-fonts
  )
fi

# CMake enables the native Linux GUI suite by default, independently of extras.
if (( WITH_STUDIO && WITH_GUI_TESTS )); then
  PACKAGES+=(xorg-x11-server-Xvfb xdotool mesa-dri-drivers)
fi

# Studio uses binary64 FFTW; s7spectral independently uses single precision.
if (( WITH_STUDIO || WITH_SPECTRAL )); then
  PACKAGES+=(fftw-devel)
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
  PACKAGES+=(rpm-build file ninja-build)
fi

SUDO=()
if (( EUID != 0 )); then
  SUDO=(sudo)
fi

if (( DRY_RUN )); then
  echo "${SUDO[*]} dnf install -y ${PACKAGES[*]}"
  exit 0
fi

"${SUDO[@]}" dnf install -y "${PACKAGES[@]}"

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
