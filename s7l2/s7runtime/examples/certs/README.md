# S7 Runtime Test Certificates

This directory contains test-only certificates for local development and smoke tests.

Contents:

- `s7_root_ca.cert.pem` / `s7_root_ca.key.pem`: self-signed test root CA
- `s7_runtime_node.cert.pem` / `s7_runtime_node.key.pem`: runtime node certificate signed by test CA

The runtime certificate includes SAN URI:

- `spiffe://s7/node/11111111-2222-3333-4444-555555555555`

These files are for non-production use only.

