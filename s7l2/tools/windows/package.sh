#!/usr/bin/env bash
# Collects a Windows cross build into a self-contained directory that runs on
# Windows (or under Wine) as is: the programs, libs7core.dll, the plugins in
# plugins/, Qt's platform and style plugins, the example schemas, and every
# DLL they import from the MSYS2 sysroot.
#
# Usage: tools/windows/package.sh <build-dir> <output-dir>
#
# Start the programs in <output-dir> (s7studio.exe from Explorer does): they
# find the plugins in ./plugins.
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
fi
cp -r "$SOURCE/s7runtime/examples" "$OUT/examples"
# Symbol tables are no use without the build tree (the sysroot's DLLs are stripped).
"llvm-strip${SUFFIX}" --strip-all "$OUT"/*.exe "$OUT"/*.dll "$OUT"/plugins/*.dll

# The DLLs a module imports that the sysroot provides (the rest are Windows').
imports() { "$OBJDUMP" -p "$1" | sed -n 's/^ *DLL Name: //p'; }
pending=("$OUT"/*.exe "$OUT"/*.dll "$OUT"/plugins/*.dll "$OUT"/platforms/*.dll "$OUT"/styles/*.dll)
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

echo "Packaged $(find "$OUT" -name '*.exe' | wc -l) programs and $(find "$OUT" -name '*.dll' | wc -l) DLLs into $OUT ($(du -sh "$OUT" | cut -f1))"
