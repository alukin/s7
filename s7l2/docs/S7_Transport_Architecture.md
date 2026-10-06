# S7 Transport Architecture

## Status

Evolving architecture proposal. The data plane (wire format v1, lossless streams over WebSocket / mTLS) is implemented; other parts described here are not yet.

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
- role classification in X.509 v3 Extended Key Usage, with S7 key-purpose OIDs next to `serverAuth`/`clientAuth`:
  - runtime-node purpose
  - control-node purpose (Studio)
- signed trusted-CA manifest JSON plus detached signature file (`.sig`), verified against primary CA

Only `control-node` role may perform control mutations.

---

## Peer and Channel Model

A runtime node MAY maintain simultaneous peer connections to multiple runtime nodes. Each peer connection pair uses one authenticated identity and two WebSocket paths:

- `/control` for authenticated control messages (JSON)
- `/data` for multiplexed binary data packets

The control and data connections for a peer MUST be bound to the same authenticated peer identity. A data connection can carry multiple configured streams for that peer. Stream routing is by configured stream identity; an implementation MUST NOT use packet arrival order to infer causal ordering across separate streams or peer connections. The current Beast endpoint still keeps one data/control socket pair and must be changed to support this model.

---

## NAT Traversal Baseline

v1 behavior:

1. gather candidate addresses (local + reflexive via STUN)
2. attempt direct connect paths
3. fallback to TURN relay when direct path fails

This is required for mixed private/public deployments.

---

## Reliability and Ordering Baseline

Data-plane baseline (see [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md)):

- lossless, exactly-once, in-order delivery per stream while both processes run (send log, cumulative ACK, RESUME and replay); producers reserve sequence ranges durably, so a restart is never mistaken for continuity: it faults the stream (`SEQUENCE_GAP`) until recovered
- ordering is defined per stream; no global ordering is implied across different streams or peers
- timestamps are described by explicit stream roles (`MODEL_TIME`, `SOURCE_TIME`, `PROCESSING_TIME`) and clock domains; uniformly sampled streams derive element times from their sample rate, others carry them per packet
- each stream travels only between its configured producer and consumer nodes
- model time is simulation time; it is not inferred from wall-clock time
- variable-length packets with max payload `256 KiB`
- little-endian numeric encoding on wire, written explicitly (never by copying host memory)

Observation is local to a runtime node. Studio Scope may observe outputs in its local runtime, including outputs of configured network-source elements after packets cross an explicit graph boundary. The observation API does not probe remote nodes and does not bypass the data protocol.

The wire protocol requires support for all listed numeric types and all timestamp roles, with each stream declaring its timing. Non-model clocks have configured domains and must change domain ID when restart continuity cannot be guaranteed. The control protocol defines canonical JSON SHA-256 digests, bounded chunked configuration transfer, one CA-policy-authorized Studio identity, and status/retry recovery for partial commits. Both protocols are implemented by the C++ runtime and have conformance data for other implementations: `s7transport/tests/fixtures/wire_v1.json` and `s7runtime/tests/fixtures/control_v1.json`. A process crash faults the streams it took part in and is not recovered in the MVP.
