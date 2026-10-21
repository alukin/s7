#!/usr/bin/env bash
# Collects a Windows cross build into a self-contained directory that runs on
# Windows (or under Wine) as is: the programs, libs7core.dll, the plugins in
# plugins/, Qt's platform and style plugins, the QML modules Studio's viewers
# import (qml/), the example schemas, and every DLL they import from the MSYS2
# sysroot.
#
# Usage: tools/windows/package.sh <build-dir> <output-dir>
#
# Start the programs in <output-dir> from any working directory.

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

if [ $# -ne 2 ]; then
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
BUILD="$1" OUT="$2"
SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
SYSROOT="$(sed -n 's/^S7_MINGW_SYSROOT:PATH=//p' "$BUILD/CMakeCache.txt")"
SUFFIX="$(sed -n 's/^S7_LLVM_SUFFIX:STRING=//p' "$BUILD/CMakeCache.txt")"
OBJDUMP="llvm-objdump${SUFFIX}"
[ -d "$SYSROOT/bin" ] || { echo "error: $BUILD is not a Windows cross build (no S7_MINGW_SYSROOT)" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/plugins"

for program in s7runtime/s7runtime s7ctl/s7ctl s7studio/s7studio; do
    [ -f "$BUILD/$program.exe" ] && cp "$BUILD/$program.exe" "$OUT/"
done
cp "$BUILD/s7core/libs7core.dll" "$OUT/"
cp "$BUILD/s7noise/libs7noise.dll" "$OUT/plugins/"
if [ -f "$OUT/s7studio.exe" ]; then
    # Qt looks for its plugins next to the executable.
    for plugin in platforms/qwindows.dll styles/qmodernwindowsstyle.dll; do
        mkdir -p "$OUT/$(dirname "$plugin")"
        cp "$SYSROOT/share/qt6/plugins/$plugin" "$OUT/$plugin"
    done
    # QML imports of s7studio/src/viewers/qml/*.qml and what they import in
    # turn; Qt looks for them in qml/ next to the executable.
    for module in QtQml QtQuick QtGraphs QtQuick3D; do
        mkdir -p "$OUT/qml"
        cp -r "$SYSROOT/share/qt6/qml/$module" "$OUT/qml/$module"
    done
    # Submodules the viewers do not import (and the DLLs they would pull in).
    for unused in QtQuick/Controls QtQuick/Dialogs QtQuick/NativeStyle QtQuick/Particles QtQuick/tooling \
                  QtQuick3D/AssetUtils QtQuick3D/Effects QtQuick3D/MaterialEditor QtQuick3D/ParticleEffects \
                  QtQuick3D/Particles3D QtQuick3D/Xr QtQuick3D/designer QtQuick3D/lightmapviewer; do
        rm -rf "${OUT:?}/qml/$unused"
    done
    # MSYS2's Qt looks for QML modules under <prefix>/share/qt6/qml; point it
    # at qml/ here. Qt plugins (platforms/, styles/) stay next to the program.
    printf '[Paths]\nPrefix = .\nPlugins = .\nQmlImports = qml\n' > "$OUT/qt.conf"
fi
cp -r "$SOURCE/s7runtime/examples" "$OUT/examples"
mkdir -p "$OUT/share/s7"
cp -r "$SOURCE/examples" "$OUT/share/s7/examples"
cp "$SOURCE/s7core/resources/schema_elements.s7library" "$OUT/share/s7/"

# Symbol tables are no use without the build tree (the sysroot's DLLs are stripped).
"llvm-strip${SUFFIX}" --strip-all "$OUT"/*.exe "$OUT"/*.dll "$OUT"/plugins/*.dll

# The DLLs a module imports that the sysroot provides (the rest are Windows').
imports() { "$OBJDUMP" -p "$1" | sed -n 's/^ *DLL Name: //p'; }
pending=("$OUT"/*.exe "$OUT"/*.dll "$OUT"/plugins/*.dll "$OUT"/platforms/*.dll "$OUT"/styles/*.dll)
if [ -d "$OUT/qml" ]; then
    while IFS= read -r -d '' module; do pending+=("$module"); done < <(find "$OUT/qml" -name '*.dll' -print0)
fi
while [ ${#pending[@]} -gt 0 ]; do
    module="${pending[0]}"
    pending=("${pending[@]:1}")
    [ -f "$module" ] || continue
    while read -r dll; do
        if [ ! -f "$OUT/$dll" ] && [ -f "$SYSROOT/bin/$dll" ]; then
            cp "$SYSROOT/bin/$dll" "$OUT/"
            pending+=("$OUT/$dll")
        fi
    done < <(imports "$module")
done

python3 "$SOURCE/tools/package_notices.py" "$SOURCE" "$BUILD" "$OUT" --sysroot "$SYSROOT"

echo "Packaged $(find "$OUT" -name '*.exe' | wc -l) programs and $(find "$OUT" -name '*.dll' | wc -l) DLLs into $OUT ($(du -sh "$OUT" | cut -f1))"
