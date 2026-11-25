# S7 Security TODO

Security findings from the 2026-11-25 review of `s7l2`, deferred for later
work. Each item was traced through the code, but none has a reproducing test
yet. Line numbers refer to commit `dfbecd9`. Fix items in order of priority.
Each fix needs a negative test that fails before the change.

## High

### S1. Outbound TLS client does not authenticate the server

- `s7transport/src/transport.cpp:1935` — `make_client_tls` calls
  `set_default_verify_paths()` before `load_verify_file(ca_file)`, so every
  system CA is trusted alongside the S7 CA.
- No hostname verification (`SSL_set1_host` / rfc2818) and no SNI anywhere
  in `s7transport`, `s7net`, `s7runtime`, `s7ctl`, Studio.
- `s7runtime/src/runtime_service.cpp:1471` — the runtime's configured
  `client.url` connection passes no expected node UUID, so the identity check
  at `transport.cpp:1726` is skipped entirely.
- **Scenario:** an on-path attacker holding any publicly trusted certificate,
  even one for its own domain, is accepted as the runtime's `wss://` peer.
  For deployment clients (`s7net/src/deployment_client.cpp:221`, identity
  checked), any system-trusted CA, such as a corporate inspection CA, can issue a
  certificate with `spiffe://s7/node/<uuid>`.
- **Fix:** trust only `ca_file`; require an expected node identity or hostname
  verification for the runtime client; set SNI.
- **Test:** a server certificate from a different CA that is in the default
  trust store must be refused; a correct-CA certificate with the wrong node
  UUID must be refused on every client path.

## Medium

### S2. Unauthenticated `PEER_ANNOUNCE` overwrites configured seed peers

- `s7runtime/src/control_plane.cpp:1005–1015` — any client can replace a seed
  entry's `node_uuid`, `node_name` and certificate fingerprint; every announce
  rewrites `peers.json` (disk write per unauthenticated request).
- Impact is limited: Studio treats peer reports as unverified
  (`s7studio/src/node_discovery.cpp:270`) and re-contacts nodes directly.
- **Fix:** never let an announce change a `configured_seed` identity; coalesce
  or rate-limit persistence; consider requiring a verified peer certificate.

### S3. Non-atomic writes of node state

- `s7runtime/src/runtime_service.cpp:515` (`node_uuid.txt`) and
  `s7runtime/src/control_plane.cpp:1673` (`peers.json`) use a truncating
  `ofstream`, although `platform::write_file_atomically` exists.
- A crash that leaves `node_uuid.txt` empty makes `load_or_create_node_uuid`
  (`runtime_service.cpp:536`) silently mint a new identity for a node without
  a certificate.
- **Fix:** use `write_file_atomically`; treat an existing empty file as an error.

## Low (hardening)

### S4. Atomic write treats a short `write()` as failure

- `s7core/src/platform.cpp:146` — no retry loop for partial writes. The failure
  is only logged at commit (`control_plane.cpp:1429`), so "the active
  deployment survives restarts" does not hold in that case.
- **Fix:** loop until all bytes are written; report persistence failure in the
  commit result or deployment history.

### S5. Remote stream consumer buffer is unbounded

- `s7transport/src/stream_protocol.cpp:414–420` — `on_packet` appends without
  checking a bound and relies on the producer honoring the acknowledgement
  window. Requires an authenticated (or `trusted_plaintext`) producer.
- **Fix:** fault with a resource-limit code when buffered elements exceed the
  configured window.

### S6. Compressed schema libraries have no decompressed-size limit

- `s7core/src/schema_library.cpp:54–80` — a `.gz` library is inflated without
  a cap, so a hostile downloaded library can exhaust memory.
- **Fix:** cap the decompressed size (configurable) and fail with a clear error.
