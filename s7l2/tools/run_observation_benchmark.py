#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure how probes and viewers slow local Studio runs (performance plan WP0).

Runs s7studio_observation_load_benchmark over a matrix of graphs, probe counts
and observation modes, in fresh processes, and reports steps/s plus the
observation overhead relative to the same graph without probes.

Profiles:
  fast  unrestricted (the development host)
  slow  two CPUs with a combined 60% CPU quota through a transient systemd user
        scope, emulating a user machine several times slower

The machine is shared: before each run the script waits until the 1-minute
load average falls below --max-load, and records the load it ran at.
Build the binary first:
  cmake --build <build> --target s7studio_observation_load_benchmark
"""
import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

MODES = {
    # name: (record, live, viewers)
    "record": (1, 0, 0),
    "record+live": (1, 1, 0),
    "record+live+viewers": (1, 1, 1),
}


def profile_prefix(profile, slow_cpus):
    if profile == "fast":
        return []
    if not shutil.which("systemd-run") or not shutil.which("taskset"):
        sys.exit("error: the slow profile needs systemd-run (user scope) and taskset")
    return ["systemd-run", "--user", "--scope", "--quiet", "-p", "CPUQuota=60%",
            "taskset", "-c", slow_cpus]


def wait_for_quiet(max_load, timeout_s):
    deadline = time.monotonic() + timeout_s
    while True:
        load = os.getloadavg()[0]
        if load <= max_load:
            return load
        if time.monotonic() > deadline:
            sys.exit(f"error: load average {load:.2f} stayed above --max-load {max_load} "
                     f"for {timeout_s} s; another job may need a quiet machine")
        print(f"waiting: load average {load:.2f} > {max_load}", file=sys.stderr, flush=True)
        time.sleep(15)


def cases(args):
    for graph in args.graphs:
        for profile in args.profiles:
            yield graph, profile, 0, "record"  # baseline: no probes
            for probes in args.probes:
                for mode in args.modes:
                    yield graph, profile, probes, mode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path, required=True, help="s7studio_observation_load_benchmark")
    parser.add_argument("--output", type=Path, required=True, help="JSON report (must not exist)")
    parser.add_argument("--profiles", nargs="+", choices=("fast", "slow"), default=["fast", "slow"])
    parser.add_argument("--graphs", nargs="+", choices=("chain", "oscillator"), default=["chain", "oscillator"])
    parser.add_argument("--probes", nargs="+", type=int, default=[4, 8])
    parser.add_argument("--modes", nargs="+", choices=tuple(MODES), default=list(MODES))
    parser.add_argument("--gains", type=int, default=64)
    parser.add_argument("--ticks", type=int, default=1_000_000)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--slow-cpus", default="2,3", help="CPU list for the slow profile")
    parser.add_argument("--max-load", type=float, default=2.0, help="wait while the 1-min load is above this")
    parser.add_argument("--wait-timeout", type=int, default=1800, help="seconds to wait for a quiet machine")
    parser.add_argument("--quick", action="store_true", help="one repeat, 100k ticks (smoke check of the harness)")
    parser.add_argument("--dry-run", action="store_true", help="print the commands without running them")
    args = parser.parse_args()
    if args.quick:
        args.repeats, args.ticks = 1, 100_000
    if args.output.exists():
        parser.error(f"{args.output} exists; choose a new report path")
    if not args.dry_run and not os.access(args.binary, os.X_OK):
        parser.error(f"{args.binary} is not an executable benchmark binary")
    if args.repeats < 1 or args.ticks < 1 or any(p < 1 or p > 8 for p in args.probes):
        parser.error("repeats and ticks must be positive; probes 1..8")

    runs = []
    for graph, profile, probes, mode in cases(args):
        record, live, viewers = MODES[mode]
        command = profile_prefix(profile, args.slow_cpus) + [
            str(args.binary), "--graph", graph, "--gains", str(args.gains), "--ticks", str(args.ticks),
            "--probes", str(probes), "--record", str(record), "--live", str(live if probes else 0),
            "--viewers", str(viewers if probes else 0)]
        for repeat in range(args.repeats):
            if args.dry_run:
                print(" ".join(command))
                continue
            load = wait_for_quiet(args.max_load, args.wait_timeout)
            completed = subprocess.run(command, capture_output=True, text=True, timeout=3600,
                                       env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
            lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
            if completed.returncode != 0 or not lines:
                sys.exit(f"error: run failed ({completed.returncode}): {' '.join(command)}\n{completed.stderr}")
            result = json.loads(lines[-1])
            result.update(profile=profile, mode=mode if probes else "baseline", repeat=repeat, load_before=load)
            runs.append(result)
            print(f"{profile} {graph} probes={probes} {result['mode']} #{repeat}: "
                  f"{result['steps_per_s']:,.0f} steps/s", flush=True)
    if args.dry_run:
        return

    summary = []
    for graph in args.graphs:
        for profile in args.profiles:
            def median(probes, mode):
                values = [r["steps_per_s"] for r in runs if r["graph"] == graph and r["profile"] == profile
                          and r["probes"] == probes and r["mode"] == mode]
                return statistics.median(values) if values else None
            baseline = median(0, "baseline")
            for probes in args.probes:
                for mode in args.modes:
                    value = median(probes, mode)
                    worst = max((r["calls"][name]["max_ms"] for r in runs
                                 if r["graph"] == graph and r["profile"] == profile and r["probes"] == probes
                                 and r["mode"] == mode for name in r["calls"]), default=0.0)
                    summary.append({"graph": graph, "profile": profile, "probes": probes, "mode": mode,
                                    "median_steps_per_s": value, "baseline_steps_per_s": baseline,
                                    "observation_share": 1 - value / baseline if value and baseline else None,
                                    "max_viewer_call_ms": worst})
    report = {"schema": "s7_observation_load_report_v1",
              "machine": platform.machine(), "cpu_count": os.cpu_count(),
              "args": {k: (str(v) if isinstance(v, Path) else v) for k, v in vars(args).items()},
              "summary": summary, "runs": runs}
    with args.output.open("x") as out:
        json.dump(report, out, indent=2)
        out.write("\n")
    print(f"{'graph':10} {'profile':7} {'probes':6} {'mode':22} {'steps/s':>12} {'obs. share':>10} {'max call':>9}")
    for row in summary:
        share = row["observation_share"]
        print(f"{row['graph']:10} {row['profile']:7} {row['probes']:<6} {row['mode']:22} "
              f"{row['median_steps_per_s'] or 0:12,.0f} {(share or 0) * 100:9.1f}% {row['max_viewer_call_ms']:8.1f}ms")


if __name__ == "__main__":
    main()
