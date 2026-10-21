#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

# Placement survives Studio's editor round trip and Studio assigns the
# streams of a diagram as "Deploy to Nodes" does (offscreen).
set -euo pipefail
# In a cross build, S7_RUN runs the target's programs (Wine); run drops the
# \r of their \r\n output.
run() { if [ -n "${S7_RUN:-}" ]; then "$S7_RUN" "$@" | tr -d '\r'; else "$@"; fi; }
STUDIO="$1" TESTS="$2"
export QT_QPA_PLATFORM=offscreen
OUT="$(mktemp)"
trap 'rm -f "$OUT" "$OUT.2"' EXIT

run "$STUDIO" --deployment-diagram "$TESTS/deployment_diagram.json" "$OUT"
python3 - "$OUT" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
nodes = {e["id"]: e.get("node") for e in d["schema"]["elements"]}
assert nodes == {"src": "11111111-2222-3333-4444-555555555555", "gain": "33333333-4444-5555-6666-777777777777"}, nodes
assert [n["name"] for n in d["runtime_nodes"]] == ["a", "b"], d["runtime_nodes"]
assert len(d["streams"]) == 1, d["streams"]
s = d["streams"][0]
assert s["data_type"] == "I16" and s["producer"]["element"] == "src" and s["consumers"][0]["element"] == "gain", s
assert "nodes" in d["diagram"], "layout kept"
PY

# A second pass keeps the stream id.
run "$STUDIO" --deployment-diagram "$OUT" "$OUT.2"
python3 -c "import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); assert a['streams'][0]['stream_id']==b['streams'][0]['stream_id']" "$OUT" "$OUT.2"

# An element without a node is reported.
python3 - "$TESTS/deployment_diagram.json" "$OUT" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
del d["schema"]["elements"][1]["node"]
json.dump(d, open(sys.argv[2], "w"))
PY
if out=$(run "$STUDIO" --deployment-diagram "$OUT" "$OUT.2"); then echo "unplaced element accepted"; exit 1; fi
grep -q "^error: Element 'gain' is not placed" <<< "$out" || { echo "missing placement error: $out"; exit 1; }
echo "deployment diagram: OK"
