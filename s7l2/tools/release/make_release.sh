#!/usr/bin/env bash
# Packages a native Linux build and/or a Windows cross build into archives
# ready to upload to a release: a .tar.gz for Linux (via tools/linux/package.sh)
# and a .zip for Windows (via tools/windows/package.sh), plus native RPM/DEB
# artifacts and a SHA256SUMS file covering all binary artifacts.
#
# Usage: tools/release/make_release.sh <version> <dist-dir> \
#            [--linux-build <dir>] [--windows-build <dir>] [--native-packages <dir>]...
#
# Example:
#   tools/release/make_release.sh 0.3.0 dist --linux-build build --windows-build build-win-release
#
# Does not upload anything; upload the files it writes to <dist-dir>
# yourself (e.g. `gh release create`, or `gh release upload`).

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

if [ $# -lt 2 ]; then
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
VERSION="$1" DIST="$(realpath -m "$2")"
[[ "$VERSION" =~ ^[0-9][A-Za-z0-9.+-]*$ ]] || { echo "error: invalid release version" >&2; exit 2; }
shift 2
LINUX_BUILD="" WINDOWS_BUILD=""
NATIVE_PACKAGES=()
while [ $# -gt 0 ]; do
    [ $# -ge 2 ] || { echo "error: missing value for $1" >&2; exit 2; }
    case "$1" in
        --linux-build) LINUX_BUILD="$2"; shift 2 ;;
        --windows-build) WINDOWS_BUILD="$2"; shift 2 ;;
        --native-packages) NATIVE_PACKAGES+=("$(realpath "$2")"); shift 2 ;;
        *) echo "error: unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$LINUX_BUILD$WINDOWS_BUILD" ] && [ ${#NATIVE_PACKAGES[@]} -eq 0 ]; then
    echo "error: pass --linux-build, --windows-build and/or --native-packages" >&2; exit 2
fi

SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
# Bundle trees can be large; keep the temporary staging tree on the output filesystem.
mkdir -p "$DIST"
WORK="$(mktemp -d "$DIST/.s7-release-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
ARTIFACTS=()

if [ -n "$LINUX_BUILD" ]; then
    NAME="s7studio-$VERSION-linux-x86_64"
    "$SOURCE/tools/linux/package.sh" "$LINUX_BUILD" "$WORK/$NAME"
    tar -czf "$DIST/$NAME.tar.gz" -C "$WORK" "$NAME"
    echo "Wrote $DIST/$NAME.tar.gz"
    ARTIFACTS+=("$NAME.tar.gz")
fi

if [ -n "$WINDOWS_BUILD" ]; then
    NAME="s7studio-$VERSION-windows-x86_64"
    "$SOURCE/tools/windows/package.sh" "$WINDOWS_BUILD" "$WORK/$NAME"
    (cd "$WORK" && zip -rq "$DIST/$NAME.zip" "$NAME")
    echo "Wrote $DIST/$NAME.zip"
    ARTIFACTS+=("$NAME.zip")
fi

for packages in "${NATIVE_PACKAGES[@]}"; do
    label="$(basename "$packages")"
    [[ "$label" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "error: invalid native package directory name" >&2; exit 2; }
    target="$DIST/native/$label"
    [ ! -e "$target" ] || { echo "error: native output already exists: $target" >&2; exit 2; }
    [ ! -f "$packages/SHA256SUMS" ] || (cd "$packages" && sha256sum -c SHA256SUMS)
    shopt -s nullglob
    binaries=("$packages"/*.rpm "$packages"/*.deb)
    [ ${#binaries[@]} -gt 0 ] || { echo "error: no RPM/DEB files in $packages" >&2; exit 2; }
    mkdir -p "$target"
    for binary in "${binaries[@]}"; do
        cp "$binary" "$target/"
        ARTIFACTS+=("native/$label/$(basename "$binary")")
    done
    for metadata in SHA256SUMS source-files.sha256 build-info.json; do
        [ ! -f "$packages/$metadata" ] || cp "$packages/$metadata" "$target/"
    done
    [ ! -d "$packages/verification" ] || cp -r "$packages/verification" "$target/"
done
(cd "$DIST" && sha256sum "${ARTIFACTS[@]}" > SHA256SUMS)
echo "Wrote $DIST/SHA256SUMS"
