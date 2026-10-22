#!/usr/bin/env bash
# Install development dependencies for s7l2 on Ubuntu (tested on 26.04 LTS).
#
# Usage: tools/install_deps_ubuntu.sh [--no-studio] [--no-spectral] [--no-extras] [--windows] [--dry-run]
#   --no-studio    skip Qt6 packages (for headless builds with -DS7_BUILD_STUDIO=OFF)
#   --no-spectral  skip plugin prerequisites; Studio still requires FFTW
#   --no-extras    skip optional tools (ninja, gnuplot)
#   --windows      also the Windows cross toolchain: Clang/LLVM 22, Wine (docs/WINDOWS_BUILD.md)
#   --dry-run      print the apt command instead of running it

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

WITH_STUDIO=1
WITH_SPECTRAL=1
WITH_EXTRAS=1
WITH_WINDOWS=0
DRY_RUN=0

for arg in "$@"; do
  case "$arg" in
    --no-studio) WITH_STUDIO=0 ;;
    --no-spectral) WITH_SPECTRAL=0 ;;
    --no-extras) WITH_EXTRAS=0 ;;
    --windows) WITH_WINDOWS=1 ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ "${ID:-}" != "ubuntu" ]]; then
    echo "Warning: this script targets Ubuntu, detected '${PRETTY_NAME:-unknown}'." >&2
  fi
fi

# Toolchain and build system (CMake >= 3.20, C++20 compiler).
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
)

# s7studio: Qt6 Core, Gui, Widgets (+ OpenGL headers required by Qt6Gui); the
# observation viewers' Qt Quick, Qt Graphs and Qt Quick 3D (Qt >= 6.9) and the
# QML modules their scenes import at run time.
if (( WITH_STUDIO )); then
  PACKAGES+=(
    qt6-base-dev
    libgl-dev
    qt6-declarative-dev
    qt6-graphs-dev
    qt6-quick3d-dev
    qml6-module-qtquick
    qml6-module-qttest
    qml6-module-qtquick-window
    qml6-module-qtqml-workerscript
    qml6-module-qtgraphs
    qml6-module-qtquick3d
    qml6-module-qtquick3d-helpers
  )
fi

# Studio uses binary64 FFTW; s7spectral independently uses single precision.
if (( WITH_STUDIO || WITH_SPECTRAL )); then
  PACKAGES+=(libfftw3-dev)
fi

# Optional helpers: faster builds and plotting of gnuplot-format sample files.
if (( WITH_EXTRAS )); then
  PACKAGES+=(
    ninja-build
    gnuplot
  )
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
    python3
    ninja-build
  )
  if (( WITH_STUDIO )); then
    PACKAGES+=(qt6-base-dev-tools)
  fi
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
if (( WITH_STUDIO )); then
  echo "  cmake -S . -B build"
else
  echo "  cmake -S . -B build -DS7_BUILD_STUDIO=OFF"
fi
echo "  cmake --build build --parallel"
echo "  cmake --build build --target build-tests --parallel"
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
