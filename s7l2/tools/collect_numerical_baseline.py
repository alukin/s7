#!/usr/bin/env python3
"""Collect direct NodeRuntime measurements for the two initial gate models."""

import argparse
import datetime as dt
import hashlib
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
    binary = args.binary.resolve()
    if not binary.is_file():
        raise FileNotFoundError(binary)
    cache = binary.parents[2]/"CMakeCache.txt"
    compiler = None
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_CXX_COMPILER:FILEPATH="):
                compiler = line.split("=", 1)[1]
                break
    compiler_version = (subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
                        if compiler else None)
    results = []
    for model, duration in (("oscillator", 200), ("linear", 12)):
        for h in (0.001, 0.0005):
            for method in (("euler", "trapezoid", "symplectic") if model == "oscillator"
                           else ("euler", "trapezoid")):
                repeats = [json.loads(subprocess.check_output(
                    [str(binary), model, str(h), str(duration), method], text=True))
                    for _ in range(5)]
                result = repeats[0]
                result["ns_per_step_repeats"] = [r["ns_per_step"] for r in repeats]
                result["ns_per_step_median"] = statistics.median(result["ns_per_step_repeats"])
                result["cpu_ns_per_step_repeats"] = [r["cpu_ns_per_step"] for r in repeats]
                result["cpu_ns_per_step_median"] = statistics.median(result["cpu_ns_per_step_repeats"])
                results.append(result)
    args.output.write_text(json.dumps(dict(
        baseline_commit="0b75330d7334f36ec79a9b67a547633466dbff0c",
        branch=subprocess.check_output(["git", "branch", "--show-current"], text=True).strip(),
        git_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        generated_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        binary=str(binary),
        platform=platform.platform(), machine=platform.processor() or platform.machine(),
        python=platform.python_version(),
        compiler=compiler_version,
        precision="IEEE-754 binary64",
        configuration=dict(models=["oscillator", "linear"], durations=[200, 12],
                           steps=[0.001, 0.0005], repetitions=5),
        reproduce="cmake --build build-release --target s7tests_numerical_research_baseline && python3 tools/collect_numerical_baseline.py --output build/linear-research/phase1_baseline.json",
        results=results), indent=2) + "\n")


if __name__ == "__main__":
    main()
