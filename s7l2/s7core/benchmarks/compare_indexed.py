#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Compare two builds of the identical opt-in NodeRuntime benchmark.

Build the benchmark at the parent revision and this revision in separate build
directories, then pass their binary paths. If a baseline shared library was
copied out of its build tree, pass its directory as --baseline-lib-dir.
"""

import argparse
import csv
import io
import os
import statistics
import subprocess


CASES = ("gain64", "gain512", "wide512", "pendulum", "feedback", "nested", "trapezoid")
NESTED_CASES = ("nested1", "nested", "nested64", "nested_large", "nested_multi", "nested_levels",
                "nested_feedback", "nested_trapezoid")
METRICS = ("compile_us", "init_us", "avg_ns", "steps_per_sec", "p50_ns", "p99_ns", "rss_delta_bytes")


def run(binary, case, steps, library_dir):
    env = os.environ.copy()
    if library_dir:
        env["LD_LIBRARY_PATH"] = library_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    output = subprocess.check_output((binary, case, str(steps)), text=True, env=env)
    row = next(csv.DictReader(io.StringIO(output)))
    return {key: float(value) for key, value in row.items() if key != "case"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("optimized")
    parser.add_argument("--baseline-lib-dir", default="")
    parser.add_argument("--optimized-lib-dir", default="")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--steps", type=int, default=20000)
    parser.add_argument("--cases", default="default",
                        help="'default', 'nested', or a comma-separated list of case names")
    args = parser.parse_args()
    if args.repeats < 1 or args.steps < 1000:
        parser.error("repeats must be positive and steps must be at least 1000")

    cases = {"default": CASES, "nested": NESTED_CASES}.get(args.cases, tuple(args.cases.split(",")))

    print("case,version,compile_us,init_us,avg_ns,steps_per_sec,p50_ns,p99_ns,rss_delta_bytes,checksum")
    for case in cases:
        values = {"before": [], "after": []}
        for repetition in range(args.repeats):
            order = ("before", "after") if repetition % 2 == 0 else ("after", "before")
            for version in order:
                binary = args.baseline if version == "before" else args.optimized
                library_dir = args.baseline_lib_dir if version == "before" else args.optimized_lib_dir
                values[version].append(run(binary, case, args.steps, library_dir))
        checksums = [row["checksum"] for runs in values.values() for row in runs]
        if len(set(checksums)) != 1:
            raise RuntimeError(f"{case}: before/after checksums differ: {checksums}")
        for version in ("before", "after"):
            medians = {key: statistics.median(row[key] for row in values[version]) for key in METRICS}
            print(",".join((case, version, *(f"{medians[key]:.6g}" for key in METRICS),
                            f"{checksums[0]:.12g}")), flush=True)


if __name__ == "__main__":
    main()
