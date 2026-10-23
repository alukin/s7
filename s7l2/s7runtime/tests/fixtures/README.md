# Control protocol fixtures

`control_v1.json` is the conformance script of the S7 control protocol,
version 1 (`../doc/S7_Control_Protocol_MVP.md`, section 9). It describes one
node (`node`) and the peers requests come from (`peers`), then gives `steps`:
each a request and what its response must hold.

To check an implementation, start a node as `node` describes (its node UUID,
listen URL, `max_configuration_bytes`, a synchronized clock, and one consumer
stream that faulted with `UNDERRUN`), authorize the Studio certificate
`s7runtime/examples/certs/s7_studio.cert.pem`, and send the steps in order:

- replace each `"${name}"` in a request with the value captured earlier
  (`capture: {name: member}` of a previous step);
- the response must have the step's `type`, every member listed in `fields`
  (paths such as `a.b` and `a[0].b`) with the given JSON type (`string`,
  `integer`, `boolean`, `object`, `array`, `null`, or `x|y` for either), and
  the exact values of `equals`. Other members may be present.

The configuration digests in the requests are RFC 8785 (JCS) SHA-256 digests
computed by `tools/gen_control_fixtures.py`, which writes the file; a node
that canonicalizes differently refuses the prepare steps.
`s7runtime_control_fixtures_smoke` runs the script against the C++ control
plane with an accepting deployment host; it checks protocol messages without
compiling the fixture graphs or performing TLS handshakes.

## TLS configuration for a live node

The script uses two peers: an anonymous peer without a client certificate and
the authorized Studio. C++ defaults to `websocket.server.tls.require_client_cert`
being `true`, which rejects the anonymous peer during the TLS handshake. To exercise
both peers against a live C++ node, explicitly set this test configuration:

```json
{
  "websocket": {
    "server": {
      "enabled": true,
      "bind_address": "127.0.0.1",
      "port": 9002,
      "tls": {
        "enabled": true,
        "cert_file": "s7runtime/examples/certs/s7_runtime_node.cert.pem",
        "key_file": "s7runtime/examples/certs/s7_runtime_node.key.pem",
        "ca_file": "s7runtime/examples/certs/s7_root_ca.cert.pem",
        "require_client_cert": false
      }
    }
  }
}
```

Merge this server configuration into the fixture node's schema; it does not
supply the other node, authorization, clock or stream settings listed above.
Certificate paths here are relative to `s7l2/`.

With optional client certificates, the server still verifies certificates
when supplied. Read-only requests can reach the control plane anonymously;
mutations still require a verified certificate with the control-node key
purpose and the pinned Studio fingerprint. An anonymous TLS mutation returns
`CERTIFICATE_REQUIRED`; a mutation without TLS returns `UNAUTHORIZED`.
The Python runner (`s7py/tools/check_control_fixtures.py`) handles this
transport-dependent error. `s7runtime_control_smoke` checks both settings:
required certificates reject anonymous handshakes, while optional certificates
allow anonymous `HELLO` and refuse anonymous deployment mutations.
