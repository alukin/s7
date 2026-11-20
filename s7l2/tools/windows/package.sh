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
BUILD="$(realpath "$1")" OUT="$(realpath -m "$2")"
SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
case "$SOURCE/" in "$OUT/"*) echo "error: output contains the source directory" >&2; exit 2;; esac
case "$BUILD/" in "$OUT/"*) echo "error: output contains the build directory" >&2; exit 2;; esac
[ "$OUT" != "$HOME" ] || { echo "error: output cannot be the home directory" >&2; exit 2; }
SYSROOT="$(sed -n 's/^S7_MINGW_SYSROOT:PATH=//p' "$BUILD/CMakeCache.txt")"
SUFFIX="$(sed -n 's/^S7_LLVM_SUFFIX:STRING=//p' "$BUILD/CMakeCache.txt")"
find_tool() {
    for candidate in "$@"; do
        if command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return
        fi
    done
    echo "error: no tool found among: $*" >&2
    return 1
}
OBJDUMP="$(find_tool "llvm-objdump${SUFFIX}" llvm-objdump x86_64-w64-mingw32-objdump objdump)"
STRIP="$(find_tool "llvm-strip${SUFFIX}" llvm-strip x86_64-w64-mingw32-strip strip)"
[ -d "$SYSROOT/bin" ] || { echo "error: $BUILD is not a Windows cross build (no S7_MINGW_SYSROOT)" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/plugins"

for program in s7runtime/s7runtime s7runtime/s7analysis_lab s7ctl/s7ctl s7studio/s7studio s7hw; do
    [ -f "$BUILD/$program.exe" ] && cp "$BUILD/$program.exe" "$OUT/"
done
cp "$BUILD/s7core/libs7core.dll" "$OUT/"
for module in s7hardware s7audio; do
    cp "$BUILD/$module/lib$module.dll" "$OUT/"
done
cp "$BUILD/s7audio/libs7audio-elements.dll" "$OUT/plugins/"
if grep -q '^S7_BUILD_SDR:BOOL=ON$' "$BUILD/CMakeCache.txt"; then
    cp "$BUILD/s7sdr/libs7sdr.dll" "$OUT/"
    cp "$BUILD/s7sdr/libs7sdr-elements.dll" "$BUILD/s7radio/libs7radio-elements.dll" "$OUT/plugins/"
fi
cp "$BUILD/s7noise/libs7noise.dll" "$OUT/plugins/"
for module in s7spectral s7wavelet; do
    plugin="$BUILD/analysis-plugins/install/lib/s7/plugins/lib$module.dll"
    [ ! -f "$plugin" ] || cp "$plugin" "$OUT/plugins/"
done
if [ -f "$OUT/s7studio.exe" ]; then
    # Qt looks for its plugins next to the executable.
    for plugin in platforms/qwindows.dll platforms/qoffscreen.dll styles/qmodernwindowsstyle.dll \
                  iconengines/qsvgicon.dll imageformats/qsvg.dll; do
        mkdir -p "$OUT/$(dirname "$plugin")"
        cp "$SYSROOT/share/qt6/plugins/$plugin" "$OUT/$plugin"
    done
    # QML imports of s7studio/src/viewers/qml/*.qml and what they import in
    # turn; Qt looks for them in qml/ next to the executable.
    for module in QtQml QtQuick QtGraphs QtQuick3D; do
        mkdir -p "$OUT/qml"
        cp -r "$SYSROOT/share/qt6/qml/$module" "$OUT/qml/$module"
    done
    # Qt 6.12 loads its 2D graph implementation through this separate QML
    # module. Its plugin pulls Qt6Graphs2DImpl.dll into the dependency walk.
    if [ -d "$SYSROOT/share/qt6/qml/QtGraphs2D" ]; then
        cp -r "$SYSROOT/share/qt6/qml/QtGraphs2D" "$OUT/qml/QtGraphs2D"
    fi
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
python3 -B "$SOURCE/tools/teaching_assets.py" copy "$SOURCE" "$OUT/share/s7"
cp "$SOURCE/s7core/resources/schema_elements.s7library" "$OUT/share/s7/"
cp "$SOURCE/docs/SPLIT_TRANSPORT.md" "$OUT/share/s7/"
cp "$SOURCE/tools/windows/BUNDLE_README.md" "$OUT/README.md"
# These Linux-specific examples explicitly select PipeWire; Windows uses PortAudio.
rm -f "$OUT/share/s7/examples/hardware/audio_capture.json" "$OUT/share/s7/examples/hardware/audio_playback.json"

# Symbol tables are no use without the build tree (the sysroot's DLLs are stripped).
"$STRIP" --strip-all "$OUT"/*.exe "$OUT"/*.dll "$OUT"/plugins/*.dll

# The DLLs a module imports that the sysroot provides (the rest are Windows').
imports() { "$OBJDUMP" -p "$1" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p'; }
pending=("$OUT"/*.exe "$OUT"/*.dll "$OUT"/plugins/*.dll "$OUT"/platforms/*.dll "$OUT"/styles/*.dll \
         "$OUT"/iconengines/*.dll "$OUT"/imageformats/*.dll)
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
python3 - "$BUILD/CMakeCache.txt" "$OUT/build-profile.json" "$SOURCE" "$SYSROOT" <<'PY'
import json
from pathlib import Path
import sys
import subprocess
cache = dict(line.split('=', 1) for line in Path(sys.argv[1]).read_text().splitlines()
             if line.startswith(('S7_BUILD_SDR:', 'S7_PORTAUDIO:', 'S7_PIPEWIRE:', 'S7_SOAPYSDR:')))
revision = subprocess.run(['git', '-C', sys.argv[3], 'rev-parse', 'HEAD'], capture_output=True, text=True)
dirty = subprocess.run(['git', '-C', sys.argv[3], 'diff', '--quiet', 'HEAD', '--', '.'])
Path(sys.argv[2]).write_text(json.dumps({'platform': 'windows-x86_64', 'build_options': cache,
    'source_revision': revision.stdout.strip() if revision.returncode == 0 else 'unknown',
    'source_modified': dirty.returncode != 0 if revision.returncode == 0 else None,
    'sysroot_packages': (Path(sys.argv[4]) / '.s7-packages').read_text().splitlines(),
    'binary_inventory': 'licenses/inventory.json'}, indent=2) + '\n')
PY

echo "Packaged $(find "$OUT" -name '*.exe' | wc -l) programs and $(find "$OUT" -name '*.dll' | wc -l) DLLs into $OUT ($(du -sh "$OUT" | cut -f1))"
