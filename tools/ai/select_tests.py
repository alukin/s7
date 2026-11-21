#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Recommend and optionally run focused CTest checks for changed S7 files."""
import argparse
import fnmatch
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MAP = Path(__file__).with_name("test_map.json")


def changed_from_git(base, include_untracked=False):
    # --end-of-options prevents branch names beginning with '-' from being parsed as flags.
    sha = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify",
                          "--end-of-options", base + "^{commit}"], check=True, text=True,
                         capture_output=True).stdout.strip()
    output = subprocess.run(["git", "-C", str(ROOT), "diff", "--name-only",
                             "--no-ext-diff", sha, "--"], capture_output=True,
                            text=True, check=True).stdout.splitlines()
    if include_untracked:
        output += subprocess.run(["git", "-C", str(ROOT), "ls-files", "--others",
                                  "--exclude-standard"], capture_output=True,
                                 text=True, check=True).stdout.splitlines()
    return sorted(set(output))


def select(files, rules, include_gui=False):
    expressions, gui_expressions, notes, mapped, unmapped = [], [], [], [], []
    for path in files:
        matches = [rule for rule in rules if fnmatch.fnmatchcase(path, rule["pattern"])]
        if not matches:
            unmapped.append(path)
            continue
        mapped.append(path)
        for rule in matches:
            expressions.extend(rule.get("ctest", []))
            gui_expressions.extend(rule.get("gui", []))
            notes.extend(rule.get("notes", []))
    # Keep patterns separate to make missing matches observable.
    unique = lambda seq: list(dict.fromkeys(seq))
    patterns = unique(expressions)
    gui = unique(gui_expressions)
    return {"mapped_files": mapped, "unmapped_files": unmapped,
            "ctest_patterns": patterns + (gui if include_gui else []),
            "gui_patterns_deferred": [] if include_gui else gui,
            "notes": unique(notes)}


def catalog(build_dir):
    result = subprocess.run(["ctest", "--test-dir", str(build_dir),
                             "--show-only=json-v1"], capture_output=True, text=True, check=True)
    value = json.loads(result.stdout)
    return [test["name"] for test in value.get("tests", [])]


def find_tests(names, patterns):
    selected = []
    missing = []
    for expression in patterns:
        regex = re.compile(expression)
        matched = [name for name in names if regex.fullmatch(name)]
        if not matched:
            missing.append(expression)
        else:
            selected.extend(matched)
    return sorted(set(selected)), missing


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    changed = parser.add_mutually_exclusive_group(required=True)
    changed.add_argument("--base", help="Git revision to diff against the current worktree")
    changed.add_argument("--changed", nargs="+", metavar="PATH",
                         help="changed repository-relative filenames")
    parser.add_argument("--include-untracked", action="store_true",
                        help="include untracked files with --base")
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP)
    parser.add_argument("--build-dir", type=Path, help="existing configured CMake build dir")
    parser.add_argument("--include-gui", action="store_true",
                        help="opt in to local-only Studio GUI checks")
    parser.add_argument("--run", action="store_true",
                        help="run selected CTest cases (requires --build-dir)")
    parser.add_argument("--strict", action="store_true",
                        help="fail for unmapped files or patterns missing from CTest catalog")
    args = parser.parse_args(argv)
    if args.run and not args.build_dir:
        parser.error("--run requires --build-dir")
    if args.include_untracked and not args.base:
        parser.error("--include-untracked requires --base")
    try:
        configuration = json.loads(args.map.read_text(encoding="utf-8"))
        rules = configuration["rules"]
        files = changed_from_git(args.base, args.include_untracked) if args.base else args.changed
        report = {"changed_files": sorted(set(files)), **select(files, rules, args.include_gui)}
        report["selected_ctest"] = []
        report["missing_ctest_patterns"] = []
        report["status"] = "recommendations_only"
        if not files:
            report["notes"].append("No changed files found; no tests selected.")
        if args.build_dir:
            path = args.build_dir.resolve()
            if not path.is_dir():
                raise ValueError("build directory does not exist: " + str(path))
            names = catalog(path)
            matched, missing = find_tests(names, report["ctest_patterns"])
            report["selected_ctest"] = matched
            report["missing_ctest_patterns"] = missing
            report["status"] = "matched_ctest_catalog"
            if args.run:
                if not matched:
                    raise ValueError("no selected tests match this CTest catalog; refusing false success")
                escaped = "|".join(re.escape(t) for t in matched)
                command = ["ctest", "--test-dir", str(path), "-R",
                           "^(?:" + escaped + ")$", "--no-tests=error", "--output-on-failure"]
                outcome = subprocess.run(command, check=False)
                report["ctest_command"] = command
                report["ctest_returncode"] = outcome.returncode
                report["status"] = "tests_passed" if outcome.returncode == 0 else "tests_failed"
        if args.strict and (report["unmapped_files"] or report["missing_ctest_patterns"]):
            report["status"] = "incomplete_mapping"
        print(json.dumps(report, indent=2))
        if report["status"] in ("tests_failed", "incomplete_mapping"):
            return 1
        return 0
    except (OSError, ValueError, KeyError, re.error, subprocess.SubprocessError) as exc:
        print(json.dumps({"status": "failed", "error": f"{type(exc).__name__}: {exc}"},
                         indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
