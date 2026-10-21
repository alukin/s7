#!/usr/bin/env bash
# Packages a native Linux build and/or a Windows cross build into archives
# ready to upload to a release: a .tar.gz for Linux (via tools/linux/package.sh)
# and a .zip for Windows (via tools/windows/package.sh), plus a SHA256SUMS
# file covering both. Pass only one build dir to release for one platform.
#
# Usage: tools/release/make_release.sh <version> <dist-dir> \
#            [--linux-build <dir>] [--windows-build <dir>]
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
VERSION="$1" DIST="$2"
shift 2
LINUX_BUILD="" WINDOWS_BUILD=""
while [ $# -gt 0 ]; do
    case "$1" in
        --linux-build) LINUX_BUILD="$2"; shift 2 ;;
        --windows-build) WINDOWS_BUILD="$2"; shift 2 ;;
        *) echo "error: unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$LINUX_BUILD$WINDOWS_BUILD" ] || { echo "error: pass --linux-build and/or --windows-build" >&2; exit 2; }

SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$DIST"

if [ -n "$LINUX_BUILD" ]; then
    NAME="s7studio-$VERSION-linux-x86_64"
    "$SOURCE/tools/linux/package.sh" "$LINUX_BUILD" "$WORK/$NAME"
    tar -czf "$DIST/$NAME.tar.gz" -C "$WORK" "$NAME"
    echo "Wrote $DIST/$NAME.tar.gz"
fi

if [ -n "$WINDOWS_BUILD" ]; then
    NAME="s7studio-$VERSION-windows-x86_64"
    "$SOURCE/tools/windows/package.sh" "$WINDOWS_BUILD" "$WORK/$NAME"
    (cd "$WORK" && zip -rq "$DIST/$NAME.zip" "$NAME")
    echo "Wrote $DIST/$NAME.zip"
fi

(cd "$DIST" && sha256sum ./*.tar.gz ./*.zip 2>/dev/null > SHA256SUMS)
echo "Wrote $DIST/SHA256SUMS"
