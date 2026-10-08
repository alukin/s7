#!/usr/bin/env bash
# Builds s7spectral the way a third party would (against the installed SDK)
# and loads it. Usage: test_third_party_plugin.sh <s7-build-dir> <s7-source-dir> <smoke-exe> <work-dir>
# In a cross build, S7_TOOLCHAIN_FILE is the build's toolchain and S7_RUN runs
# the target's programs (Wine).
set -euo pipefail
BUILD_DIR="$1" SOURCE_DIR="$2" SMOKE="$3" WORK_DIR="$4"
CROSS=() RUN=()
[ -n "${S7_RUN:-}" ] && RUN=("$S7_RUN")
if [ -n "${S7_TOOLCHAIN_FILE:-}" ]; then
    # Packages are searched for in the sysroot only, so name the SDK's directly.
    CROSS=(--toolchain "$S7_TOOLCHAIN_FILE" -Ds7core_DIR="$WORK_DIR/sdk/lib/cmake/s7core")
fi

if ! pkg-config --exists fftw3f; then
    echo "fftw3f not found; skipping third-party plugin test"
    exit 77
fi

rm -rf "$WORK_DIR"
cmake --install "$BUILD_DIR" --component s7sdk --prefix "$WORK_DIR/sdk" > /dev/null
cmake -S "$SOURCE_DIR/s7spectral" -B "$WORK_DIR/build" -DCMAKE_PREFIX_PATH="$WORK_DIR/sdk" \
    -DCMAKE_CXX_COMPILER="${CXX:-c++}" "${CROSS[@]}" > /dev/null
cmake --build "$WORK_DIR/build" > /dev/null
for PLUGIN in "$WORK_DIR"/build/libs7spectral.{so,dll}; do [ -f "$PLUGIN" ] && break; done
"${RUN[@]}" "$SMOKE" "$PLUGIN"
