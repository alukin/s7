#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a saved S7 diagram without changing its source. Optional Studio checks."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PNG_HEADER = b"\x89PNG\r\n\x1a\n"


def read_document(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("diagram root must be an object")
    return data


def is_index(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def inspect_document(doc):
    """Format-level checks; only Studio can validate element metadata and geometry."""
    errors, notes = [], []
    if doc.get("document_kind") != "s7.diagram":
        errors.append("expected document_kind=s7.diagram")
    if "diagram_version" in doc:
        errors.append("diagram_version is obsolete; run s7l2/tools/update_diagrams.py --write")
    schema = doc.get("schema")
    if not isinstance(schema, dict):
        return ["schema must be an object"], notes
    elements = schema.get("elements")
    connections = schema.get("connections")
    if not isinstance(elements, list) or not isinstance(connections, list):
        return ["schema.elements and schema.connections must be arrays"], notes
    element_ids = [e.get("id") for e in elements if isinstance(e, dict)]
    if len(element_ids) != len(elements) or any(not isinstance(i, str) or not i for i in element_ids):
        errors.append("every schema element needs a nonempty string id")
    if len(element_ids) != len(set(element_ids)):
        errors.append("duplicate schema element ids")
    ids = set(i for i in element_ids if isinstance(i, str))
    expected = Counter()
    for n, c in enumerate(connections):
        if not isinstance(c, dict):
            errors.append(f"connection {n} must be an object")
            continue
        a, b = c.get("from_element"), c.get("to_element")
        p, q = c.get("from_port"), c.get("to_port")
        if a not in ids or b not in ids or not is_index(p) or not is_index(q):
            errors.append(f"connection {n} has unknown element or invalid port index")
        else:
            expected[(a, p, b, q)] += 1
    if any(v > 1 for v in expected.values()):
        errors.append("duplicate functional connections")

    diagram = doc.get("diagram")
    if not isinstance(diagram, dict):
        notes.append("no diagram layout: Studio may generate layout from the schema")
        return errors, notes
    nodes = diagram.get("nodes", [])
    if not isinstance(nodes, list):
        return errors + ["diagram.nodes must be an array"], notes
    node_ids = {}
    for n, node in enumerate(nodes):
        if not isinstance(node, dict) or not isinstance(node.get("id"), str):
            errors.append(f"diagram node {n} has no valid id")
            continue
        ident = node["id"]
        if ident in node_ids:
            errors.append(f"duplicate diagram node id {ident}")
        node_ids[ident] = node
        pos = node.get("position")
        if not (isinstance(pos, list) and len(pos) == 2
                and all(isinstance(v, int) and not isinstance(v, bool) for v in pos)):
            errors.append(f"diagram node {ident} position must be two integer grid coordinates")
    missing = ids - set(node_ids)
    if missing:
        notes.append("layout omits schema nodes (Studio may place them): " + ", ".join(sorted(missing)))
    wires = diagram.get("wires", [])
    if not isinstance(wires, list):
        return errors + ["diagram.wires must be an array"], notes
    if not wires:
        notes.append("no explicit wire trees: Studio may reconstruct them")
        return errors, notes
    wire_by_id = {}
    for n, w in enumerate(wires):
        if not isinstance(w, dict) or not isinstance(w.get("id"), str):
            errors.append(f"wire {n} lacks a string id")
            continue
        if w["id"] in wire_by_id:
            errors.append(f"duplicate wire id {w['id']}")
        wire_by_id[w["id"]] = w

    resolving = set()
    resolved = {}

    def driver(wire_id):
        if wire_id in resolved:
            return resolved[wire_id]
        if wire_id in resolving:
            raise ValueError("cycle in wire junction graph at " + wire_id)
        if wire_id not in wire_by_id:
            raise ValueError("missing parent wire " + wire_id)
        resolving.add(wire_id)
        start = wire_by_id[wire_id].get("from", {})
        if not isinstance(start, dict):
            raise ValueError("invalid from-end in wire " + wire_id)
        if "port" in start:
            port = start["port"]
            if not (isinstance(port, list) and len(port) == 3 and
                    isinstance(port[0], str) and port[1] == "out" and is_index(port[2]) and
                    port[0] in node_ids):
                raise ValueError("invalid source port in wire " + wire_id)
            value = (port[0], port[2])
        elif "wire" in start:
            parent = start["wire"]
            if not isinstance(parent, str) or parent == wire_id:
                raise ValueError("invalid parent wire in " + wire_id)
            value = driver(parent)
            at = start.get("at")
            if not (isinstance(at, list) and len(at) == 2 and
                    all(isinstance(v, int) and not isinstance(v, bool) for v in at)):
                raise ValueError("missing/invalid junction coordinate in " + wire_id)
        else:
            raise ValueError("unrecognized source end in " + wire_id)
        resolving.remove(wire_id)
        resolved[wire_id] = value
        return value

    actual = Counter()
    sinks = set()
    for wire_id, w in wire_by_id.items():
        try:
            src, outport = driver(wire_id)
            end = w.get("to", {})
            port = end.get("port") if isinstance(end, dict) else None
            if not (isinstance(port, list) and len(port) == 3 and
                    isinstance(port[0], str) and port[1] == "in" and is_index(port[2])
                    and port[0] in node_ids):
                raise ValueError("invalid destination in wire " + wire_id)
            sink = (port[0], port[2])
            if sink in sinks:
                errors.append(f"multiple wires feed input {sink}")
            sinks.add(sink)
            if port[0] in ids:
                actual[(src, outport, port[0], port[2])] += 1
        except (ValueError, TypeError) as exc:
            errors.append(str(exc))
    if actual != expected:
        missing_edges = list((expected - actual).elements())
        extra_edges = list((actual - expected).elements())
        if missing_edges:
            errors.append("wires missing functional connections: " + repr(missing_edges[:12]))
        if extra_edges:
            errors.append("wires add functional connections: " + repr(extra_edges[:12]))
    notes.append("wire-tree checks are structural; Studio must validate geometry and port types")
    return errors, notes


def functional_content(doc):
    """All non-presentation fields are preserved for a layout-only change."""
    return {k: v for k, v in doc.items() if k not in ("diagram", "sheet")}


def baseline_from_git(ref, path):
    relative = path.resolve().relative_to(ROOT).as_posix()
    sha = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify",
                          ref + "^{commit}"], capture_output=True, text=True, check=True).stdout.strip()
    result = subprocess.run(["git", "-C", str(ROOT), "show", f"{sha}:{relative}"],
                            capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def run_studio(studio, diagram, artifacts, capture, timeout):
    normalized = artifacts / "normalized.s7diagram"
    args = [str(studio), "--normalize", str(diagram), str(normalized)]
    env = os.environ.copy()
    if not env.get("DISPLAY") and not env.get("WAYLAND_DISPLAY"):
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
    image = artifacts / "diagram.png"
    if capture:
        env["S7_NORMALIZE_PNG"] = str(image)
    result = subprocess.run(args, cwd=ROOT / "s7l2", env=env, capture_output=True,
                            text=True, timeout=timeout)
    outcome = {"command": args, "returncode": result.returncode,
               "stdout": result.stdout[-4000:], "stderr": result.stderr[-4000:]}
    if result.returncode:
        raise RuntimeError("Studio normalize failed: " + repr(outcome))
    if not normalized.is_file():
        raise RuntimeError("Studio returned success without producing normalized diagram")
    normalized_doc = read_document(normalized)
    errors, _ = inspect_document(normalized_doc)
    if errors:
        raise RuntimeError("Studio produced invalid normalized diagram: " + "; ".join(errors))
    outcome["normalized"] = str(normalized)
    if capture:
        if not image.is_file() or image.stat().st_size < 24:
            raise RuntimeError("Studio did not produce a PNG capture")
        with image.open("rb") as inp:
            header = inp.read(24)
        if header[:8] != PNG_HEADER or header[12:16] != b"IHDR":
            raise RuntimeError("Studio output is not a PNG image")
        width, height = struct.unpack(">II", header[16:24])
        if width == 0 or height == 0:
            raise RuntimeError("Studio output has zero dimensions")
        outcome["capture"] = {"path": str(image), "width": width, "height": height,
                              "status": "generated; pixel/legibility review still required"}
    return outcome


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--diagram", type=Path, required=True, help="saved .s7diagram file")
    source = p.add_mutually_exclusive_group()
    source.add_argument("--baseline", type=Path, help="compare layout-only edit with this file")
    source.add_argument("--base-ref", help="compare to the same file at a Git revision, e.g. main")
    p.add_argument("--layout-only", action="store_true", help="fail if non-presentation data changed")
    p.add_argument("--studio", type=Path, help="Studio executable; enables actual normalize check")
    p.add_argument("--capture", action="store_true", help="render 2D diagram PNG via Studio normalize")
    p.add_argument("--artifacts", type=Path, help="directory for normalized diagram, PNG and report")
    p.add_argument("--timeout", type=int, default=90)
    args = p.parse_args(argv)
    if args.layout_only and not (args.baseline or args.base_ref):
        p.error("--layout-only requires --baseline or --base-ref")
    if args.capture and (not args.studio or not args.artifacts):
        p.error("--capture requires --studio and --artifacts")
    if args.studio and not args.artifacts:
        p.error("--studio requires --artifacts")
    if args.timeout <= 0:
        p.error("--timeout must be positive")
    report = {"diagram": str(args.diagram), "errors": [], "notes": [],
              "checks": [], "status": "unverified"}
    try:
        doc = read_document(args.diagram)
        errors, notes = inspect_document(doc)
        report["errors"].extend(errors)
        report["notes"].extend(notes)
        report["checks"].append("document structure and functional wire-tree consistency")
        if args.baseline or args.base_ref:
            before = read_document(args.baseline) if args.baseline else baseline_from_git(args.base_ref, args.diagram)
            changed = sorted(k for k in (set(before) | set(doc))
                             if k not in ("diagram", "sheet") and before.get(k) != doc.get(k))
            report["semantic_changed_keys"] = changed
            if args.layout_only and changed:
                report["errors"].append("layout-only edit changed non-presentation fields: " + ", ".join(changed))
            report["checks"].append("non-presentation comparison with baseline")
        if args.studio and not report["errors"]:
            args.artifacts.mkdir(parents=True, exist_ok=True)
            report["studio"] = run_studio(args.studio.resolve(), args.diagram.resolve(),
                                          args.artifacts.resolve(), args.capture, args.timeout)
            report["checks"].append("Studio normalize and load")
        report["status"] = "failed" if report["errors"] else "passed_static"
        if args.studio and not report["errors"]:
            report["status"] = "passed_studio_normalize"
        if args.capture:
            report["notes"].append("PNG generated, NOT visually reviewed; not full GUI/viewer qualification")
    except (OSError, ValueError, subprocess.SubprocessError, RuntimeError) as exc:
        report["errors"].append(f"{type(exc).__name__}: {exc}")
        report["status"] = "failed"
    if args.artifacts:
        args.artifacts.mkdir(parents=True, exist_ok=True)
        (args.artifacts / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
