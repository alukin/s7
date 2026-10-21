#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

OUT_DIR="${1:-$(cd "$(dirname "$0")/../s7runtime/examples/certs" && pwd)}"
mkdir -p "$OUT_DIR"

ROOT_KEY="$OUT_DIR/s7_root_ca.key.pem"
ROOT_CERT="$OUT_DIR/s7_root_ca.cert.pem"
SERIAL="$OUT_DIR/s7_root_ca.cert.srl"

NODE_UUID="11111111-2222-3333-4444-555555555555"
NODE_B_UUID="33333333-4444-5555-6666-777777777777"
STUDIO_UUID="22222222-3333-4444-5555-666666666666"

# Development placeholder for the S7 control-node (Studio) key purpose, carried
# in Extended Key Usage: a UUID-derived OID under 2.25 (ITU-T X.667), pending
# assignment of S7 OIDs under an enterprise arc. Keep in sync with
# kStudioKeyPurposeOid in s7runtime/tests/deployment_smoke.cpp.
STUDIO_KEY_PURPOSE_OID="2.25.4266950079767543379770189370854073200"

openssl genrsa -out "$ROOT_KEY" 4096
openssl req -x509 -new -nodes -key "$ROOT_KEY" -sha256 -days 3650 \
  -out "$ROOT_CERT" \
  -subj "/C=US/ST=Test/L=Test/O=S7/OU=Test/CN=S7 Test Root CA"

# issue <name> <subject> <extensions> [ca_cert ca_key serial]
issue() {
  local name="$1" subject="$2" extensions="$3"
  local ca_cert="${4:-$ROOT_CERT}" ca_key="${5:-$ROOT_KEY}" serial="${6:-$SERIAL}"
  local key="$OUT_DIR/$name.key.pem" csr="$OUT_DIR/$name.csr.pem"
  local ext="$OUT_DIR/$name.ext" cert="$OUT_DIR/$name.cert.pem"

  openssl genrsa -out "$key" 2048
  openssl req -new -key "$key" -out "$csr" -subj "$subject"
  printf '%s\n' "$extensions" > "$ext"
  openssl x509 -req -in "$csr" -CA "$ca_cert" -CAkey "$ca_key" \
    -CAcreateserial -CAserial "$serial" -out "$cert" -days 825 -sha256 -extfile "$ext"
  rm -f "$csr"
}

# Runtime node: identity in the SAN URI, no Studio role.
issue s7_runtime_node "/C=US/ST=Test/L=Test/O=S7/OU=Runtime/CN=s7-runtime-node" \
"basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=URI:spiffe://s7/node/$NODE_UUID"

# Second runtime node, for multi-node deployment tests.
issue s7_runtime_node_b "/C=US/ST=Test/L=Test/O=S7/OU=Runtime/CN=s7-runtime-node-b" \
"basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=URI:spiffe://s7/node/$NODE_B_UUID"

# Control Studio: carries the control-node key purpose.
issue s7_studio "/C=US/ST=Test/L=Test/O=S7/OU=Studio/CN=s7-studio" \
"basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=clientAuth,$STUDIO_KEY_PURPOSE_OID
subjectAltName=URI:spiffe://s7/node/$STUDIO_UUID"

# Untrusted CA and a Studio-role certificate it signed, for negative tests.
UNTRUSTED_KEY="$OUT_DIR/s7_untrusted_ca.key.pem"
UNTRUSTED_CERT="$OUT_DIR/s7_untrusted_ca.cert.pem"
openssl genrsa -out "$UNTRUSTED_KEY" 2048
openssl req -x509 -new -nodes -key "$UNTRUSTED_KEY" -sha256 -days 3650 \
  -out "$UNTRUSTED_CERT" -subj "/C=US/ST=Test/L=Test/O=Elsewhere/CN=Untrusted Test CA"
issue s7_untrusted_studio "/C=US/ST=Test/L=Test/O=Elsewhere/CN=s7-studio" \
"basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=clientAuth,$STUDIO_KEY_PURPOSE_OID
subjectAltName=URI:spiffe://s7/node/$STUDIO_UUID" \
  "$UNTRUSTED_CERT" "$UNTRUSTED_KEY" "$OUT_DIR/s7_untrusted_ca.cert.srl"

openssl verify -CAfile "$ROOT_CERT" "$OUT_DIR/s7_runtime_node.cert.pem" "$OUT_DIR/s7_runtime_node_b.cert.pem" "$OUT_DIR/s7_studio.cert.pem"

echo "Studio certificate SHA-256 fingerprint:"
openssl x509 -in "$OUT_DIR/s7_studio.cert.pem" -noout -fingerprint -sha256 | sed 's/.*=//; s/://g' | tr 'A-F' 'a-f'
echo "Generated test certificates in: $OUT_DIR"
