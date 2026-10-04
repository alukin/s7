#!/usr/bin/env bash
# Install development dependencies for s7l2 on Ubuntu (tested on 26.04 LTS).
#
# Usage: tools/install_deps_ubuntu.sh [--no-studio] [--no-spectral] [--no-extras] [--dry-run]
#   --no-studio    skip Qt6 packages (for headless builds with -DS7_BUILD_STUDIO=OFF)
#   --no-spectral  skip FFTW (needed only by the s7spectral plugin)
#   --no-extras    skip optional tools (ninja, gnuplot)
#   --dry-run      print the apt command instead of running it
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
)

# Core/runtime libraries: Boost.JSON, Boost.System/Asio/Beast/UUID, OpenSSL, zlib.
PACKAGES+=(
  libboost-dev
  libboost-json-dev
  libssl-dev
  openssl          # CLI used by tools/generate_test_runtime_certs.sh
  zlib1g-dev
)

# s7studio: Qt6 Core, Gui, Widgets, PrintSupport (+ OpenGL headers required by Qt6Gui).
if (( WITH_STUDIO )); then
  PACKAGES+=(
    qt6-base-dev
    libgl-dev
  )
fi

# s7spectral plugin: single-precision FFTW.
if (( WITH_SPECTRAL )); then
  PACKAGES+=(libfftw3-dev)
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
echo "  ctest --test-dir build --output-on-failure"
