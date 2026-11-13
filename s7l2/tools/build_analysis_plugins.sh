#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: MIT-0
# Build the optional analysis plugins against this build's installed SDK.
set -euo pipefail
BUILD_DIR="$(realpath "${1:?usage: build_analysis_plugins.sh BUILD_DIR}")"
SOURCE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK_DIR="$BUILD_DIR/analysis-plugins"
SDK="$WORK_DIR/install"
mkdir -p "$WORK_DIR"
cmake --install "$BUILD_DIR" --component s7sdk --prefix "$SDK"
CROSS=()
TOOLCHAIN="$(sed -n 's/^CMAKE_TOOLCHAIN_FILE:FILEPATH=//p' "$BUILD_DIR/CMakeCache.txt")"
COMPILER="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$BUILD_DIR/CMakeCache.txt")"
if [ -z "$COMPILER" ]; then
    COMPILER="$(sed -n 's/^set(CMAKE_CXX_COMPILER "\(.*\)")/\1/p' "$BUILD_DIR"/CMakeFiles/*/CMakeCXXCompiler.cmake | head -1)"
fi
export PATH="$(dirname "$COMPILER"):$PATH"
if [ -n "$TOOLCHAIN" ]; then CROSS=(--toolchain "$TOOLCHAIN"); fi
for module in s7spectral s7wavelet; do
    cmake -S "$SOURCE_DIR/$module" -B "$WORK_DIR/$module" "${CROSS[@]}" \
        -DCMAKE_CXX_COMPILER="$COMPILER" -DCMAKE_PREFIX_PATH="$SDK" \
        -Ds7core_DIR="$SDK/lib/cmake/s7core" -Ds7spectral_DIR="$SDK/lib/cmake/s7spectral" -DBUILD_TESTING=OFF
    cmake --build "$WORK_DIR/$module" --parallel 4
    cmake --install "$WORK_DIR/$module" --prefix "$SDK"
done
printf 'Analysis plugin directory: %s\n' "$SDK/lib/s7/plugins"
