#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Build native packages on Fedora or Ubuntu, using system dependencies.
# Usage: build_native_packages.sh [--build-dir DIR] [--output-dir DIR] [--no-studio] [--test]
# Set S7_BUILD_JOBS (default 2) to limit compiler memory use.
set -euo pipefail
SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
. /etc/os-release
case "$ID" in
    fedora) GENERATOR=RPM ;;
    ubuntu) GENERATOR=DEB ;;
    *) echo "Native packages currently target Fedora and Ubuntu" >&2; exit 2 ;;
esac
for tool in cmake cpack ninja file; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; run the dependency installer with --native-packages" >&2; exit 2; }
done
if [ "$GENERATOR" = RPM ]; then NATIVE_TOOL=rpmbuild; else NATIVE_TOOL=dpkg-shlibdeps; fi
command -v "$NATIVE_TOOL" >/dev/null || { echo "Missing $NATIVE_TOOL; run the dependency installer with --native-packages" >&2; exit 2; }
BUILD="$SOURCE/build-native-${ID}${VERSION_ID}"
OUT="$SOURCE/dist/native/${ID}${VERSION_ID}"
STUDIO=ON
TEST=0
while (($#)); do
    case "$1" in
        --build-dir|--output-dir)
            [ $# -ge 2 ] || { echo "Missing directory for $1" >&2; exit 2; }
            if [ "$1" = --build-dir ]; then BUILD="$2"; else OUT="$2"; fi
            shift 2 ;;
        --no-studio) STUDIO=OFF; shift ;;
        --test) TEST=1; shift ;;
        -h|--help) sed -n '4,6s/^# //p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
JOBS="${S7_BUILD_JOBS:-2}"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "S7_BUILD_JOBS must be a positive integer" >&2; exit 2; }
BUILD="$(realpath -m "$BUILD")"
OUT="$(realpath -m "$OUT")"
cmake -S "$SOURCE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DS7_NATIVE_PACKAGES=ON \
    -DS7_BUILD_STUDIO="$STUDIO" -DS7_BUILD_SDR=ON -DS7_BUILD_GUI_REGRESSION_TESTS=OFF \
    -DS7_PORTAUDIO=ON -DS7_PIPEWIRE=ON -DS7_SOAPYSDR=ON \
    -DS7_FRAME_LZ4=ON -DS7_FRAME_ZSTD=ON
cmake --build "$BUILD" --parallel "$JOBS"
S7_BUILD_JOBS="$JOBS" "$SOURCE/tools/build_analysis_plugins.sh" "$BUILD"
cmake -S "$SOURCE" -B "$BUILD" -DS7_NATIVE_ANALYSIS_PREFIX="$BUILD/analysis-plugins/install"
if ((TEST)); then
    cmake --build "$BUILD" --target build-tests --parallel "$JOBS"
    ctest --test-dir "$BUILD" --output-on-failure --parallel "$JOBS"
fi
mkdir -p "$OUT"
cpack --config "$BUILD/CPackConfig.cmake" -G "$GENERATOR" -B "$OUT"
python3 "$SOURCE/tools/linux/package_manifest.py" "$SOURCE" "$BUILD" "$OUT"
