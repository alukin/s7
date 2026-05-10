#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
S7MAIN_BIN="${1:-$ROOT_DIR/build_verify/s7main/s7main}"

if [[ ! -x "$S7MAIN_BIN" ]]; then
  echo "s7main binary not found or not executable: $S7MAIN_BIN" >&2
  exit 1
fi

echo "[1/5] Listing I/O element types"
"$S7MAIN_BIN" --list-types | grep -E "(File|Mem|Net)" || true

echo "[2/5] Validate file I/O model"
"$S7MAIN_BIN" --schema "$ROOT_DIR/s7main/examples/io_file_real_sink.json" --validate-only

echo "[3/5] Run file I/O model for 16 ticks"
rm -f /tmp/s7_real_sink.s7io
"$S7MAIN_BIN" --schema "$ROOT_DIR/s7main/examples/io_file_real_sink.json" --ticks 16

if [[ ! -f /tmp/s7_real_sink.s7io ]]; then
  echo "Expected output file missing: /tmp/s7_real_sink.s7io" >&2
  exit 1
fi

echo "[4/5] Run mem I/O model for 16 ticks"
"$S7MAIN_BIN" --schema "$ROOT_DIR/s7main/examples/io_mem_real_sink.json" --ticks 16

echo "[5/5] Run net->file loopback model for 16 ticks"
rm -f /tmp/s7_real_sink.s7io
"$S7MAIN_BIN" --schema "$ROOT_DIR/s7main/examples/io_net_real_loopback_to_file.json" --ticks 16

if [[ ! -f /tmp/s7_real_sink.s7io ]]; then
  echo "Expected loopback output file missing: /tmp/s7_real_sink.s7io" >&2
  exit 1
fi

echo "All I/O example models ran successfully."

