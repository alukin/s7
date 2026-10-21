#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

# Checks Studio's wire type labels on laid-out diagrams (offscreen).
set -euo pipefail
# In a cross build, S7_RUN runs the target's programs (Wine); run drops the
# \r of their \r\n output.
run() { if [ -n "${S7_RUN:-}" ]; then "$S7_RUN" "$@" | tr -d '\r'; else "$@"; fi; }
STUDIO="$1" TESTS="$2"
export QT_QPA_PLATFORM=offscreen

ok=$(run "$STUDIO" --wire-types "$TESTS/wire_types_ok.json")
echo "$ok"
for expected in "src:0=I16" "gain:0=I16" "iq:0=CI8" "conv:0=C32" "split:0=F32"; do
    grep -qx "$expected" <<< "$ok" || { echo "missing label: $expected"; exit 1; }
done
if grep -q "^error:" <<< "$ok"; then echo "unexpected type error"; exit 1; fi

conflict=$(run "$STUDIO" --wire-types "$TESTS/wire_types_conflict.json")
echo "$conflict"
grep -q "^error: Type conflict" <<< "$conflict" || { echo "conflict not reported"; exit 1; }
grep -qx "a:0=?" <<< "$conflict" || { echo "conflicting wires are not marked '?'"; exit 1; }
