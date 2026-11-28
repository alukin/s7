#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Update saved diagrams to the current format, in place.

Before 0.2.0 the diagram format is not versioned and Studio keeps no
compatibility code: when the format changes, every saved diagram in the
repository is updated with this script instead. Migrations:

  * drop "diagram_version" (top level and nested schemas);
  * record "design_rate_hz" on rate-designed elements (metadata declares
    the parameter, e.g. FirFilter/IirFilter) that have none, from the dt of
    the schema they were written in (ADR-022), as Studio does on save.

Files keep their formatting where the change allows: removals are textual,
and rewritten files keep the JSON indentation they were written with.

Without --write nothing is changed: the files that need an update are
listed and the exit status is 1 if there are any (usable as a check).
"""

import argparse
import json
from fractions import Fraction
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # repository root (s7/)
SUFFIXES = (".s7diagram", ".s7schema")
VERSION_FIELD = re.compile(r'\s*"diagram_version"\s*:\s*-?\d+(\.\d+)?\s*,|,\s*"diagram_version"\s*:\s*-?\d+(\.\d+)?(?=\s*\})')


def tracked_diagrams():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], check=True, capture_output=True).stdout
    return [ROOT / name for name in out.decode().split("\0") if name.endswith(SUFFIXES)]


def design_rate_types():
    types = set()
    for metadata in ROOT.glob("s7l2/*/resources/element_metadata.json"):
        document = json.loads(metadata.read_text(encoding="utf-8"))
        for element in document.get("elements", []):
            names = {p.get("name") for p in element.get("parameters", [])}
            if "design_rate_hz" in names:
                types.add(element.get("type_name") or element.get("name"))
    return types


def without_versions(value):
    if isinstance(value, dict):
        return {k: without_versions(v) for k, v in value.items() if k != "diagram_version"}
    if isinstance(value, list):
        return [without_versions(v) for v in value]
    return value


def rate_hz(dt):
    """Hz of the simple rational period Rate::from_seconds(dt) recovers."""
    period = Fraction(1.0 / (1.0 / dt)).limit_denominator(10**12)
    hz = 1 / period
    return float(hz.numerator) if hz.denominator == 1 else float(hz)


def record_design_rates(schema, dt, types):
    """Mirror of s7::record_design_rates (s7core/src/schema_library.cpp)."""
    changed = False
    own = schema.get("dt")
    rate_dt = own if isinstance(own, (int, float)) and not isinstance(own, bool) and own > 0 else dt
    for element in schema.get("elements", []):
        nested = element.get("schema")
        if isinstance(nested, dict):
            changed = record_design_rates(nested, rate_dt, types) or changed
        if element.get("type") not in types or not rate_dt:
            continue
        params = element.setdefault("params", {})
        if params.get("design_rate_hz") is not None:
            continue
        params["design_rate_hz"] = rate_hz(rate_dt)
        changed = True
    return changed


def json_style(text, document):
    for ensure_ascii in (False, True):
        for indent in (2, 1, 4, None):
            out = json.dumps(document, indent=indent, ensure_ascii=ensure_ascii)
            if text in (out, out + "\n"):
                return indent, ensure_ascii, text.endswith("\n")
    return None


def updated(path, types):
    """Returns the updated text, or None when the file is current."""
    text = path.read_text(encoding="utf-8")
    original = json.loads(text)
    new_text = VERSION_FIELD.sub("", text)
    if json.loads(new_text) != without_versions(original):
        raise ValueError("removing diagram_version textually changed other content")
    document = json.loads(new_text)
    schema = document.get("schema", document)
    dt = document.get("dt")
    if isinstance(schema, dict) and record_design_rates(schema, dt if isinstance(dt, (int, float)) else None, types):
        style = json_style(new_text, json.loads(new_text))
        if style is None:
            raise ValueError("needs design_rate_hz but its formatting cannot be preserved; update it by hand")
        indent, ensure_ascii, newline = style
        new_text = json.dumps(document, indent=indent, ensure_ascii=ensure_ascii) + ("\n" if newline else "")
    return None if new_text == text else new_text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", type=Path, help="diagrams to update (default: every tracked .s7diagram/.s7schema)")
    parser.add_argument("--write", action="store_true", help="rewrite the files (default: only report them)")
    args = parser.parse_args(argv)
    paths = [p.resolve() for p in args.paths] if args.paths else tracked_diagrams()
    for path in paths:
        if not path.is_file() or path.suffix not in SUFFIXES:
            parser.error(f"not a diagram file: {path}")
    types = design_rate_types()
    pending, failed = [], []
    for path in paths:
        try:
            text = updated(path, types)
        except (ValueError, json.JSONDecodeError) as error:
            failed.append(path)
            print(f"error: {path.relative_to(ROOT)}: {error}", file=sys.stderr)
            continue
        if text is None:
            continue
        pending.append(path)
        if args.write:
            path.write_text(text, encoding="utf-8")
        print(("updated " if args.write else "needs update ") + str(path.relative_to(ROOT)))
    print(f"{len(paths)} diagram(s) checked, {len(pending)} {'updated' if args.write else 'need an update'}, {len(failed)} failed")
    if failed:
        return 2
    return 1 if pending and not args.write else 0


if __name__ == "__main__":
    sys.exit(main())
