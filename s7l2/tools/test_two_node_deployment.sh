#!/usr/bin/env bash
# Deploys one diagram across two s7runtime nodes over mTLS with s7ctl, checks
# that node B computes from node A's stream, then redeploys a changed diagram
# and checks that the stream continues without a fault; finally restarts the
# producer node, which restores its deployment, and recovers the stream.
#
# Usage: test_two_node_deployment.sh <build dir> <source dir>
set -euo pipefail

BUILD="$1"
SRC="$2"
CERTS="$SRC/s7runtime/examples/certs"
RUNTIME="$BUILD/s7runtime/s7runtime"
CTL="$BUILD/s7ctl/s7ctl"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/s7-two-node.XXXXXX")"
PIDS=()
cleanup() {
  for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

# Ports below the Linux ephemeral range, so outgoing connections cannot hold them.
PORT_A=$((20000 + RANDOM % 5000))
PORT_B=$((PORT_A + 1))
NODE_A="11111111-2222-3333-4444-555555555555"
NODE_B="33333333-4444-5555-6666-777777777777"
OID="2.25.4266950079767543379770189370854073200"
STUDIO_FP="$(openssl x509 -in "$CERTS/s7_studio.cert.pem" -noout -fingerprint -sha256 | sed 's/.*=//; s/://g' | tr 'A-F' 'a-f')"
TLS=(--cert "$CERTS/s7_studio.cert.pem" --key "$CERTS/s7_studio.key.pem" --ca "$CERTS/s7_root_ca.cert.pem")

# node_config <cert name> <port>: a node's startup schema (an idle graph).
node_config() {
  cat <<EOF
{
  "name": "idle",
  "elements": [ { "id": "idle", "type": "Constant", "params": { "value": 0 } } ],
  "connections": [],
  "websocket": { "server": { "enabled": true, "port": $2, "tls": {
    "enabled": true, "cert_file": "$CERTS/$1.cert.pem", "key_file": "$CERTS/$1.key.pem",
    "ca_file": "$CERTS/s7_root_ca.cert.pem", "require_client_cert": true } } },
  "control": { "studio_key_purpose_oid": "$OID", "studio_certificate_sha256": "$STUDIO_FP" },
  "clock": { "assume_synchronized": true },
  "execution": { "step_rate_hz": 50 }
}
EOF
}
node_config s7_runtime_node "$PORT_A" > "$WORK/a.json"
node_config s7_runtime_node_b "$PORT_B" > "$WORK/b.json"

# source (A) -> double (A) -> scale (B) -> out (B)
diagram() {
  cat <<EOF
{
  "document_kind": "s7.diagram",
  "runtime_nodes": [
    { "node_uuid": "$NODE_A", "name": "a", "endpoint": "wss://127.0.0.1:$PORT_A" },
    { "node_uuid": "$NODE_B", "name": "b", "endpoint": "wss://127.0.0.1:$PORT_B" }
  ],
  "schema": {
    "name": "two_nodes",
    "elements": [
      { "id": "source", "type": "Constant", "params": { "type": "I32", "value": 21 }, "node": "$NODE_A" },
      { "id": "double", "type": "Gain", "params": { "factor": 2 }, "node": "$NODE_A" },
      { "id": "scale", "type": "Gain", "params": { "factor": $1 }, "node": "$NODE_B" },
      { "id": "out", "type": "Gain", "params": { "factor": 1 }, "node": "$NODE_B" }
    ],
    "connections": [
      { "from_element": "source", "from_port": 0, "to_element": "double", "to_port": 0 },
      { "from_element": "double", "from_port": 0, "to_element": "scale", "to_port": 0 },
      { "from_element": "scale", "from_port": 0, "to_element": "out", "to_port": 0 }
    ]
  },
  "dt": 0.02
}
EOF
}
diagram 10 > "$WORK/diagram.json"

"$RUNTIME" --schema "$WORK/a.json" --state-dir "$WORK/state-a" --serve-seconds 60 > "$WORK/a.log" 2>&1 &
PIDS+=($!)
"$RUNTIME" --schema "$WORK/b.json" --state-dir "$WORK/state-b" --serve-seconds 60 > "$WORK/b.log" 2>&1 &
PIDS+=($!)
for port in "$PORT_A" "$PORT_B"; do
  for _ in $(seq 50); do (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null && break; sleep 0.1; done
done

fail() {
  echo "FAIL: $*"
  for log in a a2 b; do [ -f "$WORK/$log.log" ] && { echo "--- node $log"; cat "$WORK/$log.log"; }; done
  exit 1
}

# 0) Endpoints swapped in the diagram: each node's certificate names the other
#    node, so nothing is prepared anywhere.
python3 - "$WORK/diagram.json" "$WORK/swapped.json" <<'EOF2'
import json, sys
d = json.load(open(sys.argv[1]))
a, b = d["runtime_nodes"]
a["endpoint"], b["endpoint"] = b["endpoint"], a["endpoint"]
json.dump(d, open(sys.argv[2], "w"))
EOF2
if "$CTL" deploy "$WORK/swapped.json" "${TLS[@]}" > "$WORK/swapped.log" 2>&1; then fail "deployed to nodes with the wrong identity"; fi
grep -q "is node $NODE_B, not node $NODE_A" "$WORK/swapped.log" || fail "identity mismatch not reported: $(cat "$WORK/swapped.log")"
"$CTL" config "wss://127.0.0.1:$PORT_A" "${TLS[@]}" > "$WORK/config-none.json" || true
grep -q REVISION_NOT_FOUND "$WORK/config-none.json" || fail "a node was deployed despite the mismatch: $(cat "$WORK/config-none.json")"

# 1) Deploy; keep the diagram with its streams for the next deployment. With
#    dt both nodes step on one 20 ms UTC grid from the same activation time.
"$CTL" deploy "$WORK/diagram.json" "${TLS[@]}" --write "$WORK/deployed.json" > "$WORK/deploy1.log" || fail "first deployment: $(cat "$WORK/deploy1.log")"
ACTIVATE_AT="$(sed -n 's/^activate_at_ns //p' "$WORK/deploy1.log")"
[ -n "$ACTIVATE_AT" ] || fail "no activation time: $(cat "$WORK/deploy1.log")"
sleep 1
"$CTL" info "wss://127.0.0.1:$PORT_A" "${TLS[@]}" > "$WORK/info-a.json"
"$CTL" info "wss://127.0.0.1:$PORT_B" "${TLS[@]}" > "$WORK/info-b.json"
python3 - "$WORK/info-a.json" "$WORK/info-b.json" "$ACTIVATE_AT" <<'EOF2' || fail "the nodes do not share the grid: $(cat "$WORK/info-a.json" "$WORK/info-b.json")"
import json, sys
a, b = (json.load(open(path))["state"]["timing"] for path in sys.argv[1:3])
at = int(sys.argv[3])
assert a["pacing"] == b["pacing"] == "grid" and a["period_ns"] == b["period_ns"] == 20000000, (a, b)
assert a["origin_utc_ns"] == b["origin_utc_ns"] == at, (a, b, at)
assert abs(a["steps"] - b["steps"]) <= 3, (a["steps"], b["steps"])  # read a moment apart
assert a["steps"] >= 20, a["steps"]  # about 1 s after activation, at 50 Hz
EOF2


# json_check <file> <python expression over `r`>
json_check() { python3 -c "import json,sys; r=json.load(open(sys.argv[1])); sys.exit(0 if ($2) else 1)" "$1"; }

"$CTL" streams "wss://127.0.0.1:$PORT_B" "${TLS[@]}" > "$WORK/streams-b.json"
json_check "$WORK/streams-b.json" \
  "len(r['streams'])==1 and r['streams'][0]['role']=='consumer' and r['streams'][0]['data_type']=='I32' and r['streams'][0]['fault']=='NONE' and r['streams'][0]['elements']>10" \
  || fail "node B consumes the I32 stream: $(cat "$WORK/streams-b.json")"
STREAM_ID="$(python3 -c "import json; print(json.load(open('$WORK/streams-b.json'))['streams'][0]['stream_id'])")"
json_check "$WORK/streams-b.json" \
  "r['streams'][0]['latency']['samples']>0 and 0<=r['streams'][0]['latency']['p50_ns']<1000000000" \
  || fail "node B measures the stream's latency: $(cat "$WORK/streams-b.json")"

"$CTL" config "wss://127.0.0.1:$PORT_B" "${TLS[@]}" > "$WORK/config-b.json"
json_check "$WORK/config-b.json" \
  "r['type']=='NETWORK_CONFIGURATION' and r['revision']==1 and r['diagram']['streams'][0]['stream_id']=='$STREAM_ID' and any(e['type']=='NetSource' for e in r['node_schema']['elements']) and not any(e['id']=='double' for e in r['node_schema']['elements'])" \
  || fail "node B returns the deployed diagram and its part: $(cat "$WORK/config-b.json")"

# 2) Redeploy a change on both nodes: the same stream continues.
python3 - "$WORK/deployed.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
for e in d["schema"]["elements"]:
    if e["id"] == "scale":
        e["params"]["factor"] = 100
json.dump(d, open(sys.argv[1], "w"))
EOF
BEFORE="$(python3 -c "import json; print(json.load(open('$WORK/streams-b.json'))['streams'][0]['elements'])")"
"$CTL" deploy "$WORK/deployed.json" "${TLS[@]}" > "$WORK/deploy2.log" || fail "redeployment: $(cat "$WORK/deploy2.log")"
ACTIVATE_AT2="$(sed -n 's/^activate_at_ns //p' "$WORK/deploy2.log")"
sleep 1
"$CTL" streams "wss://127.0.0.1:$PORT_B" "${TLS[@]}" > "$WORK/streams-b2.json"
json_check "$WORK/streams-b2.json" \
  "len(r['streams'])==1 and r['streams'][0]['stream_id']=='$STREAM_ID' and r['streams'][0]['state']=='active' and r['streams'][0]['fault']=='NONE' and r['streams'][0]['elements']>$BEFORE" \
  || fail "the stream continues across the redeployment: $(cat "$WORK/streams-b2.json")"
"$CTL" streams "wss://127.0.0.1:$PORT_A" "${TLS[@]}" > "$WORK/streams-a.json"
json_check "$WORK/streams-a.json" \
  "r['streams'][0]['role']=='producer' and r['streams'][0]['fault']=='NONE' and r['streams'][0]['lost_resumes']==0" \
  || fail "the producer has no fault: $(cat "$WORK/streams-a.json")"
"$CTL" config "wss://127.0.0.1:$PORT_A" "${TLS[@]}" > "$WORK/config-a.json"
json_check "$WORK/config-a.json" "r['revision']==2" || fail "node A runs revision 2"

grep -q "runtime.execution.stopped=error" "$WORK/a.log" "$WORK/b.log" && fail "a node stopped stepping"

# 3) Restart node A: it restores revision 2 from its state directory. Its
#    stream continues beyond its sequence reservation, so node B sees the
#    gap and faults (a process restart loses data in flight); recovery
#    resumes it.
kill "${PIDS[0]}"
wait "${PIDS[0]}" 2>/dev/null || true
"$RUNTIME" --schema "$WORK/a.json" --state-dir "$WORK/state-a" --serve-seconds 60 > "$WORK/a2.log" 2>&1 &
PIDS+=($!)
for _ in $(seq 50); do (exec 3<>"/dev/tcp/127.0.0.1/$PORT_A") 2>/dev/null && break; sleep 0.1; done
grep -q "deployment.restored deployment_id=.* revision=2" "$WORK/a2.log" || fail "node A did not restore its deployment: $(cat "$WORK/a2.log")"
"$CTL" config "wss://127.0.0.1:$PORT_A" "${TLS[@]}" > "$WORK/config-a2.json"
json_check "$WORK/config-a2.json" "r['revision']==2" || fail "the restarted node A runs revision 2: $(cat "$WORK/config-a2.json")"
"$CTL" info "wss://127.0.0.1:$PORT_A" "${TLS[@]}" > "$WORK/info-a2.json"
json_check "$WORK/info-a2.json" \
  "(lambda t: t['pacing']=='grid' and t['origin_utc_ns']>$ACTIVATE_AT2 and (t['origin_utc_ns']-$ACTIVATE_AT2)%20000000==0)(r['state']['timing'])" \
  || fail "the restarted node A is back on the deployment's grid: $(cat "$WORK/info-a2.json")"
for _ in $(seq 50); do
  "$CTL" streams "wss://127.0.0.1:$PORT_B" "${TLS[@]}" > "$WORK/streams-b3.json"
  json_check "$WORK/streams-b3.json" "r['streams'][0]['fault']=='SEQUENCE_GAP'" && break
  sleep 0.1
done
json_check "$WORK/streams-b3.json" "r['streams'][0]['fault']=='SEQUENCE_GAP'" \
  || fail "node B reports the producer restart as a gap: $(cat "$WORK/streams-b3.json")"
"$CTL" recover "wss://127.0.0.1:$PORT_B" "$STREAM_ID" "${TLS[@]}" > /dev/null || fail "recovery"
GAP_ELEMENTS="$(python3 -c "import json; print(json.load(open('$WORK/streams-b3.json'))['streams'][0]['elements'])")"
sleep 1
"$CTL" streams "wss://127.0.0.1:$PORT_B" "${TLS[@]}" > "$WORK/streams-b4.json"
json_check "$WORK/streams-b4.json" \
  "r['streams'][0]['fault']=='NONE' and r['streams'][0]['elements']>$GAP_ELEMENTS and r['streams'][0]['recent_faults'][-1]['code']=='SEQUENCE_GAP'" \
  || fail "the stream resumes after recovery and the gap stays reported: $(cat "$WORK/streams-b4.json")"

echo "two-node deployment: OK (stream $STREAM_ID)"
