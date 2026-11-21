#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record comparable S7 Codex/agent task outcomes without fabricating usage data."""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TASKS = Path(__file__).with_name("benchmark_tasks.json")


def load_tasks(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("version") != 1 or not isinstance(value.get("tasks"), list):
        raise ValueError("unsupported benchmark task manifest")
    return {task["id"]: task for task in value["tasks"]}


def load_results(path):
    if not path.exists():
        return []
    results = []
    for index, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"invalid result JSONL at line {index}: {exc}") from exc
        if not isinstance(row, dict):
            raise ValueError(f"result line {index} is not an object")
        results.append(row)
    return results


def evaluate(rows):
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row.get("task"), row.get("variant"))].append(row)
    output = []
    for (task, variant), group in sorted(grouped.items()):
        completed = [r for r in group if r.get("status") == "pass"]
        durations = [r["duration_s"] for r in completed if r.get("duration_s") is not None]
        tokens = [r["input_tokens"] + r["output_tokens"] for r in completed
                  if r.get("input_tokens") is not None and r.get("output_tokens") is not None]
        attempts = [r["attempts"] for r in completed if r.get("attempts") is not None]
        output.append({"task": task, "variant": variant, "runs": len(group),
                       "passed": len(completed), "pass_rate": len(completed)/len(group),
                       "median_success_seconds": statistics.median(durations) if durations else None,
                       "median_success_tokens": statistics.median(tokens) if tokens else None,
                       "successful_runs_with_known_tokens": len(tokens),
                       "median_success_attempts": statistics.median(attempts) if attempts else None})
    return output


def nonnegative(value):
    if value is not None and value < 0:
        raise ValueError("durations and token counts cannot be negative")
    return value


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tasks", type=Path, default=DEFAULT_TASKS)
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("list", help="show task definitions and acceptance criteria")
    record = sub.add_parser("record", help="append one observed run to JSONL (outside Git)")
    record.add_argument("--results", type=Path, required=True)
    record.add_argument("--task", required=True)
    record.add_argument("--variant", required=True, help="e.g. vanilla, skills, skills+scripts")
    record.add_argument("--model", required=True)
    record.add_argument("--status", required=True, choices=("pass", "fail", "blocked"))
    record.add_argument("--duration-s", type=float)
    record.add_argument("--input-tokens", type=int)
    record.add_argument("--output-tokens", type=int)
    record.add_argument("--attempts", type=int, default=1)
    record.add_argument("--evidence", default="", help="test log/artifact path or review reference")
    record.add_argument("--notes", default="")
    record.add_argument("--baseline-sha", help="pin a controlled test baseline")
    summary = sub.add_parser("report", help="summarize real recorded outcomes by task and variant")
    summary.add_argument("--results", type=Path, required=True)
    args = p.parse_args(argv)
    try:
        tasks = load_tasks(args.tasks)
        if args.action == "list":
            print(json.dumps(list(tasks.values()), indent=2))
            return 0
        if args.action == "record":
            if args.task not in tasks:
                raise ValueError("unknown benchmark task: " + args.task)
            for val in (args.duration_s, args.input_tokens, args.output_tokens):
                nonnegative(val)
            if args.attempts < 1:
                raise ValueError("attempts must be >= 1")
            if not args.variant.strip() or not args.model.strip():
                raise ValueError("model and variant must not be empty")
            if args.status == "pass" and not args.evidence.strip():
                raise ValueError("pass requires a verifiable evidence reference")
            sha = args.baseline_sha or subprocess.run(
                ["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True,
                check=True, text=True).stdout.strip()
            record = {"task": args.task, "variant": args.variant, "model": args.model,
                      "status": args.status, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                      "baseline_sha": sha, "duration_s": args.duration_s,
                      "input_tokens": args.input_tokens, "output_tokens": args.output_tokens,
                      "attempts": args.attempts, "evidence": args.evidence, "notes": args.notes,
                      "usage_note": "token counts are user-supplied; not independently verified"}
            args.results.parent.mkdir(parents=True, exist_ok=True)
            with args.results.open("a", encoding="utf-8") as output:
                output.write(json.dumps(record) + "\n")
            print(json.dumps(record, indent=2))
            return 0
        rows = load_results(args.results)
        print(json.dumps({"results": len(rows), "groups": evaluate(rows),
                          "warning": "Only compare controlled runs on matching tasks/baselines. "
                          "Unknown tokens are never estimated; pass status is externally assessed."}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print(f"benchmark error: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
