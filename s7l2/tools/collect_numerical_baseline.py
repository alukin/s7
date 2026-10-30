#!/usr/bin/env python3
"""Collect direct NodeRuntime measurements for the two initial gate models."""

import argparse
import json
import platform
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build-release/s7tests/s7tests_numerical_research_baseline"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for model, duration in (("oscillator", 200), ("linear", 12)):
        for h in (0.001, 0.0005):
            for method in (("euler", "trapezoid", "symplectic") if model == "oscillator"
                           else ("euler", "trapezoid")):
                repeats = [json.loads(subprocess.check_output(
                    [str(args.binary), model, str(h), str(duration), method], text=True))
                    for _ in range(5)]
                result = repeats[0]
                result["ns_per_step_repeats"] = [r["ns_per_step"] for r in repeats]
                result["ns_per_step_median"] = statistics.median(result["ns_per_step_repeats"])
                result["cpu_ns_per_step_repeats"] = [r["cpu_ns_per_step"] for r in repeats]
                result["cpu_ns_per_step_median"] = statistics.median(result["cpu_ns_per_step_repeats"])
                results.append(result)
    args.output.write_text(json.dumps(dict(
        baseline_commit="0b75330d7334f36ec79a9b67a547633466dbff0c",
        branch_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        machine=platform.processor() or platform.machine(), results=results), indent=2) + "\n")


if __name__ == "__main__":
    main()
