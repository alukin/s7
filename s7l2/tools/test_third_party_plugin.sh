#!/usr/bin/env bash
# Builds s7spectral the way a third party would (against the installed SDK)
# and loads it. Usage: test_third_party_plugin.sh <s7-build-dir> <s7-source-dir> <smoke-exe> <work-dir>
set -euo pipefail
BUILD_DIR="$1" SOURCE_DIR="$2" SMOKE="$3" WORK_DIR="$4"

if ! pkg-config --exists fftw3f; then
    echo "fftw3f not found; skipping third-party plugin test"
    exit 77
fi

rm -rf "$WORK_DIR"
cmake --install "$BUILD_DIR" --component s7sdk --prefix "$WORK_DIR/sdk" > /dev/null
cmake -S "$SOURCE_DIR/s7spectral" -B "$WORK_DIR/build" -DCMAKE_PREFIX_PATH="$WORK_DIR/sdk" \
    -DCMAKE_CXX_COMPILER="${CXX:-c++}" > /dev/null
cmake --build "$WORK_DIR/build" > /dev/null
"$SMOKE" "$WORK_DIR/build/libs7spectral.so"
