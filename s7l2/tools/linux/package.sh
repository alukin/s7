#!/usr/bin/env bash
# Collects a native Linux build into a self-contained AppDir: the programs,
# libs7core.so, the plugins in plugins/, Qt and its plugins (xcb platform,
# svg icon engine, ...), and every shared library they need that isn't part
# of the base desktop stack every distro provides (glibc, X11/xcb, GL/EGL,
# D-Bus, fontconfig, ...). Built with linuxdeploy + linuxdeploy-plugin-qt,
# which maintain that base/bundle split; see NOTES.md next to this script.
#
# Usage: tools/linux/package.sh <build-dir> <output-dir>
#
# Start the programs in <output-dir> (./s7studio etc.) from a shell with
# <output-dir> as the working directory: they find the plugins in ./plugins.
#
# Set S7_MAX_GLIBC (e.g. 2.35) to fail the build if any bundled binary
# requires a newer glibc than your oldest supported target -- this script
# does not by itself make the package run on an older distro than the one
# it was built on; see NOTES.md.
#
# Tools are downloaded once and cached in S7_PACKAGE_CACHE
# (default: ~/.cache/s7-package).
set -euo pipefail

if [ $# -ne 2 ]; then
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
BUILD="$1" OUT="$2"
SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
CACHE="${S7_PACKAGE_CACHE:-$HOME/.cache/s7-package}"
LINUXDEPLOY_TAG="1-alpha-20251107-1"
LINUXDEPLOY_QT_TAG="1-alpha-20250213-1"
LINUXDEPLOY="$CACHE/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$CACHE/linuxdeploy-plugin-qt-x86_64.AppImage"

[ -f "$BUILD/CMakeCache.txt" ] || { echo "error: $BUILD is not a CMake build directory" >&2; exit 1; }
grep -q '^S7_MINGW_SYSROOT:PATH=.\+' "$BUILD/CMakeCache.txt" 2>/dev/null &&
    { echo "error: $BUILD is a Windows cross build; use tools/windows/package.sh" >&2; exit 1; }

mkdir -p "$CACHE"
fetch() { # fetch <url> <dest>
    [ -f "$2" ] || { echo "Fetching $(basename "$2")..."; curl -L --fail -o "$2.part" "$1"; mv "$2.part" "$2"; }
    chmod +x "$2"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$LINUXDEPLOY_QT_TAG/linuxdeploy-plugin-qt-x86_64.AppImage" "$LINUXDEPLOY_QT"
export PATH="$CACHE:$PATH" # linuxdeploy locates its plugin by searching PATH for linuxdeploy-plugin-*

rm -rf "$OUT"
mkdir -p "$OUT"

declare -a EXECUTABLES=()
for program in s7runtime/s7runtime s7ctl/s7ctl s7studio/s7studio; do
    [ -f "$BUILD/$program" ] && EXECUTABLES+=(--executable "$BUILD/$program")
done
[ ${#EXECUTABLES[@]} -gt 0 ] || { echo "error: no programs found under $BUILD" >&2; exit 1; }

declare -a LIBRARIES=()
[ -f "$BUILD/s7noise/libs7noise.so" ] && LIBRARIES+=(--library "$BUILD/s7noise/libs7noise.so")

# Only s7studio is a Qt/GUI application; the Qt plugin and a desktop
# file/icon (required by linuxdeploy to pick an AppDir entry point) are
# only needed when it was built.
declare -a QT_ARGS=()
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT
if [ -f "$BUILD/s7studio/s7studio" ]; then
    cat > "$WORKDIR/s7studio.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=S7 Studio
Exec=s7studio
Icon=s7studio
Categories=Development;Engineering;
EOF
    # Flat placeholder icon; replace with a real one if/when the project has one.
    base64 -d > "$WORKDIR/s7studio.png" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAAAOklEQVR42u3OQQ0AAAgEoMtlKFvZ0xbOBxsBSPW8EiEhISEhISEhISEhISEhISEhISEhISEhISGhOwsjprWm4kIWxAAAAABJRU5ErkJggg==
EOF
    QT_ARGS=(--desktop-file "$WORKDIR/s7studio.desktop" --icon-file "$WORKDIR/s7studio.png" --plugin qt)
fi

QMAKE="${QMAKE:-qmake6}" APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" \
    --appdir "$OUT" \
    "${EXECUTABLES[@]}" \
    "${LIBRARIES[@]}" \
    "${QT_ARGS[@]}"

# s7's own plugin loader (s7runtime/src/plugin_loader.cpp) looks in ./plugins
# relative to the current directory, skipping symlinks, so each plugin must
# be a real copy here -- separate from Qt's own plugins under usr/plugins/,
# which Qt finds via the qt.conf linuxdeploy-plugin-qt generated next to the
# executables. A loaded plugin's own need for libs7core.so.0 is satisfied by
# the copy already loaded into the process by s7studio/s7runtime/s7ctl
# themselves, not by this copy's own rpath, so relocating it here is safe.
if [ -f "$OUT/usr/lib/libs7noise.so" ]; then
    mkdir -p "$OUT/plugins"
    cp "$OUT/usr/lib/libs7noise.so" "$OUT/plugins/libs7noise.so"
fi

for program in s7runtime s7ctl s7studio; do
    [ -f "$OUT/usr/bin/$program" ] && ln -sf "usr/bin/$program" "$OUT/$program"
done
cp -r "$SOURCE/s7runtime/examples" "$OUT/examples"

# Base libraries every distro with a working desktop provides: these must
# come from the host (kernel/driver/X-or-Wayland-server/D-Bus version all
# have to match), so their presence here would mean linuxdeploy's bundling
# policy regressed -- not something to silently accept.
forbidden="$(find "$OUT/usr/lib" -maxdepth 1 -name '*.so*' 2>/dev/null |
    xargs -r -n1 basename |
    grep -E '^(libc|libm|libpthread|libdl)\.so(\.|$)|^ld-linux.*\.so|^libGL\.so(\.|$)|^libEGL\.so(\.|$)' || true)"
if [ -n "$forbidden" ]; then
    echo "error: base system libraries were bundled (should come from the host):" >&2
    echo "$forbidden" >&2
    exit 1
fi

# Highest glibc/libstdc++ symbol version any bundled binary needs. This is a
# property of the BUILD, not of this packaging step: it reflects the glibc
# on the machine that ran the compiler, and this script cannot lower it.
# See NOTES.md before assuming a package built here runs on an older distro.
max_ver() { # max_ver <prefix>
    find "$OUT" -name '*.so*' -o -type f -perm -u+x 2>/dev/null |
        while read -r f; do objdump -T "$f" 2>/dev/null; done |
        grep -o "${1}_[0-9.]*" | sort -V | tail -1
}
GLIBC_MAX="$(max_ver GLIBC)"
GLIBCXX_MAX="$(max_ver GLIBCXX)"
CXXABI_MAX="$(max_ver CXXABI)"
echo "Required: ${GLIBC_MAX:-none}, ${GLIBCXX_MAX:-none}, ${CXXABI_MAX:-none}"
if [ -n "${S7_MAX_GLIBC:-}" ] && [ -n "$GLIBC_MAX" ]; then
    highest="$(printf '%s\n%s\n' "${GLIBC_MAX#GLIBC_}" "$S7_MAX_GLIBC" | sort -V | tail -1)"
    [ "$highest" = "$S7_MAX_GLIBC" ] || {
        echo "error: package needs $GLIBC_MAX, which is newer than S7_MAX_GLIBC=$S7_MAX_GLIBC" >&2
        exit 1
    }
fi

echo "Packaged $(find "$OUT" -maxdepth 1 -type l | wc -l) programs and $(find "$OUT/usr/lib" -name '*.so*' 2>/dev/null | wc -l) bundled libraries into $OUT ($(du -sh "$OUT" | cut -f1))"
