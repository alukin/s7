#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Check saved example geometry and index coverage without starting Studio."""

import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"
GRID = 20
PIXELS_PER_MM = 96 / 25.4


def intersects(a, b):
    return min(a[2], b[2]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[1], b[1])


def check(path, index):
    doc = json.loads(path.read_text())
    diagram = doc["diagram"]
    issues = []
    relative = Path(os.path.relpath(path, EXAMPLES)).as_posix()
    if f"({relative})" not in index:
        issues.append("missing index link")
    sheet = doc.get("sheet")
    if not sheet:
        return [*issues, "missing explicit sheet size"]
    bounds = (sheet["width_mm"] * PIXELS_PER_MM / GRID,
              sheet["height_mm"] * PIXELS_PER_MM / GRID)
    ports = {}
    for wire in diagram["wires"]:
        for end in ("from", "to"):
            port = wire[end].get("port")
            if port:
                ports[port[0]] = max(ports.get(port[0], 0), port[2] + 1)
    rects = []
    ids = set()
    for node in diagram["nodes"]:
        name = node["id"]
        if name in ids:
            issues.append(f"duplicate node {name}")
        ids.add(name)
        x, y = node["position"]
        # Studio's default body is 9 grid steps wide and at least 5 high;
        # each additional port row needs two steps. Saved custom sizes win.
        width, height = node.get("size", (9, max(5, 2 * ports.get(name, 0) + 1)))
        rect = (x, y, x + width, y + height)
        if x < 0 or y < 0 or rect[2] > bounds[0] or rect[3] > bounds[1]:
            issues.append(f"{name} outside sheet")
        for other, other_rect in rects:
            if intersects(rect, other_rect):
                issues.append(f"{name} overlaps {other}")
        rects.append((name, rect))
    for frame in diagram.get("text_frames", []):
        x, y = frame["position"]
        params = frame.get("params", {})
        rect = (x, y, x + params.get("width", 260) / GRID,
                y + params.get("height", 120) / GRID)
        if x < 0 or y < 0 or rect[2] > bounds[0] or rect[3] > bounds[1]:
            issues.append(f"notes {frame['id']} outside sheet")
        for name, other in rects:
            if intersects(rect, other):
                issues.append(f"notes {frame['id']} overlap {name}")
    for wire in diagram["wires"]:
        for end in ("from", "to"):
            port = wire[end].get("port")
            if port and port[0] not in ids:
                issues.append(f"wire {wire['id']} references missing node {port[0]}")
    return issues


def main():
    index = (EXAMPLES / "README.md").read_text()
    paths = sorted([*EXAMPLES.rglob("*.s7diagram"), *(ROOT / "education/labs").glob("*/*.s7diagram")])
    failures = [(path, problem) for path in paths for problem in check(path, index)]
    for path, problem in failures:
        print(f"{path.relative_to(ROOT)}: {problem}")
    print(f"Checked {len(paths)} example diagrams: {len(failures)} issue(s)")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
