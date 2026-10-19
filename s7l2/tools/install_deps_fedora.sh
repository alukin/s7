#!/usr/bin/env bash
# Install development dependencies for s7l2 on Fedora (tested on Fedora 44).
#
# Usage: tools/install_deps_fedora.sh [--no-studio] [--no-spectral] [--no-extras] [--dry-run]
#   --no-studio    skip Qt6 packages (for headless builds with -DS7_BUILD_STUDIO=OFF)
#   --no-spectral  skip FFTW (needed only by the s7spectral plugin)
#   --no-extras    skip optional tools (ninja, gnuplot)
#   --dry-run      print the dnf command instead of running it
# Windows binaries are cross-built on Ubuntu (tools/install_deps_ubuntu.sh --windows).
set -euo pipefail

WITH_STUDIO=1
WITH_SPECTRAL=1
WITH_EXTRAS=1
DRY_RUN=0

for arg in "$@"; do
  case "$arg" in
    --no-studio) WITH_STUDIO=0 ;;
    --no-spectral) WITH_SPECTRAL=0 ;;
    --no-extras) WITH_EXTRAS=0 ;;
    --windows)
      echo "Windows cross builds are set up on Ubuntu: tools/install_deps_ubuntu.sh --windows (docs/WINDOWS_BUILD.md)." >&2
      exit 2 ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ "${ID:-}" != "fedora" ]]; then
    echo "Warning: this script targets Fedora, detected '${PRETTY_NAME:-unknown}'." >&2
  fi
fi

# Toolchain and build system (CMake >= 3.20, GCC >= 15 for C++26).
PACKAGES=(
  gcc-c++
  cmake
  make
  git-core         # QSchematic fetches its GPDS dependency via FetchContent
  pkgconf-pkg-config
  python3          # wire/control fixture tests (skipped without it)
  diffutils        # cmp, used by the fixture tests
)

# Core/runtime libraries: Boost.JSON, Boost.System/Asio/Beast/UUID, OpenSSL, zlib.
PACKAGES+=(
  boost-devel
  openssl-devel
  openssl          # CLI used by tools/generate_test_runtime_certs.sh
  zlib-ng-compat-devel
)

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
  )
fi

# s7spectral plugin: single-precision FFTW.
if (( WITH_SPECTRAL )); then
  PACKAGES+=(fftw-devel)
fi

# Optional helpers: faster builds and plotting of gnuplot-format sample files.
if (( WITH_EXTRAS )); then
  PACKAGES+=(
    ninja-build
    gnuplot
  )
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
if (( WITH_STUDIO )); then
  echo "  cmake -S . -B build"
else
  echo "  cmake -S . -B build -DS7_BUILD_STUDIO=OFF"
fi
echo "  cmake --build build --parallel"
echo "  ctest --test-dir build --output-on-failure"
