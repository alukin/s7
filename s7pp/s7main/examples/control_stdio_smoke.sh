#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
S7MAIN_BIN="${1:-$ROOT_DIR/build_verify/s7main/s7main}"

if [[ ! -x "$S7MAIN_BIN" ]]; then
  echo "s7main binary not found or not executable: $S7MAIN_BIN" >&2
  exit 1
fi

REQ_HELLO='{"jsonrpc":"2.0","id":"1","method":"runtime.hello","params":{"client_name":"smoke","client_version":"0.1.0","protocol_min":0,"protocol_max":0}}'
REQ_TYPES='{"jsonrpc":"2.0","id":"2","method":"runtime.types.list","params":{}}'

OUT_FILE="$(mktemp /tmp/s7_control_stdio_XXXXXX.log)"
{
  printf '%s\n' "$REQ_HELLO"
  printf '%s\n' "$REQ_TYPES"
} | "$S7MAIN_BIN" --control-stdio > "$OUT_FILE"

if ! grep -q '"protocol_version":0' "$OUT_FILE"; then
  echo "Missing runtime.hello response in $OUT_FILE" >&2
  exit 1
fi

if ! grep -q '"types"' "$OUT_FILE"; then
  echo "Missing runtime.types.list response in $OUT_FILE" >&2
  exit 1
fi

echo "control-stdio smoke test passed"
rm -f "$OUT_FILE"

