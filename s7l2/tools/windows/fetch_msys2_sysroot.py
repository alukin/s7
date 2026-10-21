#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Fetch the Windows (x86_64, UCRT) sysroot for cross-building S7 on Linux.

Downloads MSYS2 UCRT64 packages -- the mingw-w64 CRT and headers, GCC's
libstdc++ (headers, import libraries, runtime DLLs) and the S7 dependencies
(Boost, OpenSSL, zlib, Qt6, FFTW) with everything they depend on -- verifies
their SHA-256 sums against the repository database and unpacks them into one
directory. Clang then cross-compiles against it with
cmake/toolchains/x86_64-w64-mingw32-clang.cmake.

Usage: tools/windows/fetch_msys2_sysroot.py [DEST] [--mirror URL] [--no-studio] [--no-spectral]

DEST defaults to ~/.cache/s7/msys2-ucrt64. Rerunning updates DEST to the
packages the repository currently ships (downloads are cached in DEST/.cache).
Needs Python 3 and the zstd command.
"""

import argparse
import hashlib
import io
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

PREFIX = "mingw-w64-ucrt-x86_64-"

# Compiler support: libstdc++ headers and libraries live in the gcc package.
TOOLCHAIN = ["gcc", "gcc-libs", "crt", "headers", "winpthreads", "libwinpthread"]
CORE = ["boost", "openssl", "zlib"]
# Studio's observation viewers: Qt Quick (declarative), Qt Graphs, Qt Quick 3D.
STUDIO = ["qt6-base", "qt6-declarative", "qt6-graphs", "qt6-quick3d"]
SPECTRAL = ["fftw", "pkgconf"]

# Dependencies needed only to run the Windows-hosted compiler or build tools,
# never by what S7 links or loads.
SKIP = {"binutils", "isl", "mpc", "gmp", "mpfr", "python", "tcl", "tk", "ncurses",
        "readline", "windows-default-manifest"}


def run_zstd(data: bytes) -> bytes:
    return subprocess.run(["zstd", "-dc"], input=data, capture_output=True, check=True).stdout


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def parse_db(data: bytes) -> dict:
    packages = {}
    with tarfile.open(fileobj=io.BytesIO(run_zstd(data))) as db:
        for member in db:
            if not member.name.endswith("/desc"):
                continue
            fields, key = {}, None
            for line in db.extractfile(member).read().decode().splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line.strip("%")
                    fields[key] = []
                elif line and key:
                    fields[key].append(line)
            packages[fields["NAME"][0]] = fields
    return packages


def bare_name(dependency: str) -> str:
    return re.split(r"[<>=]", dependency, maxsplit=1)[0]


def resolve(packages: dict, wanted: list) -> list:
    provides = {}
    for name, fields in packages.items():
        for provided in fields.get("PROVIDES", []):
            provides.setdefault(bare_name(provided), name)
    closure, pending = [], [PREFIX + name for name in wanted]
    while pending:
        name = pending.pop()
        name = name if name in packages else provides.get(name, name)
        if name in closure:
            continue
        if name not in packages:
            sys.exit(f"error: package {name} is not in the repository")
        closure.append(name)
        for dependency in packages[name].get("DEPENDS", []):
            dependency = bare_name(dependency)
            if dependency.removeprefix(PREFIX) not in SKIP:
                pending.append(dependency)
    return sorted(closure)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dest", nargs="?", default=pathlib.Path.home() / ".cache/s7/msys2-ucrt64", type=pathlib.Path)
    parser.add_argument("--mirror", default="https://repo.msys2.org/mingw/ucrt64")
    parser.add_argument("--no-studio", action="store_true", help="skip Qt6 (for -DS7_BUILD_STUDIO=OFF)")
    parser.add_argument("--no-spectral", action="store_true", help="skip FFTW (needed only by s7spectral)")
    args = parser.parse_args()
    if shutil.which("zstd") is None:
        sys.exit("error: the zstd command is required (apt install zstd)")

    wanted = TOOLCHAIN + CORE + ([] if args.no_studio else STUDIO) + ([] if args.no_spectral else SPECTRAL)
    print(f"Reading {args.mirror}/ucrt64.db")
    packages = parse_db(fetch(f"{args.mirror}/ucrt64.db"))
    closure = resolve(packages, wanted)

    dest = args.dest.expanduser().resolve()
    cache = dest / ".cache"
    cache.mkdir(parents=True, exist_ok=True)
    stamp = dest / ".s7-packages"
    installed = stamp.read_text().split() if stamp.exists() else []
    files = [packages[name]["FILENAME"][0] for name in closure]
    if installed == files:
        print(f"{dest} is up to date ({len(files)} packages)")
        return

    # Unpack into a fresh tree so packages dropped or updated upstream leave no stale files.
    staging = dest.with_name(dest.name + ".new")
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir()
    for name, filename in zip(closure, files):
        archive = cache / filename
        expected = packages[name]["SHA256SUM"][0]
        if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            print(f"  downloading {filename}")
            data = fetch(f"{args.mirror}/{filename}")
            if hashlib.sha256(data).hexdigest() != expected:
                sys.exit(f"error: SHA-256 mismatch for {filename}")
            archive.write_bytes(data)
        with tarfile.open(fileobj=io.BytesIO(run_zstd(archive.read_bytes()))) as package:
            for member in package:
                if member.name.startswith("ucrt64/"):
                    member.name = member.name.removeprefix("ucrt64/")
                    if member.islnk():
                        member.linkname = member.linkname.removeprefix("ucrt64/")
                    package.extract(member, staging, filter="tar")
    for old in cache.iterdir():
        if old.name not in files:
            old.unlink()
    for entry in list(dest.iterdir()):
        if entry.name != ".cache":
            shutil.rmtree(entry) if entry.is_dir() and not entry.is_symlink() else entry.unlink()
    for entry in staging.iterdir():
        entry.rename(dest / entry.name)
    staging.rmdir()
    stamp.write_text("\n".join(files) + "\n")
    print(f"Installed {len(files)} packages into {dest}")


if __name__ == "__main__":
    main()
