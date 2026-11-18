#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Build distro-specific RPMs/DEBs without installing development packages on the host.
# Usage: build_native_container.sh fedora:45|ubuntu:26.04|ubuntu:26.10 [--no-studio] [--test]
# S7_CONTAINER_ENGINE selects docker (default) or podman. S7_BUILD_JOBS defaults to 2.
set -euo pipefail
SOURCE="$(cd "$(dirname "$0")/../.." && pwd)"
case "${1:-}" in
    fedora:45) IMAGE=registry.fedoraproject.org/fedora:45; INSTALLER=install_deps_fedora.sh; LABEL=fedora45 ;;
    ubuntu:26.04) IMAGE=docker.io/library/ubuntu:26.04; INSTALLER=install_deps_ubuntu.sh; LABEL=ubuntu26.04 ;;
    ubuntu:26.10) IMAGE=docker.io/library/ubuntu:26.10; INSTALLER=install_deps_ubuntu.sh; LABEL=ubuntu26.10 ;;
    -h|--help|'') sed -n '4,6s/^# //p' "$0"; exit 0 ;;
    *) echo "Supported targets: fedora:45, ubuntu:26.04, ubuntu:26.10" >&2; exit 2 ;;
esac
shift
BUILD_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --no-studio) BUILD_ARGS+=(--no-studio) ;;
        --test) BUILD_ARGS+=(--test) ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done
BUILD="$SOURCE/build-native-container-$LABEL"
OUT="$SOURCE/dist/native/$LABEL"
mkdir -p "$BUILD" "$OUT"
# /src includes the parent license files and the QSchematic submodule, read-only.
"${S7_CONTAINER_ENGINE:-docker}" run --rm --security-opt label=disable \
    --volume "$(dirname "$SOURCE"):/src:ro" --volume "$BUILD:/build:rw" --volume "$OUT:/packages:rw" \
    --env DEBIAN_FRONTEND=noninteractive --env S7_BUILD_JOBS="${S7_BUILD_JOBS:-2}" \
    --workdir /src/s7l2 "$IMAGE" bash -ec '
        installer=$1; shift
        install_args=(--native-packages --no-gui-tests --no-extras)
        for arg in "$@"; do
            if [ "$arg" = --no-studio ]; then install_args+=(--no-studio); fi
        done
        /src/s7l2/tools/"$installer" "${install_args[@]}"
        /src/s7l2/tools/linux/build_native_packages.sh --build-dir /build --output-dir /packages "$@"
    ' native-build "$INSTALLER" "${BUILD_ARGS[@]}"
