#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${S7_BUILD_DIR:-${ROOT_DIR}/tmp/no-rsocket-build}"
S7MAIN_BIN="${BUILD_DIR}/s7main/s7main"
S7CTL_BIN="${BUILD_DIR}/s7ctl/s7ctl"
PORT="${S7_PORT:-5578}"

if [[ ! -x "$S7MAIN_BIN" ]]; then
  echo "missing s7main binary: $S7MAIN_BIN" >&2
  exit 1
fi
if [[ ! -x "$S7CTL_BIN" ]]; then
  echo "missing s7ctl binary: $S7CTL_BIN" >&2
  exit 1
fi

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]]; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  if [[ -n "${EVENTS_PID:-}" ]]; then
    kill "$EVENTS_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

coproc SERVER { "$S7MAIN_BIN" --server --port "$PORT"; }
sleep 1

HELLO_OUT="$($S7CTL_BIN --host 127.0.0.1 --port "$PORT" hello)"
STATUS_OUT="$($S7CTL_BIN --host 127.0.0.1 --port "$PORT" status)"

EVENTS_LOG="$(mktemp)"
"$S7CTL_BIN" --host 127.0.0.1 --port "$PORT" events --topics schema --count 1 >"$EVENTS_LOG" &
EVENTS_PID=$!
sleep 0.2

LOAD_OUT="$($S7CTL_BIN --host 127.0.0.1 --port "$PORT" load-file --schema-path "$ROOT_DIR/s7main/examples/io_file_real_sink.json")"
wait "$EVENTS_PID"
unset EVENTS_PID

SCHEMA_HANDLE="$(echo "$LOAD_OUT" | sed -n 's/.*"schema_handle"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)"
if [[ -z "$SCHEMA_HANDLE" ]]; then
  echo "failed to extract schema_handle from load response" >&2
  exit 1
fi

VALIDATE_OUT="$($S7CTL_BIN --host 127.0.0.1 --port "$PORT" validate --schema-handle "$SCHEMA_HANDLE")"
RUN_OUT="$($S7CTL_BIN --host 127.0.0.1 --port "$PORT" run --schema-handle "$SCHEMA_HANDLE" --ticks 4)"
UNLOAD_OUT="$($S7CTL_BIN --host 127.0.0.1 --port "$PORT" unload --schema-handle "$SCHEMA_HANDLE")"

echo "$HELLO_OUT" | grep -q '"protocol_version"'
echo "$STATUS_OUT" | grep -q '"health"'
grep -q '"method": "runtime.event"' "$EVENTS_LOG"
echo "$VALIDATE_OUT" | grep -q '"valid"'
echo "$RUN_OUT" | grep -q '"run_id"'
echo "$UNLOAD_OUT" | grep -q '"unloaded"'

echo "s7ctl smoke test passed"

# Stop server deterministically.
kill "$SERVER_PID" >/dev/null 2>&1 || true
wait "$SERVER_PID" 2>/dev/null || true



