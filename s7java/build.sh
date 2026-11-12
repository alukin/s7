#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Compiles s7java (standard library only, Java 21 or later) into build/ and, with --test,
# checks the codec against the shared wire fixtures, the delivery rules, the node's and the JSON
# checks, and /data connections over mTLS on loopback with the repository's test certificates.
# Usage: ./build.sh [--test]   (JAVAC / JAVA select the tools; default: from PATH)
set -euo pipefail
cd "$(dirname "$0")"
JAVAC="${JAVAC:-javac}"
JAVA="${JAVA:-java}"
rm -rf build
mkdir -p build
"$JAVAC" --release 21 -Xlint:all -Werror -d build $(find src test -name '*.java')
if [ "${1:-}" = "--test" ]; then
  "$JAVA" -cp "build${S7_FRAME_CODEC_CLASSPATH:+:$S7_FRAME_CODEC_CLASSPATH}" s7.frames.FrameCheck
  "$JAVA" -cp build s7.wire.WireFixtureCheck ../s7l2/s7transport/tests/fixtures/wire_v1.json
  "$JAVA" -ea -cp build s7.stream.StreamProtocolCheck
  "$JAVA" -ea -cp build s7.node.NodeCheck
  "$JAVA" -ea -cp build s7.net.NetCheck
  "$JAVA" -ea -cp build s7.net.TransportCheck ../s7l2/s7runtime/examples/certs
fi
