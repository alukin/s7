#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create and validate compact S7 agent-to-agent engineering handoffs."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
STATES = {"planned", "in_progress", "blocked", "accepted", "rejected"}


def validate(doc):
    errors = []
    if doc.get("version") != 1:
        errors.append("version must equal 1")
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{2,80}", str(doc.get("id", ""))):
        errors.append("id must be a descriptive lowercase slug")
    if not isinstance(doc.get("goal"), str) or not doc["goal"].strip():
        errors.append("goal must be a nonempty string")
    if not isinstance(doc.get("baseline_sha"), str) or not re.fullmatch(r"[a-f0-9]{40}", doc["baseline_sha"]):
        errors.append("baseline_sha must be a 40-character Git SHA")
    for key in ("scope", "invariants", "acceptance_checks"):
        value = doc.get(key)
        if not isinstance(value, list) or not value or not all(
                isinstance(s, str) and s.strip() for s in value):
            errors.append(key + " must contain nonempty strings")
    if doc.get("status") not in STATES:
        errors.append("status must be one of " + ", ".join(sorted(STATES)))
    evidence = doc.get("evidence")
    if not isinstance(evidence, list):
        errors.append("evidence must be a list")
    elif doc.get("status") == "accepted":
        if not evidence or not all(
                isinstance(e, dict) and isinstance(e.get("command"), str)
                and e.get("result") in ("passed", "failed", "skipped")
                for e in evidence):
            errors.append("accepted task requires recorded commands/results, not a bare claim")
        elif any(e["result"] != "passed" for e in evidence):
            errors.append("accepted task contains failed/skipped evidence; explain and review explicitly")
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    init = sub.add_parser("init", help="write a new handoff without overwriting existing files")
    init.add_argument("--id", required=True)
    init.add_argument("--goal", required=True)
    init.add_argument("--path", action="append", required=True, help="affected path; repeatable")
    init.add_argument("--invariant", action="append", required=True, help="preserved contract; repeatable")
    init.add_argument("--check", action="append", required=True, help="acceptance check; repeatable")
    init.add_argument("--output", type=Path, required=True)
    check = sub.add_parser("check", help="validate a handoff; acceptance status requires evidence")
    check.add_argument("file", type=Path)
    show = sub.add_parser("show", help="print compact summary for another agent")
    show.add_argument("file", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.action == "init":
            sha = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"],
                                 capture_output=True, check=True, text=True).stdout.strip()
            doc = {"version": 1, "id": args.id, "goal": args.goal,
                   "created_utc": datetime.now(timezone.utc).isoformat(),
                   "baseline_sha": sha, "scope": args.path,
                   "invariants": args.invariant, "acceptance_checks": args.check,
                   "status": "planned", "evidence": [],
                   "notes": ["Agent must inspect current code and tests before editing."]}
            issues = validate(doc)
            if issues:
                raise ValueError("; ".join(issues))
            # Exclusive mode avoids losing an existing task/handoff.
            with args.output.open("x", encoding="utf-8") as output:
                json.dump(doc, output, indent=2)
                output.write("\n")
            print(str(args.output))
            return 0
        doc = json.loads(args.file.read_text(encoding="utf-8"))
        issues = validate(doc)
        if args.action == "show":
            print(json.dumps({k: doc.get(k) for k in
                              ("id", "goal", "baseline_sha", "scope", "invariants",
                               "acceptance_checks", "status", "evidence", "notes")}, indent=2))
        else:
            print(json.dumps({"file": str(args.file), "valid": not issues, "issues": issues}, indent=2))
        return 1 if issues else 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print(f"handoff error: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
