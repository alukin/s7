#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record a non-sensitive S7 build/test environment profile for comparable runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def command_version(cmd):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if result.returncode:
            return None
        return result.stdout.strip().splitlines()[0] if result.stdout.strip() else None
    except (OSError, subprocess.SubprocessError, IndexError):
        return None


def build_profile():
    commit = command_version(["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    dirty = None
    try:
        check = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain"],
                               capture_output=True, text=True, timeout=10)
        if check.returncode == 0:
            dirty = bool(check.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        pass
    data = {"schema_version": 1, "repo_sha": commit, "dirty_worktree": dirty,
            "os": platform.system(), "os_release": platform.release(),
            "architecture": platform.machine(), "python": platform.python_version(),
            "cpu_count": os.cpu_count(),
            "cmake": command_version(["cmake", "--version"]),
            "cxx": command_version(["c++", "--version"]),
            "qt6_core": command_version(["pkg-config", "--modversion", "Qt6Core"])}
    # Exclude a hostname, username, addresses and environment variables.
    canonical = json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8")
    data["fingerprint_sha256"] = hashlib.sha256(canonical).hexdigest()
    return data


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, help="optional JSON file (refuses overwrite)")
    args = p.parse_args(argv)
    try:
        profile = build_profile()
        serialized = json.dumps(profile, indent=2) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as out:
                out.write(serialized)
        print(serialized, end="")
        return 0
    except OSError as exc:
        print(f"environment probe error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
