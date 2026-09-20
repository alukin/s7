#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-$(cd "$(dirname "$0")/../s7runtime/examples/certs" && pwd)}"
mkdir -p "$OUT_DIR"

ROOT_KEY="$OUT_DIR/s7_root_ca.key.pem"
ROOT_CERT="$OUT_DIR/s7_root_ca.cert.pem"
NODE_KEY="$OUT_DIR/s7_runtime_node.key.pem"
NODE_CSR="$OUT_DIR/s7_runtime_node.csr.pem"
NODE_EXT="$OUT_DIR/s7_runtime_node.ext"
NODE_CERT="$OUT_DIR/s7_runtime_node.cert.pem"
SERIAL="$OUT_DIR/s7_root_ca.cert.srl"

NODE_UUID="11111111-2222-3333-4444-555555555555"

openssl genrsa -out "$ROOT_KEY" 4096
openssl req -x509 -new -nodes -key "$ROOT_KEY" -sha256 -days 3650 \
  -out "$ROOT_CERT" \
  -subj "/C=US/ST=Test/L=Test/O=S7/OU=Test/CN=S7 Test Root CA"

openssl genrsa -out "$NODE_KEY" 2048
openssl req -new -key "$NODE_KEY" -out "$NODE_CSR" \
  -subj "/C=US/ST=Test/L=Test/O=S7/OU=Runtime/CN=s7-runtime-node" \
  -addext "subjectAltName=URI:spiffe://s7/node/$NODE_UUID"

cat > "$NODE_EXT" <<EOF
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=URI:spiffe://s7/node/$NODE_UUID
EOF

openssl x509 -req -in "$NODE_CSR" -CA "$ROOT_CERT" -CAkey "$ROOT_KEY" \
  -CAcreateserial -CAserial "$SERIAL" -out "$NODE_CERT" -days 825 -sha256 -extfile "$NODE_EXT"

openssl verify -CAfile "$ROOT_CERT" "$NODE_CERT"

echo "Generated test certificates in: $OUT_DIR"

