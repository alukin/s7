#!/usr/bin/env bash
# s7ctl infers stream types of plugin elements once it loads the plugin.
# Usage: plugin_types_test.sh <s7ctl> <test plugin .so>
set -euo pipefail
CTL="$1"
PLUGIN="$2"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/s7ctl-plugin.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" # no ./plugins here

A="aaaaaaaa-0000-4000-8000-000000000001"
B="bbbbbbbb-0000-4000-8000-000000000002"
cat > diagram.json <<JSON
{
  "document_kind": "s7.diagram",
  "runtime_nodes": [ { "node_uuid": "$A", "endpoint": "ws://127.0.0.1:1" }, { "node_uuid": "$B", "endpoint": "ws://127.0.0.1:2" } ],
  "schema": {
    "name": "plugin_stream",
    "elements": [
      { "id": "p", "type": "TestPluginConstant", "node": "$A" },
      { "id": "g", "type": "Gain", "params": { "factor": 2 }, "node": "$B" }
    ],
    "connections": [ { "from_element": "p", "from_port": 0, "to_element": "g", "to_port": 0 } ]
  }
}
JSON

if S7_PLUGIN_PATH= HOME="$WORK" "$CTL" check diagram.json > without.log 2>&1; then
  echo "FAIL: a diagram with an unknown element type passed"; cat without.log; exit 1
fi
grep -q "TestPluginConstant" without.log || { echo "FAIL: the unknown type is not named"; cat without.log; exit 1; }

S7_PLUGIN_PATH= HOME="$WORK" "$CTL" check diagram.json --plugin "$PLUGIN" --write deployed.json > with.log 2> plugin.log \
  || { echo "FAIL: check with the plugin"; cat with.log plugin.log; exit 1; }
grep -q "^stream .* F64 p:0 ($A) -> g:0 ($B)$" with.log || { echo "FAIL: stream not inferred"; cat with.log; exit 1; }
grep -q "^node $A 2 elements$" with.log || { echo "FAIL: node A part"; cat with.log; exit 1; }
python3 -c "import json,sys; d=json.load(open('deployed.json')); assert d['streams'][0]['data_type']=='F64'" \
  || { echo "FAIL: --write"; exit 1; }
if grep -q "Plugin" with.log; then echo "FAIL: plugin progress on stdout"; cat with.log; exit 1; fi

# The same through a plugin directory.
mkdir plugins-dir && cp "$PLUGIN" plugins-dir/
S7_PLUGIN_PATH= HOME="$WORK" "$CTL" check diagram.json --plugin-dir plugins-dir > dir.log 2>/dev/null \
  || { echo "FAIL: check with --plugin-dir"; cat dir.log; exit 1; }
grep -q "^stream .* F64" dir.log || { echo "FAIL: stream not inferred through --plugin-dir"; cat dir.log; exit 1; }
echo "s7ctl plugin types: OK"
