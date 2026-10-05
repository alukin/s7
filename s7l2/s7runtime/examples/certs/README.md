# S7 Runtime Test Certificates

Test-only certificates for local development and smoke tests. Regenerate them with `tools/generate_test_runtime_certs.sh` (this changes every fingerprint).

| Files | Signed by | Identity | Purpose |
|---|---|---|---|
| `s7_root_ca.cert.pem` / `.key.pem` | self | `CN=S7 Test Root CA` | Trust root for the test network |
| `s7_runtime_node.cert.pem` / `.key.pem` | test root | SAN `spiffe://s7/node/11111111-2222-3333-4444-555555555555` | Runtime node (server and client); no Studio role |
| `s7_runtime_node_b.cert.pem` / `.key.pem` | test root | SAN `spiffe://s7/node/33333333-4444-5555-6666-777777777777` | Second runtime node, for multi-node deployment tests |
| `s7_studio.cert.pem` / `.key.pem` | test root | SAN `spiffe://s7/node/22222222-3333-4444-5555-666666666666`, Extended Key Usage `clientAuth` + `2.25.4266950079767543379770189370854073200` | Control Studio (client); carries the development control-node key purpose |
| `s7_untrusted_ca.*`, `s7_untrusted_studio.*` | untrusted CA | Studio role | Negative tests: must fail the TLS handshake |

To authorize the test Studio on a node, add to its schema:

```json
"control": {
  "studio_key_purpose_oid": "2.25.4266950079767543379770189370854073200",
  "studio_certificate_sha256": "<openssl x509 -in s7_studio.cert.pem -noout -fingerprint -sha256>"
}
```

The fingerprint may be given with or without colons, in either case.

These files are for non-production use only.
