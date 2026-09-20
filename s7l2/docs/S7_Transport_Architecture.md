# S7 Transport Architecture

## Status

Draft v1 baseline for implementation.

This document defines module boundaries and runtime transport responsibilities for distributed S7 execution.

---

## Goals

- Keep `s7net` as an element library, not a transport backend.
- Provide a pluggable transport backend that can evolve beyond WebSocket.
- Support secure peer-to-peer communication from day one.
- Support NAT-constrained deployments from day one.

---

## Sub-Project Boundaries

## `s7transport` (new)

`s7transport` owns transport abstractions and backend implementations.

Responsibilities:

- transport/session abstractions
- data stream framing and multiplexing
- reliability mechanics for sample delivery
- buffering and backpressure primitives
- NAT traversal support (STUN + TURN + direct attempts)
- concrete backend implementations:
  - Beast WebSocket/TLS backend (v1)
  - future raw-socket backend (v2+)

Non-responsibilities:

- schema execution semantics
- element-level signal processing logic
- GUI/control policy semantics

## `s7runtime`

`s7runtime` orchestrates distributed behavior using `s7transport`.

Responsibilities:

- load/configure transport backends
- mTLS identity extraction and role policy enforcement
- control transaction policy (`prepare`/`commit`/`abort`)
- stream lifecycle and route management
- cluster-level session and health orchestration

Non-responsibilities:

- transport framing internals
- element library behavior

## `s7net`

`s7net` remains an element library.

Responsibilities:

- define abstract network elements (`NetSink`, `NetSource`) as reusable element bases
- provide runtime-backed element implementations for inter-runtime links
- provide adapter elements for external systems
- consume transport contracts from `s7transport` where required by reusable element behavior

Non-responsibilities:

- direct transport backend ownership
- mTLS/NAT/session orchestration

---

## Dependency Direction

Required direction:

- `s7transport` -> no dependency on `s7runtime` or `s7net`
- `s7runtime` -> depends on `s7transport`
- `s7net` -> depends on runtime-facing service interface and transport contracts from `s7transport`, but does not own transport backend implementations

This avoids coupling element code to specific wire protocols.

---

## Runtime Network Service Contract

`s7runtime` should expose a narrow service contract consumed by `s7net` elements.

Suggested interface sketch:

```cpp
class IRuntimeNetworkService {
public:
    virtual ~IRuntimeNetworkService() = default;

    virtual PrepareResult prepare_stream(const StreamPrepareRequest&) = 0;
    virtual CommitResult commit_stream(const StreamCommitRequest&) = 0;
    virtual void abort_stream(const StreamAbortRequest&) = 0;

    virtual SendResult send_packet(const DataPacket&) = 0;
    virtual SubscribeResult subscribe_stream(const SubscribeRequest&) = 0;

    virtual FlowResult update_flow_window(const FlowWindowUpdate&) = 0;
};
```

`s7net` elements use this service instead of owning sockets.

---

## Security and Identity Baseline

Transport and runtime must enforce:

- TLS 1.3 + mutual TLS
- node identity in SAN URI: `spiffe://s7/node/<uuid>`
- role classification in X.509 Certificate Policies OIDs:
  - runtime-node policy
  - control-node policy
- signed trusted-CA manifest JSON plus detached signature file (`.sig`), verified against primary CA

Only `control-node` role may perform control mutations.

---

## One-Port Channel Model

A node exposes one TLS port with two WebSocket paths:

- `/control` for authenticated control messages (JSON)
- `/data` for multiplexed data transport (binary)

Both paths share peer identity and certificate context.

---

## NAT Traversal Baseline

v1 behavior:

1. gather candidate addresses (local + reflexive via STUN)
2. attempt direct connect paths
3. fallback to TURN relay when direct path fails

This is required for mixed private/public deployments.

---

## Reliability and Ordering Baseline

Data-plane baseline:

- all samples must be delivered
- global ordering across all multiplexed channels per connection
- packet-level model timestamp required (`t_model`)
- system timestamp optional/required by source profile (`t_system`)
- variable-length packets with max payload `256 KiB`
- big-endian numeric encoding on wire

Priority policy baseline:

1. control traffic
2. element-connection data traffic
3. observation traffic

Observer transport is network-transparent but lower priority than functional graph data transport.

---

## Open Questions

No unresolved architecture questions remain for v1 baseline in this document.

