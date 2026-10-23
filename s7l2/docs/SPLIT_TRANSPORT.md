# Separate control and data listeners

Split transport is a 0.1.0 preview requirement added on 2026-10-23. It lets
headless nodes keep authenticated control while choosing whether to encrypt
bulk data. The wire packet format, causal graph, stream ordering, backpressure
and recovery rules do not change.

## Node configuration

The existing shared listener remains available. To use separate listeners,
add `websocket.data_server` to the runtime node's schema:

```json
{
  "websocket": {
    "server": {
      "enabled": true,
      "port": 9002,
      "bind_address": "127.0.0.1",
      "tls": {
        "enabled": true,
        "cert_file": "s7runtime/examples/certs/s7_runtime_node.cert.pem",
        "key_file": "s7runtime/examples/certs/s7_runtime_node.key.pem",
        "ca_file": "s7runtime/examples/certs/s7_root_ca.cert.pem",
        "require_client_cert": true
      }
    },
    "data_server": {
      "enabled": true,
      "port": 9003,
      "bind_address": "127.0.0.1",
      "security": "trusted_plaintext"
    }
  }
}
```

Merge this into a node schema with its graph, rate, clock and pinned Studio
authorization settings. Each process still needs its own state directory.
Certificate paths above are relative to `s7l2/`.

With split listeners, `server` accepts only `/control` and requires mTLS with
client certificates. `data_server` accepts only `/data`. Both listeners share
one transport, stream table and I/O thread. JSON control operations cannot be
performed through the data listener.

`data_server.security` supports:

| Value | Data transport | Peer identity and trust |
|---|---|---|
| `mtls` (default) | `wss://` | Verified node certificate; uses the control server's TLS credentials |
| `trusted_plaintext` | `ws://` | Node UUID claimed in `S7-Node`; explicitly trusts the network |

Plaintext is an explicit deployment policy. It supplies no cryptographic
confidentiality, packet integrity or proof of peer identity. Packet type,
configured producer/consumer, sequence and timestamp checks remain in force.
Control authentication does not authenticate a separate plaintext socket.
No per-packet cryptography is added to the trusted plaintext path.

An empty plaintext bind address means `127.0.0.1`. For IPv6 loopback use
`"bind_address": "::1"` and bracket IPv6 addresses in URLs. To use a trusted
LAN, explicitly bind its interface address, or bind `0.0.0.0`/`::` and set
`advertise_address` to the reachable hostname/address. Both `server` and
`data_server` accept `advertise_address`. It changes advertisement, not binding.
Without it, wildcard listeners advertise loopback; they cannot infer their
externally reachable address. Port 0 selects a free port; discovery reports
the actual bound port.

## Deployment and discovery

A node with a separate listener advertises these additional `NODE_INFO.network`
fields through its control endpoint:

```json
{
  "listen_url": "wss://127.0.0.1:9002",
  "control_path": "/control",
  "data_path": "/data",
  "data_listen_url": "ws://127.0.0.1:9003",
  "data_security": "trusted_plaintext",
  "separate_data_listener": true
}
```

In the committed diagram, declare the control and data endpoints separately:

```json
{
  "runtime_nodes": [
    {
      "node_uuid": "11111111-2222-3333-4444-555555555555",
      "name": "node-a",
      "endpoint": "wss://127.0.0.1:9002",
      "data_endpoint": "ws://127.0.0.1:9003",
      "data_security": "trusted_plaintext"
    }
  ]
}
```

`data_endpoint` and `data_security` must be supplied together. `mtls` requires
`wss://`; `trusted_plaintext` requires `ws://`. Omit both for a shared listener.
Nodes using split listeners reject network graphs whose declared local policy
does not match their configured listener. A consumer must also opt into
`trusted_plaintext` locally before accepting a plaintext producer endpoint.
An encrypted connection failure never triggers a plaintext fallback.

Studio learns separate endpoints only by directly contacting a node whose
certificate verifies its claimed UUID, and includes those endpoints when
deploying. `s7ctl deploy` uses the endpoints in the diagram. Changing transport
routes alone preserves stream IDs and sequence state.

Split-mode data peers are configured by the committed diagram. In this mode,
`websocket.client.url` establishes control only; it does not infer or open a
data connection to the control listener.

## Other implementations and observation

A control client connects only to `/control`; it does not need to open a data
socket. In C++, use `configure_client(url, tls, false)` or `BeastControlRequester`.
Python and other implementations can implement control before adding a data
plane. A data client selects the explicitly configured URL and implements the
existing binary packet, ACK and RESUME protocol on `/data`. Trusted plaintext
connections announce the node UUID in the `S7-Node` WebSocket handshake header.

Observation within Studio's bundled runtime already uses memory, without TLS
or sockets. This change provides a transport choice for networked samples; it
does not add a remote probing/subscription API or put observers in the graph.

## Verification

`s7transport_beast_stream_smoke` covers IPv4/IPv6 split listeners, plaintext
data beside mTLS control, channel isolation, explicit policy errors and data
disconnect/resume while control stays connected. `s7net_diagram_deployment_smoke`
checks endpoint selection and scheme/policy validation. Runtime and Studio
checks cover configuration errors and verified discovery metadata.

`s7_two_node_split_mtls` and `s7_two_node_split_trusted_plaintext` deploy real
runtime processes with `s7ctl`, reject policy mismatches, redeploy and recover
after a producer restart. An independent standard-library Python WebSocket peer
checks mTLS control, fragmented responses, the selected data transport and
wrong-channel rejection. No throughput or Raspberry Pi platform qualification
is implied by these functional checks.
