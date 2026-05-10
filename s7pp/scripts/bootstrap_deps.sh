#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MODE="dry-run"
RUN_CONFIGURE=0
BUILD_DIR="${ROOT_DIR}/build_verify"
APPLY_PATCHES=1

usage() {
  cat <<'EOF'
bootstrap_deps.sh

Install native dependencies needed for vendored rsocket-cpp + folly bootstrap.

Usage:
  ./scripts/bootstrap_deps.sh [--install] [--configure] [--build-dir <path>] [--no-apply-patches]

Options:
  --install           Execute package manager commands (default is dry-run)
  --configure         After install, run cmake configure with -DRSOCKET_INSTALL_DEPS=ON
  --build-dir <path>  Build directory for configure step (default: ./build_verify)
  --no-apply-patches  Skip applying third-party patch bundle before configure
  -h, --help          Show this help

Examples:
  ./scripts/bootstrap_deps.sh
  ./scripts/bootstrap_deps.sh --install
  ./scripts/bootstrap_deps.sh --install --configure
  ./scripts/bootstrap_deps.sh --install --configure --no-apply-patches
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install)
      MODE="install"
      shift
      ;;
    --configure)
      RUN_CONFIGURE=1
      shift
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --no-apply-patches)
      APPLY_PATCHES=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ ! -f /etc/os-release ]]; then
  echo "Cannot detect OS (missing /etc/os-release)." >&2
  exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release

echo "Detected OS: ${PRETTY_NAME:-unknown}"

dnf_packages=(
  gcc-c++
  gcc
  make
  cmake
  ninja-build
  pkgconf-pkg-config
  git
  openssl-devel
  gflags-devel
  glog-devel
  fmt-devel
  double-conversion-devel
  fast_float-devel
  boost-devel
  libevent-devel
  libunwind-devel
  zlib-ng-compat-devel
  libzstd-devel
  libsodium-devel
)

apt_packages=(
  build-essential
  cmake
  ninja-build
  pkg-config
  git
  libssl-dev
  libgflags-dev
  libgoogle-glog-dev
  libfmt-dev
  libdouble-conversion-dev
  libfast-float-dev
  libboost-all-dev
  libevent-dev
  libunwind-dev
  zlib1g-dev
  libzstd-dev
  libsodium-dev
)

run_cmd() {
  echo "> $*"
  if [[ "$MODE" == "install" ]]; then
    "$@"
  fi
}

SUDO_CMD=()
if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  SUDO_CMD=(sudo)
fi

case "${ID:-}" in
  fedora|rhel|centos)
    run_cmd "${SUDO_CMD[@]}" dnf install -y "${dnf_packages[@]}"
    ;;
  ubuntu|debian)
    run_cmd "${SUDO_CMD[@]}" apt-get update
    run_cmd "${SUDO_CMD[@]}" apt-get install -y "${apt_packages[@]}"
    ;;
  *)
    echo "Unsupported distro id: ${ID:-unknown}" >&2
    echo "Install required deps manually (see docs/S7_RSOCKET_DEPENDENCIES.md)." >&2
    exit 2
    ;;
esac

if [[ "$MODE" == "dry-run" ]]; then
  echo "Dry-run complete. Re-run with --install to apply."
fi

if [[ $RUN_CONFIGURE -eq 1 ]]; then
  if [[ $APPLY_PATCHES -eq 1 ]]; then
    run_cmd "$ROOT_DIR/scripts/apply_third_party_patches.sh"
  fi
  run_cmd mkdir -p "$BUILD_DIR"
  run_cmd cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DRSOCKET_INSTALL_DEPS=ON
fi

