# Code Review: Required Fixes

**Status:** Working fix checklist for the S7 MVP; implementation is in progress.
**Scope:** Findings from the read-only review of the current C++ implementation. This document describes required behavior and acceptance criteria; it is not a claim that the current runtime conforms to the protocol drafts.

The project is a work in progress. These items are prioritized by their effect on correctness, availability, security, and the ability to build interoperable runtime nodes in different languages.

## Priority guide

- **P1 — Required before relying on the feature:** correctness, crash, or authorization defect in an exercised path.
- **P2 — Required before multi-node MVP use:** behavior or integration that prevents the documented distributed use case.
- **P3 — Follow-up hardening:** valuable robustness work that is less likely to invalidate basic MVP use.

## Progress

- **Implemented and tested (2026-10-04):** only causal-memory elements (`Feedthrough::None`: `Delay`, `PreviousValue`) are deferred; their outputs are staged and published at the start of the next step. Stateful elements with direct feedthrough run in topological order with stateless ones. See the resolved "Every stateful element delays its output by one step" item.
- **Implemented and tested (2026-10-04):** the transport prototype requires a model timestamp (zero is valid), validates frame length, flags, reserved field, timestamp presence, data type and payload size, and reports reader-thread decode failures through `take_transport_errors()` instead of `receive()`. In-memory delivery round-trips packets through the same codec. Covered by `s7transport_validation_smoke`. Its codec API is named `*_prototype` to distinguish it from the candidate wire format.
- **Wire format remains under review:** [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md) now describes candidate draft 0.3, explicitly not a conformance standard. Decisions recorded: every listed numeric type and all timestamp roles are mandatory capabilities; each stream declares the roles carried per packet; both ends persist sequence state; non-model clock domains are configured and must change when continuity across restart is not guaranteed. The exact crash delivery guarantee, descriptor serialization, fault messages, and conformance fixtures remain open. The candidate still does not match the prototype codec.
- **Control draft updated:** canonical JSON digesting uses RFC 8785 JCS plus SHA-256; configuration bundles have bounded direct/chunked transfer; one CA-policy-authorized Studio is configured; partial commits are reconciled through status queries and idempotent retries. Exact message schemas, diagram/schema representation, retention, and request replay cache policy remain open.
- **Implemented and tested (2026-10-04):** the transport keeps multiple simultaneous peers with bounded, observable send and receive queues (see the multiple-peer item). Publishing with no connected data peer drops the packet and reports `SendResult::delivered == false` (counted by `NetSink::dropped_count()` and transport stats). Per-peer stream routing waits for the stream descriptors of wire draft section 12.1.
- The review also confirmed that observation is local to one runtime node. Remote-origin values can be observed after entering the local graph through explicit network source/sink elements; observation does not bypass network boundaries. Scope queue and recording bounds remain open work.

## P1: Fix data packet validation and receive failure handling — implemented and tested

### Current problem

Network reader threads called the packet decoder without handling its exceptions. Malformed or unsupported input could therefore escape a thread entry point and terminate the process. The prototype decoder also accepted trailing bytes after the declared payload, although the wire-format draft requires exact message length.

### Required behavior

1. Reject any packet whose total length is not exactly the fixed header plus extensions plus payload.
2. Validate the packet fields needed to interpret the payload, including data type, element count, and timestamp/flag combinations, before queueing it.
3. Convert decode failures into a reported connection/packet error. A bad packet MUST NOT terminate the runtime process.
4. Define whether a malformed packet closes the connection or is discarded; apply that policy consistently on client and server readers.

### Acceptance criteria

- Truncated, oversized, unsupported-version, invalid-length, and trailing-byte frames are rejected.
- Rejection is observable through the endpoint/runtime error path.
- A malformed frame does not terminate the process or prevent unrelated runtime work from continuing.

**Progress:** exact frame length, supported flags, reserved field, timestamp/flag consistency, data type (prototype supports scalar F64 only, code `kPrototypeDataTypeF64`), element count versus payload size, extension layout, and reader-thread exception handling are implemented. Policy: a malformed frame is reported as a connection-level error and closes that peer's connection; the endpoint keeps serving and accepts new peers. Errors are not tied to a stream, so they are returned by `IRuntimeNetEndpoint::take_transport_errors()` (bounded to 32, oldest dropped) rather than by `receive()`; `NetSource` therefore no longer fails a runtime step because of another peer's bad frame. `s7runtime` prints them as `runtime.transport.error=` while serving. Protocol-version behavior is "reject anything but version 1". Tests: `s7transport_validation_smoke` rejects 16 malformed-frame variants (truncated, oversized, unsupported version/flags/type, invalid lengths, trailing bytes, count mismatch), rejects 6 invalid packets at encode time, and checks over WebSocket that a malformed frame is reported and closes only that connection while a new peer's packets are still delivered. Manually verified: a stepping `s7runtime` node kept stepping after receiving a malformed frame and logged the error. Mapping errors to control-plane fault messages waits for the wire/control drafts.

Relevant code: [transport.cpp](../s7transport/src/transport.cpp:256), [transport.cpp](../s7transport/src/transport.cpp:556).

## P1: Make stateful graph updates independent of declaration order — implemented and tested

### Current problem

`NodeRuntime::step()` runs stateless nodes and then steps stateful nodes sequentially. The compiler builds the stateful list in schema declaration order. A stateful element later in that list can therefore read an earlier stateful element's newly updated output in the same tick. Reordering otherwise identical schema elements may change results, and chains of delay/memory elements may not preserve the intended one-step causal behavior.

### Required behavior

Define and implement explicit stateful update semantics. All stateful elements participating in a runtime step MUST observe the same logically defined pre-update state where the element contract requires previous-step values. Updating/committing state MUST NOT expose partial same-step updates based only on declaration order.

The chosen lifecycle (for example, read/compute followed by commit) must be documented for element authors and applied consistently to built-in and plugin elements. This fix must preserve the existing rule that cycles require causal-memory elements.

### Acceptance criteria

- A stateful chain produces the same trace when element declarations are reordered.
- A delay chain advances by the documented number of steps.
- Feedback loops with memory remain valid and deterministic.

**Progress:** causal-memory elements step after all other elements; each one's published output stays at its pre-step value until the step ends, and the staged outputs are published at the start of the next step. Elements with direct feedthrough are ordered topologically, so declaration order does not matter for them either. `core_smoke` covers a delay chain declared in both orders, `Delay`/`PreviousValue` timing, and the feedback loop. Element-private state advances during its own `step()` call; it must not serve as same-pass cross-element communication.

Relevant code: [runtime.cpp](../s7core/src/runtime.cpp:9), [compiler.cpp](../s7core/src/compiler.cpp:160), [elements.hpp](../s7core/include/s7/elements.hpp:161).

## P1: Permit zero model time — implemented and tested

### Current problem

The packet encoder rejects packets whose model timestamp is zero. `NetSink` assigns `t_model` from its sequence counter, which begins at zero, so its initial packet cannot be encoded. Zero is also a valid model-time origin.

### Required behavior

Timestamp presence MUST be represented separately from timestamp value. A present model timestamp of zero is valid. If a stream profile requires model time, reject only a missing timestamp, not the value zero.

### Acceptance criteria

- A packet at model time zero encodes, decodes, and round-trips with the timestamp intact.
- A packet without a required timestamp is rejected explicitly.
- The runtime's first packet on a newly created stream can be sent.

**Progress:** `DataPacket::t_model_ns` is the only model-time field (the redundant `double t_model` was removed) and is required: the encoder rejects a packet without it, and zero is valid. `s7transport_validation_smoke` round-trips model time zero and system time zero, rejects a missing model timestamp (codec and in-memory publish), and delivers a first packet with sequence 0 and model time 0 over WebSocket. `NetSink` sets `t_model_ns = sequence × 1 s` as a placeholder until the observation-time item defines model time.

Relevant code: [net.cpp](../s7net/src/net.cpp:133), [transport.cpp](../s7transport/src/transport.cpp:224).

## P1: Enforce control authorization using the authenticated peer — fixed 2026-10-04

### Current problem

The TLS server can request a client certificate, but the control callback receives only JSON text. The request dispatcher does not receive a verified node identity or certificate role before processing configuration-mutating requests. Trusting certificate-chain validity alone does not establish that the peer is an authorized S7Studio operator.

### Required behavior

1. Extract the authenticated peer identity and authorization attributes from the TLS session.
2. Bind that identity to the protocol connection; do not accept JSON identity claims as authentication.
3. Require the Studio control role for configuration-changing operations and reject unauthorized peers before they reach mutation handlers.
4. Require trusted TLS for control mutations. Plain WebSocket mode may remain available for local development but MUST NOT authorize configuration changes.

The certificate attribute format and provisioning policy should match the identity/provisioning design document.

### Acceptance criteria

- An untrusted, missing, invalid, or wrong-role certificate cannot prepare, commit, or abort a deployment.
- A valid runtime-node certificate cannot act as Studio authority solely by sending a Studio identity in JSON.
- Authorized requests are associated with the authenticated certificate identity in logs/errors without exposing key material.

**Resolution:** the transport hands each control request to the handler with a `ControlPeer` (TLS flag plus, for a chain-verified client certificate, its SHA-256 fingerprint, SAN URIs and policy OIDs). Prepare/commit/abort require TLS, a verified certificate, the configured control-node key purpose in Extended Key Usage (moved from Certificate Policies on 2026-10-05) and the pinned Studio fingerprint (schema `control` block); JSON claims are ignored. Decisions are logged (`control.authorized` / `control.denied`) with the certificate fingerprint. The test PKI now has a Studio certificate with a development key-purpose OID (`2.25.…`, placeholder until S7 OIDs are assigned) and an untrusted-CA Studio certificate. `s7runtime_deployment_smoke` covers plain, certificate-less, runtime-node (with forged JSON claims), unpinned, unconfigured and untrusted-CA peers, and an authorized Studio over real mTLS; `s7runtime_control_smoke` checks that plain WebSocket mutations are refused.

Relevant code: [transport.cpp](../s7transport/src/transport.cpp:500), [transport.cpp](../s7transport/src/transport.cpp:565), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:200).

## P2: Approve a normative stream identity and packet layout before claiming conformance — approved 2026-10-05; implementation pending

### Current problem

Candidate draft 0.2 specifies a 128-bit stream UUID and an `S7DP` magic/version header. The current prototype codec uses a 64-bit `stream_id` and a different header layout. Implementations following the candidate cannot safely interoperate with the prototype. The draft has open review items and is not yet the approved MVP contract.

### Required behavior

Review and approve one versioned packet format, then keep the implementation, public types, examples, and wire-protocol document in sync. Do not silently reinterpret candidate fields or call the prototype conformant. Current candidate decisions require 128-bit stream identity, mandatory support for every listed numeric type and timestamp role, per-stream required timestamp roles, durable sender and receiver sequence state, and explicit clock domains. Crash delivery semantics, descriptor serialization, fault messages, and negotiation details still need resolution before codec implementation.

Also ensure that timestamp roles and their presence are encoded unambiguously; the draft distinguishes model, source, and processing time while the current packet representation has a single model timestamp and optional system timestamp.

### Acceptance criteria

- Independent implementations can encode/decode the same normative packet examples byte-for-byte.
- Protocol versioning rejects incompatible layouts explicitly.
- Stream identity remains stable and unique for the configured stream lifetime.

**Progress:** decided 2026-10-05 (wire draft 0.4, section 12.1): data must not be lost. While both processes run, delivery is exactly-once and in order across connection loss, using an in-memory send log, cumulative `ACK`, `RESUME` and replay; a full send log blocks the producer up to a configured limit, then faults the stream; process crashes fault the stream (no recovery in the MVP); every gap is a fault (no gap policy). Streams follow the schema's explicit network connections directly from producer to consumer; no relaying in the MVP, but the format stays end-to-end so relays can be added. Still awaiting approval: stream descriptor serialization (item 1), fault reporting (item 3), negotiation (item 4). The Section 7 codec is not implemented until those are settled.

Also decided 2026-10-05 (wire draft 0.5): every port and stream has one exact wire type with no implicit conversion; the type set is I8–I128, U8–U128 (including I12/U12), F16, F32, F64, F128, C16–C128, and complex integers CI8, CU8, CI16 for native SDR I/Q; a 16-byte little-endian header carries version, flags, `data_type`, a per-connection `stream_alias` and `sequence`; element count follows from the message length; uniformly sampled streams carry no per-packet timestamps; batching up to `max_elements_per_packet` with `max_batch_latency_ms` (default 10 ms), both configured on the network boundary element. The stream descriptor (item 1) is approved as proposed. Studio deploys diagrams with per-element `node` placement (item 6). Fault reporting (item 3) and negotiation (item 4) approved as proposed the same day; the draft is the approved MVP contract. Remaining: C++ implementation and cross-language fixtures.

**Consequence for the current transport:** the drop-oldest send and receive queues from the multiple-peer item are wrong for data streams under this guarantee. With the new codec they become the send log (block, then fault) and receive-side delivery without dropping; fan-out to every connected peer becomes per-connection routing to the configured consumers.

Relevant code: [transport.hpp](../s7transport/include/s7/transport.hpp:1), [transport.cpp](../s7transport/src/transport.cpp:210), [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md:1).

## P2: Implement multiple-peer endpoint behavior — fixed 2026-10-04 (routing by stream descriptor pending)

### Current problem

The WebSocket server stores only one active data socket and one control socket. A later peer replaces the previous connection. That does not provide a general multi-peer endpoint for a distributed network.

### Required behavior

Support multiple simultaneous peers with explicit routing by authenticated peer and stream, as selected for the MVP topology. Do not silently disconnect an existing peer when another connects.

Define bounded queue/backpressure behavior as part of endpoint operation. A producer that outruns a receiver must not cause unbounded memory growth; report overload or apply a documented bounded policy.

### Acceptance criteria

- Connecting a second permitted peer does not unexpectedly replace the first.
- Packets are delivered only to the intended stream/peer endpoints.
- Queue limits and overload behavior are explicit and observable.

**Resolution:** `BeastRuntimeNetEndpoint` keeps a list of independent connections instead of one data and one control socket. A server accepts up to `TransportLimits::max_inbound_connections` (default 64); further connections are refused and counted, never replacing an existing peer. Each data connection has a bounded send queue (default 1024 packets) drained by its own writer thread, so `publish()` never blocks the step loop; a full queue drops its oldest packet. Receive queues are bounded per stream (default 1024, oldest dropped) and in the number of distinct streams (default 256); the in-memory endpoint uses the same queues. `stats()` reports connections, rejections, packets sent/received and every drop counter; `s7runtime` prints them when it stops serving. `configure_client()` still replaces only this endpoint's own outbound connections (reconnect). Tests: `s7transport_multipeer_smoke` (two peers coexist in both directions and on control, fan-out, inbound limit, receive queue and stream limits, a stalled peer cannot block `publish()`, no-peer drops, in-memory bound); manually, one node fed two receiver nodes at once.

Not done: packets are still fanned out to every connected data peer and selected by stream name at the receiver. Delivery "only to the intended stream/peer" and binding a stream to its authenticated producer need the stream descriptors of the committed configuration (wire draft section 12.1, awaiting approval).

Relevant code: [transport.cpp](../s7transport/src/transport.cpp:523), [transport.cpp](../s7transport/src/transport.cpp:587).

## P2: Make deployment readiness and commit reflect actual runtime state — fixed 2026-10-04

### Current problem

The current control-plane implementation can return `READY` after checking a limited set of capability fields. `COMMIT_DEPLOYMENT` updates an in-memory deployment identifier and reports `running`, but does not load/validate the configuration bundle, compile it, activate it, or confirm that the runtime is executing that revision. Malformed capability entries can be skipped rather than rejected. The response can therefore overstate what the node has prepared or committed.

### Required behavior

For the MVP, define exactly what a runtime node prepares and commits. At minimum, readiness must mean the node has received and validated its assigned configuration and can activate it; commit must activate that configuration or return a failure. If activation is intentionally outside this control-plane stub, responses and protocol documentation MUST use state names that do not claim the deployment is running.

Reject malformed required capability structures instead of silently treating them as absent. Validate integer ranges before narrowing revision values.

### Acceptance criteria

- A successful prepare is tied to a specific configuration revision/digest and validated node assignment.
- A successful commit can be observed in the actual runtime state.
- A failed compile/activation does not report `running` and leaves the last committed runtime configuration active.
- Invalid capability/request shapes produce an explicit error or `NOT_READY` result.

**Resolution:** prepare requires `revision`, `base_revision` and a `configuration` whose `configuration_digest` (SHA-256 of RFC 8785 canonical JSON) must match; the node compiles `assigned_fragment` and keeps the compiled graph staged. Commit swaps it into the running node through `RuntimeService` (graph owned by the service, stepped under a mutex) and reports `state: active` plus the real `execution` state; a failed activation returns `INTERNAL_ERROR` and leaves the previous graph running. Revisions are range-checked (≤ 2^53), malformed `requires` entries are `INVALID_REQUEST`, compile failures are `NOT_READY`/`INVALID_CONFIGURATION`, and a preparation superseded by another commit on the same base gets `REVISION_CONFLICT`. "running" is no longer reported anywhere. Details in `S7_Control_Protocol_MVP.md` section 10.1. Not done: chunked transfer, persistence of deployment state across restarts.

Relevant code: [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:519), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:552), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:675).

## P2: Define a thread-safe consistency model for observation — fixed 2026-10-04

### Current problem

`ObservationService` reads a runtime's current output values and step count directly. It has no synchronization with `NodeRuntime::step()`. Studio's current local runner samples on the same worker thread immediately after stepping, which avoids concurrent access in that path, but the public API does not state or enforce that restriction. Concurrent sampling can race with output mutation; a batch may also contain samples from different runtime steps.

### Required behavior

Define how observation obtains a consistent sample while execution is active. Options include restricting sampling to the runtime execution thread, providing an explicit synchronized snapshot operation, or publishing immutable snapshots at step boundaries. The API contract must say whether a batch represents one common step. Avoid presenting unsynchronized reads as safe merely because the observation methods are `const`.

### Acceptance criteria

- The API documents and enforces its threading contract.
- A multi-probe snapshot has a coherent step identity, or explicitly reports per-sample step identities when coherence is not guaranteed.
- Sampling cannot data-race with element output writes or `step_count_` updates.

**Resolution:** the contract is "observe between steps on the thread that steps the runtime". `NodeRuntime` records its stepping thread; `ObservationService` throws `std::logic_error` when called from any other thread after the runtime has stepped. `sample_outputs()` is replaced by `snapshot()`, which returns one `step` for all samples. Code that needs samples elsewhere hands them over itself (Studio's runner uses its mailbox; `RuntimeService` exposes reads under its graph mutex). `observe_smoke` checks the coherent step and the cross-thread refusal.

Relevant code: [observe.cpp](../s7observe/src/observe.cpp:5), [runtime.cpp](../s7core/src/runtime.cpp:9), [runtime.hpp](../s7core/include/s7/runtime.hpp:15).

## P2: Bound Scope sample delivery and recording — fixed 2026-10-04

### Current problem

The local runner emits one queued Qt signal per Scope channel per simulation step. If the runtime produces events faster than Studio processes them, queued UI events can accumulate without bound. The 20,000-point history limit is applied only after Studio receives each event, so it does not limit the pending event queue. Separately, a Scope window appends every sample to its recording vectors for as long as recording remains active, with no size or duration limit.

### Required behavior

Bound the sample path between the runtime worker and UI. Use batched delivery, a bounded queue, or rate reduction with a documented policy; make dropped or aggregated samples visible where that affects interpretation. Keep plotting updates decoupled from simulation tick rate. For explicit recording, define a maximum size/duration or provide a deliberate disk-backed recording mode so long captures do not silently exhaust memory.

### Acceptance criteria

- A runtime that produces samples faster than the UI can render does not create an unbounded event backlog.
- Plot updates remain responsive under sustained high-rate sampling.
- Recording memory use is bounded or the selected storage mode has documented capacity behavior.
- Any decimation/drop policy is clear to the user and preserves timestamp/order information for retained samples.

**Resolution:** the local runner pushes Scope samples into a bounded `ScopeSampleMailbox` (200,000 samples) and emits at most one pending `scopeSamplesAvailable` notification; the UI drains everything queued in one batch. When the UI falls behind, the oldest samples are dropped; retained samples keep their order, step and time. Drops are shown in each Scope window ("display dropped N samples") and logged at most once per second. `ticksProgress` is throttled to 20 per second plus the final count. Recording stops by itself at 2,000,000 samples per channel and says so in the window. Plot refresh stays on its 33 ms timer. Tests: Studio smoke checks the mailbox policy and runs 300,000 steps with no UI draining (exactly 200,000 kept, 100,000 counted, newest last). The interactive GUI path was not exercised by automation.

Relevant code: [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp:124), [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp:191), [main_window.cpp](../s7studio/src/main_window.cpp:1196), [scope_view_window.cpp](../s7studio/src/scope_view_window.cpp:133).

## P3: Give observations explicit time and type semantics — fixed 2026-10-04

### Current problem

`ObservationSample` contains a step index and a real-or-complex value, but no model-time or source/processing timestamp. Studio Scope separately computes its horizontal coordinate as `step * dt`. Scope channels currently accept only `DOUBLE`, while the wider runtime schema advertises additional numeric types. The observation contract therefore does not yet cover the timestamp roles and typed values needed by heterogeneous nodes and real-time streams.

### Required behavior

Define the types and time fields exposed by observation and how they relate to the data-stream timestamp profile. Preserve distinctions between model time and timestamps from source or processing clock domains. Define whether Scope supports only real values or also complex/integer channels, and show unsupported types explicitly rather than silently omitting samples.

**MVP boundary:** observation probes are local to one runtime node. Scope samples outputs in the node where it runs; it does not issue probes to another node or fetch another node's internal state. Data originating remotely MAY be observed after it arrives through an explicitly configured network source/sink connection into the local graph. The network boundary remains visible in the graph and uses the data-stream protocol; observation itself does not bypass that boundary.

### Acceptance criteria

- Each observation has unambiguous value type and timestamp semantics, or explicitly declares which are unavailable.
- Scope displays or clearly reports unsupported signal types.
- Observation probes stay within the local runtime; remotely sourced values are visible only through explicit network elements and their local outputs.

**Resolution:** `ObservationSample` and `ObservationSnapshot` carry `step` and an optional `model_time_ns`, set when the runtime has a model-time base (`NodeRuntime::set_model_time_step_ns`; Studio sets it from the discretization step, `s7runtime` from a `simulation` block, deployed graphs have none). Source and processing timestamps are documented as unavailable to local observation: they belong to data-stream packets. Values are typed by the port's `SignalType`, with `as_real()`, `as_integer()` (integer types only) and `as_complex()`. Scope ports accept `DOUBLE` only; other types are a validation error, and a channel that cannot be read is logged once per run instead of being skipped silently. Scope's time axis now comes from the sample's model time. Observation remains local to one runtime node.

Relevant code: [observe.hpp](../s7observe/include/s7/observe.hpp:18), [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp:197), [local_runtime_v2.hpp](../s7studio/src/local_runtime_v2.hpp:28).

## P2: Use a cryptographically secure prepare token — fixed 2026-10-04

### Current problem

Prepare tokens are generated with `std::random_device` and `std::mt19937_64`. C++ does not guarantee that this combination is cryptographically secure across platforms. The token gates commit and abort operations.

### Required behavior

Generate tokens from an operating-system cryptographic random source or an established cryptographic library, with sufficient entropy for an unguessable bearer token. Scope token validity to the prepared node/deployment/revision and enforce its expiry at commit and abort time.

### Acceptance criteria

- Token generation uses a documented CSPRNG API on every supported platform.
- Expired tokens are rejected based on parsed expiry/state, not merely removed opportunistically.
- Tokens are not returned in peer listings or logs beyond the protocol response that needs them.

**Resolution:** 256-bit tokens from OpenSSL `RAND_bytes`, compared in constant time, scoped to the node's deployment/revision, with a monotonic-clock expiry checked at commit and abort (`PREPARE_EXPIRED`). `LIST_DEPLOYMENTS` no longer returns tokens. Covered by `s7runtime_deployment_smoke`.

Relevant code: [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:86), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:691).

## P3: Align signal types with their portable representation — superseded 2026-10-05

### Current problem

The schema declares multiple fixed-width integer signal types, but `SignalStorage` backs all non-complex types with `double`. Port metadata can therefore claim an integer type while the runtime storage and access paths operate on floating-point values. That ambiguity will hinder heterogeneous runtime implementations and wire compatibility.

### Required behavior

Specify whether integer signal values are stored and computed as their declared integer types or represented internally by a canonical numeric type with explicit range/rounding rules. Ensure ports, element access, serializers, and wire encoders all follow that contract. Do not rely on a language's implicit casts or native object layout.

### Acceptance criteria

- Every declared type has defined width, signedness, range, conversion, overflow, and serialization behavior.
- A value round-trips between C++, Python, Rust, and JVM implementations without changing its meaning.

**Superseded (2026-10-05):** 32/64/128-bit integers and F16/F32/F128 are required, so integers can no longer live in a `double`. The runtime moves to typed storage per port (C++26 on GCC: `std::float16_t`, `std::float128_t`, `__int128`), every type of wire draft section 8 becomes a `SignalType`, and conversions are explicit through one `Convert` element (`from`, `to`, `mode`: value or normalized, `rounding`, `overflow` all explicit). Windows is not an MVP target (a later port would use MinGW-w64). Also decided: complex integer types CI32/CI64/CI128; generic elements implemented as templates with one registry entry each and types inferred from connections (F64 default when undetermined); integer `overflow` per element (`saturate` default, `wrap`); `accumulator_type` on accumulating elements. Full contract: `ELEMENT_DEV_GUIDE.md`, "Type system". **Progress:** the full type catalog with native storage, wire-id enumerators and overflow-aware integer arithmetic is in place (`s7/signal_types.hpp`, `s7/signal_arithmetic.hpp`); ports, runtime staging, observation (`ObservationSample::as<Type>()`), schema JSON names (`F64`, `C64`, `I12`, …) and the s7io file headers use it. The interim double-storage rule is gone. Generic elements with type inference are in place (`s7/type_inference.hpp`: per-element type variables and port rules, solved before validation; F64/C64 default; conflicts reported with element and port names and a Convert hint); `Constant`, `Gain`, `Adder`, `Multiply`, `Delay` and `PreviousValue` are generic with typed parameters (`signal_value_from_json`: exact integers, 128-bit values as decimal strings, complex as `[re, im]`) and an integer `overflow` parameter; all loaders build elements through `make_element_spec()`; the element catalog exposes type variables and rules. All built-in elements are generic (`Integrator` running sum, `FirFilter` and arithmetic elements over arithmetic types; `IirFilter`, the integrators and `AnalogOscillator` over floating types, computed in their own precision; complex split/join over complex types). New `EulerIntegrator` (rectangle rule, causal memory: closes ODE loops without a Delay) and `TrapezoidIntegrator` (direct feedthrough). Remaining: `Convert` (replacing `Int12ToDouble`/`DoubleToInt12`), `accumulator_type`, generic Studio Scope, Studio type labels.

**Resolution:** the canonical-representation option: integer types (all ≤ 16 bits) are stored as a `double` that must hold an exact in-range integer. `signal_type_info()` defines width, signedness, range and candidate wire type id per type; `quantize_to_signal_type()` defines conversion (round half to even, saturate, NaN → 0) and `is_valid_signal_value()` the invariant. `DoubleToInt12` cast to `int16_t` before clamping (undefined for large inputs) and scaled by 2047 while `Int12ToDouble` divided by 2048; both now use Q11 (× / ÷ 2048) with the defined quantization. The table is in `ELEMENT_DEV_GUIDE.md`; `core_smoke` covers ranges, rounding, saturation, NaN and the INT12 round trip. Cross-language round trips need the wire codec (step 3).

Relevant code: [element.hpp](../s7core/include/s7/element.hpp:29), [element.hpp](../s7core/include/s7/element.hpp:82).

## P3: Integrate or clearly isolate the spectral plugin build — fixed 2026-10-05

### Current problem

The top-level CMake configuration does not add `s7spectral`, while the plugin has a separate build configuration that uses `find_package(s7core)`, for which no package configuration exists. A normal project build therefore does not build/install this plugin as part of the repository build.

The plugin also does not compile and is functionally incomplete (verified 2026-10-04 with `g++ -fsyntax-only`):

- `src/plugin_spectral.cpp:60` and `:109`: `ParameterSpec` brace initializers omit the `enum_values` member, so `required`/enum lists bind to the wrong fields and the assignment fails to compile.
- `WindowedFFT` registers an input but no output port, although its metadata advertises a `spectrum` output.
- `MagnitudeSpectrum` and `PowerSpectrumDB` never read their bound input; `input_frame_` stays empty and they register no outputs.
- `SpectralFrame` is not a `SignalType`, so frame-valued ports cannot be typed or validated; the metadata labels them `DOUBLE`.
- Factories call `ctx.get_int("fft_size")` etc. unconditionally, so a schema that omits a parameter throws even though metadata declares defaults.

### Required behavior

Either integrate `s7spectral` as an optional top-level target with its FFTW dependency gated, or explicitly document and automate its standalone build/install path. Keep the default build usable when FFTW is unavailable if the plugin is optional.

### Acceptance criteria

- The supported build instructions reliably produce the advertised plugin, or clearly state that it is an independently built optional component.
- Plugin discovery paths match the chosen install output.

**Resolution (2026-10-05):** the plugin's role is the reference third-party plugin. `s7core` is now a shared library installed with its headers and an exported CMake package (`s7sdk` component, `find_package(s7core)`); `s7spectral` builds out of tree against it, with FFTW found through pkg-config. It compiles, provides a working `WindowedFFT` (dominant frequency and amplitude as scalar outputs; the FFTW plan now runs on FFTW-allocated buffers), and `MagnitudeSpectrum`/`PowerSpectrumDB` were removed until frame types exist. `ctest -R s7spectral_sdk_smoke` installs the SDK, builds the plugin, loads it with `PluginLoader` and checks the measured frequency.

*Earlier (2026-10-04):* isolated rather than integrated: the plugin cannot work until the runtime has block/frame signal types (a `SpectralFrame` port type), so fixing its compile errors would only ship elements without usable ports. The top-level README, AGENTS.md and the plugin README state that it is experimental and deliberately excluded from the build. Integration is future work together with block signal types.

Relevant code: [CMakeLists.txt](../CMakeLists.txt:51), [s7spectral/CMakeLists.txt](../s7spectral/CMakeLists.txt:7), [plugin_spectral.cpp](../s7spectral/src/plugin_spectral.cpp:53), [fft_element.cpp](../s7spectral/src/fft_element.cpp:69).

---

# Additional findings — full code review, 2026-10-04

Items below come from a full read of the code and documentation. "Verified" means the defect was reproduced; the others were found by code reading.

## P1: Port names dangle for Adder, Multiply, and Studio Scope — fixed 2026-10-04

### Current problem

`Port::name` is a `std::string_view`. `AdderT`, `MultiplyT`, and the Studio `ScopeElement` build each port name in a local `std::string` and register a view of it; the string is destroyed when the constructor loop iteration ends. Any later read of those port names is undefined behavior. AddressSanitizer reports `stack-use-after-return` when reading `s7::Adder{}.input_ports()[0].name`.

### Required behavior

Port names must refer to storage that lives as long as the element: either `Port` owns a `std::string`, or the element owns the name strings in stable storage. Remove the duplicated `ScopeElement` in the unused `local_runtime.hpp` while fixing the Studio copy.

### Acceptance criteria

- Adder/Multiply with 2 and N inputs and a multi-channel Scope report correct port names.
- Smoke tests read port names and pass under AddressSanitizer.

**Resolution:** `Port::name` is now a `std::string` owned by the port; `register_input/register_output` copy the name. The unused `local_runtime.hpp` was removed. `core_smoke` checks 3-input Adder and 4-input Multiply names, the Studio smoke checks a 3-channel Scope, and the full test suite passes under AddressSanitizer/UBSan.

Relevant code: [elements.hpp](../s7core/include/s7/elements.hpp:92), [elements.hpp](../s7core/include/s7/elements.hpp:129), [local_runtime_v2.hpp](../s7studio/src/local_runtime_v2.hpp:35), [element.hpp](../s7core/include/s7/element.hpp:45).

## P1: Every stateful element delays its output by one step — fixed 2026-10-04

### Current problem

The runtime equates "has state" with "causal memory" (`has_causal_memory()` returns `has_state_`). All stateful elements are excluded from the topological schedule and stepped after the stateless pass with snapshot-then-commit, so their new outputs become visible downstream only on the next step. Elements with direct feedthrough therefore gain an extra unit delay: with `Constant(1) → FirFilter(taps=[1]) → Gain(1)`, step 1 yields `fir=1, gain=0`. The same applies to `IirFilter`, `Integrator`, `AnalogOscillator`, the noise generators, and every nested `Schema` (which is constructed as stateful), so wrapping elements in a composite changes timing. The causal scheduler also drops every edge that touches a stateful element, so stateful-to-stateful chains have no defined order other than the snapshot.

### Required behavior

Separate "keeps state" from "breaks instantaneous dependency". Only elements without direct feedthrough (e.g. `Delay`, `PreviousValue`, or elements that declare it per input) count as causal memory for cycle validation and snapshot-then-commit. Stateful elements with direct feedthrough are scheduled topologically with stateless elements. A nested schema must not add delay beyond what its contents define. Document the rule in the element contract and the formal model.

### Acceptance criteria

- `Constant → FirFilter([1]) → Gain` produces the constant at step 1.
- `Delay` and `PreviousValue` still delay exactly one step; feedback loops through them remain valid and deterministic.
- A loop closed only through `FirFilter` / `Integrator` without a delay element is rejected, or explicitly allowed only for elements that declare no direct feedthrough.
- A graph and the same graph wrapped in a nested schema produce identical traces.

**Resolution:** `Element` takes a `Feedthrough` flag (default `Direct`); `has_causal_memory()` means `Feedthrough::None`, which only `Delay` and `PreviousValue` declare. `has_state` is informational. A nested `Schema` has direct feedthrough. `PreviousValue` previously delayed by two steps in a graph; it now matches `Delay`. The runtime publishes causal-memory outputs at the start of the next step, so all values read after a step belong to that step. A loop through `Integrator` or `FirFilter` without a `Delay` is rejected; `schema_oscillator_gnuplot.json` now closes its integrator loop through a `Delay` (semi-implicit Euler, bounded amplitude). All four acceptance criteria are covered in `core_smoke`; the formal model, terminology, architecture overview and element guide describe the rule.

Relevant code: [element.hpp](../s7core/include/s7/element.hpp:119), [compiler.cpp](../s7core/src/compiler.cpp:88), [runtime.cpp](../s7core/src/runtime.cpp), [schema.cpp](../s7core/src/schema.cpp:59).

## P1: Node identity uses the certificate CN and a shared state directory — fixed 2026-10-04

### Current problem

When a TLS certificate is configured, `resolve_runtime_instance_uuid()` writes the certificate **common name** into `node_uuid.txt` and uses it as `node_uuid`; with the test certificates the node UUID becomes `s7-runtime-node`, which `control_plane_smoke.cpp` asserts. The design requires the UUID from the SAN URI `spiffe://s7/node/<uuid>`. Without `S7_NODE_STATE_DIR`, every runtime on a host uses `~/.s7runtime/default-node`, so two local nodes share one `node_uuid` and one `peers.json`, and Studio merges them into one node. The control-plane smoke test constructs a `RuntimeService` with the real home directory, so running `ctest` overwrites the developer's node identity file.

### Required behavior

Extract `node_uuid` from the SAN URI and fail startup if the certificate has no valid S7 SAN URI or if it conflicts with configured identity. Require an explicit, distinct state directory per node (command-line option or schema/config field), defaulting only for single-node development. Tests must use temporary state directories.

### Acceptance criteria

- With the test certificate, `node_uuid` equals `11111111-2222-3333-4444-555555555555`.
- Two runtimes started per `LOCAL_HEADLESS_NODES_DEMO.md` report different `node_uuid`s and appear as two nodes in Studio.
- `ctest` does not create or modify files under `$HOME`.

**Resolution:** `node_uuid` comes from the certificate's single `spiffe://s7/node/<uuid>` SAN URI; startup fails if it is missing, invalid, ambiguous, or disagrees with `node_uuid.txt` in the state directory. The common name is no longer used as a node UUID anywhere (including peer records). `RuntimeService` takes the state directory explicitly; `s7runtime --state-dir <dir>` sets it (default `~/.s7runtime/default-node` for single-node development; `S7_NODE_STATE_DIR` was removed). The runtime holds an exclusive `flock` on `<dir>/runtime.lock`, so a second runtime using the same directory fails at startup. `control_plane_smoke` uses temporary directories and checks the certificate UUID, the conflict and lock errors, and distinct persistent UUIDs for two directories; `ctest` with an empty `HOME` leaves it empty.

Relevant code: [runtime_service.cpp](../s7runtime/src/runtime_service.cpp:441), [runtime_service.cpp](../s7runtime/src/runtime_service.cpp:584), [control_plane_smoke.cpp](../s7runtime/tests/control_plane_smoke.cpp:248).

## P2: Headless runtime never executes its graph while serving — fixed 2026-10-04 (configuration-driven)

### Current problem

`s7runtime` compiles the schema but calls `runtime.step()` only for schemas with a `simulation` block, and then only for a precomputed number of steps before serving. In `--serve-forever` / `--serve-seconds` mode the main loop only sleeps, so `NetSink` elements in `schema_minimal.json` never publish and a headless node never processes `NetSource` input. A committed deployment is not executed either (see "Make deployment readiness and commit reflect actual runtime state").

### Required behavior

Define the runtime execution loop for served nodes: continuous stepping (free-running or rate-limited by a configured step rate), start/stop driven by configuration or control-plane commit, and clean shutdown on signal.

### Acceptance criteria

- A served node with `Constant → NetSink` publishes packets that a peer receives.
- Stopping the process stops stepping cleanly; step rate is configurable and observable.

**Resolution:** while serving, `s7runtime` steps the graph on the main thread at `execution.step_rate_hz` from the schema or `--step-rate <hz>` (0 or unset: control plane only). `run_step_loop()` paces steps against a steady clock, drops backlog instead of bursting, and returns on SIGINT/SIGTERM or the `--serve-seconds` deadline; the runtime prints the configured rate, step count and achieved rate. Free-running (unpaced) stepping is not offered. Starting/stopping from a control-plane commit remains part of "Make deployment readiness and commit reflect actual runtime state". Verified: `control_plane_smoke` steps `Constant → NetSink` at 200 Hz and receives every packet; manually, node A at 50 Hz published to node B (`NetSource` → file) over WebSocket and kept running while B disconnected and reconnected.

Relevant code: [main.cpp](../s7runtime/src/main.cpp:157).

## P2: Plugin unload after partial registration leaves dangling factories — partly fixed 2026-10-05

### Current problem

If `s7_register_elements` registers some elements and then throws (for example on a duplicate type name), the loader calls `dlclose()`. The already-registered descriptors keep factory and metadata code that lived in the unloaded library; creating such an element crashes. `s7core` is also a static library linked separately into the host and every plugin, so plugin and host run different copies of registry and element code.

### Required behavior

Make registration transactional: register into a staging registry or remove the plugin's descriptors on failure, or never unload a library after its registration function was called. Link `s7core` as a shared library used by both host and plugins (or otherwise guarantee one copy of registry code).

### Acceptance criteria

- A plugin that fails midway leaves no descriptors from that plugin in the registry and the process keeps running.
- Host and plugins resolve registry/element symbols from one shared `s7core`.

**Progress:** `s7core` is a shared library used by the host and all plugins (second criterion). Transactional registration on failure is still open.

Relevant code: [plugin_loader.cpp](../s7runtime/src/plugin_loader.cpp:308), [s7core/CMakeLists.txt](../s7core/CMakeLists.txt:4), [s7noise/CMakeLists.txt](../s7noise/CMakeLists.txt:23).

## P2: Studio rebuilds the scene from a stale schema and can drop drawn wires

### Current problem

`MainWindow::runtimeSchema_` is updated from the scene only on save, run, or discretization change. Adding an element from the Element Library and editing parameters of `Adder`/`Multiply`/`Scope` serialize the scene but then replace `document.runtimeSchema` with the stale `runtimeSchema_` before reloading. Connections drawn interactively since the last sync are not in that schema, so the reload loses them; wire routes are re-associated by index and may attach to the wrong connections. Found by code reading; not yet reproduced.

### Required behavior

Make the scene (or one model object) the single source of truth: serialize the scene, apply the change to that schema, and reload; or edit the scene incrementally without a full reload. Prefer undo-aware scene commands (see `STUDIO_DIAGRAM_EDITOR.md`).

### Acceptance criteria

- Draw a wire, add an element from the library: the wire remains and the runtime schema contains the connection.
- Draw a wire, edit Adder weights: existing wires remain attached to the right ports.

Relevant code: [main_window.cpp](../s7studio/src/main_window.cpp:937), [main_window.cpp](../s7studio/src/main_window.cpp:1182).

## P2: Trust is inferred from URL scheme and peers are not verified

### Current problem

Studio marks a discovered node `verified` whenever its endpoint starts with `wss://`, without checking that the certificate SAN matches the reported `node_uuid`. The transport TLS client verifies the chain but not the peer identity (no host or SAN check). The WebSocket server binds `0.0.0.0`, so a plain `ws://` node exposes its full control plane, including deployment commands, to the network. Peer records learned from `GET_PEERS` keep the trust state claimed by the remote node.

### Required behavior

Trust state is derived from verified certificate identity: chain valid, SAN `spiffe://s7/node/<uuid>` present, and equal to the claimed `node_uuid`. Peer-reported trust states are claims and are shown as such until verified directly. Plain `ws://` servers bind to loopback by default; binding other interfaces is an explicit option.

### Acceptance criteria

- A `wss://` node whose certificate SAN does not match its claimed UUID is shown as `identity_mismatch`, not trusted.
- A default plain `ws://` runtime is not reachable from another host.

Relevant code: [headless_nodes_panel.cpp](../s7studio/src/headless_nodes_panel.cpp:400), [transport.cpp](../s7transport/src/transport.cpp:520), [transport.cpp](../s7transport/src/transport.cpp:699).

## P2: Transport threads share blocking sockets — found 2026-10-04

### Current problem

`BeastRuntimeNetEndpoint` uses synchronous Beast/Asio calls on one socket from several threads: a reader thread blocks in `read()` while a writer thread (or a control reply) writes, and other threads interrupt reads by closing the socket. Beast and Asio objects are not safe for that use, and for TLS connections OpenSSL forbids concurrent `SSL_read`/`SSL_write` on one `SSL` object. ThreadSanitizer reports data races in `reactive_socket_service_base`, `socket_ops::close` and `websocket::stream` in every networked test; the same races existed before the multi-peer rework (verified on the previous commit).

### Required behavior

Run each endpoint's sockets on an `io_context` with asynchronous reads and writes serialized through one strand per connection (or one I/O thread), and cancel through the executor instead of closing sockets from other threads. Keep the public endpoint API, limits and statistics.

### Acceptance criteria

- The transport, runtime and Studio network tests run clean under ThreadSanitizer.
- mTLS data connections never perform concurrent reads and writes on one SSL stream.

Relevant code: [transport.cpp](../s7transport/src/transport.cpp).

## P3: Element parameters are neither validated nor defaulted

### Current problem

`ElementMetadata::parameters` declares types, ranges, enum values, `required`, and defaults, but the registry does not validate parameters and does not apply defaults. Each factory handles missing values ad hoc; a factory that calls `get_*` unconditionally throws on a schema that omits a parameter with a declared default. Unknown parameter names are silently ignored.

### Required behavior

Validate params against metadata when an element is created from a descriptor (type, range, enum, required, unknown names) and pass defaulted params to the factory.

### Acceptance criteria

- Wrong-typed, out-of-range, missing-required, and unknown parameters produce explicit validation errors naming element and parameter.
- Omitted optional parameters receive their declared defaults.

Relevant code: [element_registry.cpp](../s7core/src/element_registry.cpp:93), [builtin_element_registry.cpp](../s7core/src/builtin_element_registry.cpp).

## P3: Validation and compilation instantiate elements repeatedly

### Current problem

The validator creates every element once for the prototype catalog and twice more for each connection (port type check); the compiler then creates them again. Element constructors run several times per compile: file sinks open and truncate their files each time, nested schemas are rebuilt recursively, and future hardware or network elements would acquire resources repeatedly.

### Required behavior

Instantiate each element once per compilation and reuse the instance (or derive port information from metadata) for validation, port-type checks, and binding.

### Acceptance criteria

- Each element factory is invoked exactly once per `Compiler::compile`.

Relevant code: [validation.cpp](../s7core/src/validation.cpp:222), [compiler.cpp](../s7core/src/compiler.cpp:144).

## P3: Smaller correctness issues

- `NetSink` sets `data_type = 0x000C`, which the prototype codec treats as F64 but the wire draft assigns to `U128` (F64 is `0x000F`). Align when the draft codec is implemented. [net.cpp](../s7net/src/net.cpp:136)
- `PinkNoiseGenerator` is described as Voss-McCartney but outputs the mean of the last 7 white samples (low-pass noise, not 1/f). [noise_elements.cpp](../s7noise/src/noise_elements.cpp:53)
- `s7studio/src/local_runtime.hpp` is an unused duplicate of `local_runtime_v2.hpp`; `examples/*.cpp` are not built; `s7ctl` is a placeholder. Remove or wire into the build.
- `ControlPlaneMvp::handle_abort_deployment` reads `active_deployment_id_` after releasing the mutex. [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:747)
- The WebSocket server performs TLS and HTTP handshakes on the accept thread; one slow client blocks all new connections and can block shutdown. [transport.cpp](../s7transport/src/transport.cpp:629)

## Suggested order

0. ~~Fix the verified 2026-10-04 defects: dangling port names, extra delay from stateful elements, certificate-bound node identity and per-node state directories; then the headless execution loop.~~ Done 2026-10-04.
1. ~~Add tests for snapshot-then-commit stateful behavior, model time zero, malformed frame rejection, and receive failure handling.~~ Done 2026-10-04.
2. ~~Enforce control authorization against the authenticated TLS peer and clarify deployment prepare/commit behavior.~~ Done 2026-10-04 (also the prepare-token item).
3. Resolve candidate wire-format questions, approve the normative contract, then implement and test its codec before writing other-language clients. *Contract approved 2026-10-05 (wire draft 0.5); implementation pending.*
4. ~~Implement multiple-peer routing, bounded per-peer queues/backpressure, and deployment success semantics.~~ Done 2026-10-04, except routing by stream descriptor (needs step 3 approval). Follow-up: make the transport's socket threading safe (new P2 finding above).
5. ~~Define safe observation snapshots and bound Scope delivery/recording before using it with high-rate runtimes.~~ Done 2026-10-04.
6. ~~Address signal representation, observation timestamps/types, and plugin build integration.~~ Done 2026-10-04 (spectral plugin isolated, not integrated).

This order addresses immediate correctness and trust boundaries first, then stabilizes contracts needed by heterogeneous nodes.
