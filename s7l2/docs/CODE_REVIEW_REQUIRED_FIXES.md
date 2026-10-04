# Code Review: Required Fixes

**Status:** Working fix checklist for the S7 MVP; implementation is in progress.
**Scope:** Findings from the read-only review of the current C++ implementation. This document describes required behavior and acceptance criteria; it is not a claim that the current runtime conforms to the protocol drafts.

The project is a work in progress. These items are prioritized by their effect on correctness, availability, security, and the ability to build interoperable runtime nodes in different languages.

## Priority guide

- **P1 — Required before relying on the feature:** correctness, crash, or authorization defect in an exercised path.
- **P2 — Required before multi-node MVP use:** behavior or integration that prevents the documented distributed use case.
- **P3 — Follow-up hardening:** valuable robustness work that is less likely to invalidate basic MVP use.

## Progress

- **Implemented, build-checked:** stateful element outputs are staged and published after the stateful pass, preserving the existing `step()` interface. The element contract now describes snapshot-then-commit output semantics. The `s7core` target compiles. Tests were not run.
- **Implemented, build-checked:** the transport prototype accepts model time zero, rejects trailing frame bytes and unsupported flags, validates timestamp presence, and converts decoder exceptions into reported endpoint errors rather than allowing them to escape reader threads. Its codec API is named `*_prototype` to distinguish it from the candidate wire format. The `s7transport`, `s7net`, `s7runtime`, and `s7studio` targets compile. Tests were not run.
- **Wire format remains under review:** [S7_Network_Wire_Protocol_v1.md](/home/al/projects/s7/s7l2/docs/S7_Network_Wire_Protocol_v1.md) now describes candidate draft 0.2, explicitly not a conformance standard. It still does not match the prototype codec. Resolve the open design questions and approve a normative format before implementing a conformant codec.
- **Pending:** the transport endpoint still has singleton active peer sockets; multiple-peer routing, per-peer queueing, and backpressure behavior are not implemented.
- The review also confirmed that observation is local to one runtime node. Remote-origin values can be observed after entering the local graph through explicit network source/sink elements; observation does not bypass network boundaries. Scope queue and recording bounds remain open work.

## P1: Fix data packet validation and receive failure handling — partially implemented

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

**Progress:** exact frame length, supported flags, timestamp/flag consistency, and reader-thread exception handling are implemented in the prototype. The close-on-malformed-frame policy is to report the error and interrupt that connection's reader. Broader field validation, protocol-version behavior, and acceptance tests remain to be completed.

Relevant code: [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:256), [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:556).

## P1: Make stateful graph updates independent of declaration order — implementation in place; tests pending

### Current problem

`NodeRuntime::step()` runs stateless nodes and then steps stateful nodes sequentially. The compiler builds the stateful list in schema declaration order. A stateful element later in that list can therefore read an earlier stateful element's newly updated output in the same tick. Reordering otherwise identical schema elements may change results, and chains of delay/memory elements may not preserve the intended one-step causal behavior.

### Required behavior

Define and implement explicit stateful update semantics. All stateful elements participating in a runtime step MUST observe the same logically defined pre-update state where the element contract requires previous-step values. Updating/committing state MUST NOT expose partial same-step updates based only on declaration order.

The chosen lifecycle (for example, read/compute followed by commit) must be documented for element authors and applied consistently to built-in and plugin elements. This fix must preserve the existing rule that cycles require causal-memory elements.

### Acceptance criteria

- A stateful chain produces the same trace when element declarations are reordered.
- A delay chain advances by the documented number of steps.
- Feedback loops with memory remain valid and deterministic.

**Progress:** runtime outputs are snapshotted before the stateful pass, each stateful element computes against pre-pass outputs, and staged outputs are published after the pass. This implements the selected snapshot-then-commit behavior for exposed outputs. Build compilation passed; declaration-order, delay-chain, and feedback behavior still needs test coverage. Element-private state advances during its own `step()` call; it must not serve as same-pass cross-element communication.

Relevant code: [runtime.cpp](/home/al/projects/s7/s7l2/s7core/src/runtime.cpp:9), [compiler.cpp](/home/al/projects/s7/s7l2/s7core/src/compiler.cpp:160), [elements.hpp](/home/al/projects/s7/s7l2/s7core/include/s7/elements.hpp:161).

## P1: Permit zero model time — implemented; tests pending

### Current problem

The packet encoder rejects packets whose model timestamp is zero. `NetSink` assigns `t_model` from its sequence counter, which begins at zero, so its initial packet cannot be encoded. Zero is also a valid model-time origin.

### Required behavior

Timestamp presence MUST be represented separately from timestamp value. A present model timestamp of zero is valid. If a stream profile requires model time, reject only a missing timestamp, not the value zero.

### Acceptance criteria

- A packet at model time zero encodes, decodes, and round-trips with the timestamp intact.
- A packet without a required timestamp is rejected explicitly.
- The runtime's first packet on a newly created stream can be sent.

**Progress:** timestamp presence is represented separately from its value in the prototype packet API, so zero is valid. Round-trip and missing-required-timestamp tests remain pending.

Relevant code: [net.cpp](/home/al/projects/s7/s7l2/s7net/src/net.cpp:133), [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:224).

## P1: Enforce control authorization using the authenticated peer

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

Relevant code: [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:500), [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:565), [control_plane_mvp.cpp](/home/al/projects/s7/s7l2/s7runtime/src/control_plane_mvp.cpp:200).

## P2: Approve a normative stream identity and packet layout before claiming conformance

### Current problem

Candidate draft 0.2 specifies a 128-bit stream UUID and an `S7DP` magic/version header. The current prototype codec uses a 64-bit `stream_id` and a different header layout. Implementations following the candidate cannot safely interoperate with the prototype. The draft has open review items and is not yet the approved MVP contract.

### Required behavior

Review and approve one versioned packet format, then keep the implementation, public types, examples, and wire-protocol document in sync. Do not silently reinterpret candidate fields or call the prototype conformant. The current candidate's 128-bit identity, timestamp roles, numeric type table, receiver rejoin behavior, clock-domain mapping, and negotiation/error behavior need resolution before codec implementation.

Also ensure that timestamp roles and their presence are encoded unambiguously; the draft distinguishes model, source, and processing time while the current packet representation has a single model timestamp and optional system timestamp.

### Acceptance criteria

- Independent implementations can encode/decode the same normative packet examples byte-for-byte.
- Protocol versioning rejects incompatible layouts explicitly.
- Stream identity remains stable and unique for the configured stream lifetime.

Relevant code: [transport.hpp](/home/al/projects/s7/s7l2/s7transport/include/s7/transport.hpp:1), [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:210), [S7_Network_Wire_Protocol_v1.md](/home/al/projects/s7/s7l2/docs/S7_Network_Wire_Protocol_v1.md:1).

## P2: Implement multiple-peer endpoint behavior — pending

### Current problem

The WebSocket server stores only one active data socket and one control socket. A later peer replaces the previous connection. That does not provide a general multi-peer endpoint for a distributed network.

### Required behavior

Support multiple simultaneous peers with explicit routing by authenticated peer and stream, as selected for the MVP topology. Do not silently disconnect an existing peer when another connects.

Define bounded queue/backpressure behavior as part of endpoint operation. A producer that outruns a receiver must not cause unbounded memory growth; report overload or apply a documented bounded policy.

### Acceptance criteria

- Connecting a second permitted peer does not unexpectedly replace the first.
- Packets are delivered only to the intended stream/peer endpoints.
- Queue limits and overload behavior are explicit and observable.

Relevant code: [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:523), [transport.cpp](/home/al/projects/s7/s7l2/s7transport/src/transport.cpp:587).

## P2: Make deployment readiness and commit reflect actual runtime state

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

Relevant code: [control_plane_mvp.cpp](/home/al/projects/s7/s7l2/s7runtime/src/control_plane_mvp.cpp:519), [control_plane_mvp.cpp](/home/al/projects/s7/s7l2/s7runtime/src/control_plane_mvp.cpp:552), [control_plane_mvp.cpp](/home/al/projects/s7/s7l2/s7runtime/src/control_plane_mvp.cpp:675).

## P2: Define a thread-safe consistency model for observation

### Current problem

`ObservationService` reads a runtime's current output values and step count directly. It has no synchronization with `NodeRuntime::step()`. Studio's current local runner samples on the same worker thread immediately after stepping, which avoids concurrent access in that path, but the public API does not state or enforce that restriction. Concurrent sampling can race with output mutation; a batch may also contain samples from different runtime steps.

### Required behavior

Define how observation obtains a consistent sample while execution is active. Options include restricting sampling to the runtime execution thread, providing an explicit synchronized snapshot operation, or publishing immutable snapshots at step boundaries. The API contract must say whether a batch represents one common step. Avoid presenting unsynchronized reads as safe merely because the observation methods are `const`.

### Acceptance criteria

- The API documents and enforces its threading contract.
- A multi-probe snapshot has a coherent step identity, or explicitly reports per-sample step identities when coherence is not guaranteed.
- Sampling cannot data-race with element output writes or `step_count_` updates.

Relevant code: [observe.cpp](/home/al/projects/s7/s7l2/s7observe/src/observe.cpp:5), [runtime.cpp](/home/al/projects/s7/s7l2/s7core/src/runtime.cpp:9), [runtime.hpp](/home/al/projects/s7/s7l2/s7core/include/s7/runtime.hpp:15).

## P2: Bound Scope sample delivery and recording

### Current problem

The local runner emits one queued Qt signal per Scope channel per simulation step. If the runtime produces events faster than Studio processes them, queued UI events can accumulate without bound. The 20,000-point history limit is applied only after Studio receives each event, so it does not limit the pending event queue. Separately, a Scope window appends every sample to its recording vectors for as long as recording remains active, with no size or duration limit.

### Required behavior

Bound the sample path between the runtime worker and UI. Use batched delivery, a bounded queue, or rate reduction with a documented policy; make dropped or aggregated samples visible where that affects interpretation. Keep plotting updates decoupled from simulation tick rate. For explicit recording, define a maximum size/duration or provide a deliberate disk-backed recording mode so long captures do not silently exhaust memory.

### Acceptance criteria

- A runtime that produces samples faster than the UI can render does not create an unbounded event backlog.
- Plot updates remain responsive under sustained high-rate sampling.
- Recording memory use is bounded or the selected storage mode has documented capacity behavior.
- Any decimation/drop policy is clear to the user and preserves timestamp/order information for retained samples.

Relevant code: [local_schema_runner.cpp](/home/al/projects/s7/s7l2/s7studio/src/local_schema_runner.cpp:124), [local_schema_runner.cpp](/home/al/projects/s7/s7l2/s7studio/src/local_schema_runner.cpp:191), [main_window.cpp](/home/al/projects/s7/s7l2/s7studio/src/main_window.cpp:1196), [scope_view_window.cpp](/home/al/projects/s7/s7l2/s7studio/src/scope_view_window.cpp:133).

## P3: Give observations explicit time and type semantics

### Current problem

`ObservationSample` contains a step index and a real-or-complex value, but no model-time or source/processing timestamp. Studio Scope separately computes its horizontal coordinate as `step * dt`. Scope channels currently accept only `DOUBLE`, while the wider runtime schema advertises additional numeric types. The observation contract therefore does not yet cover the timestamp roles and typed values needed by heterogeneous nodes and real-time streams.

### Required behavior

Define the types and time fields exposed by observation and how they relate to the data-stream timestamp profile. Preserve distinctions between model time and timestamps from source or processing clock domains. Define whether Scope supports only real values or also complex/integer channels, and show unsupported types explicitly rather than silently omitting samples.

**MVP boundary:** observation probes are local to one runtime node. Scope samples outputs in the node where it runs; it does not issue probes to another node or fetch another node's internal state. Data originating remotely MAY be observed after it arrives through an explicitly configured network source/sink connection into the local graph. The network boundary remains visible in the graph and uses the data-stream protocol; observation itself does not bypass that boundary.

### Acceptance criteria

- Each observation has unambiguous value type and timestamp semantics, or explicitly declares which are unavailable.
- Scope displays or clearly reports unsupported signal types.
- Observation probes stay within the local runtime; remotely sourced values are visible only through explicit network elements and their local outputs.

Relevant code: [observe.hpp](/home/al/projects/s7/s7l2/s7observe/include/s7/observe.hpp:18), [local_schema_runner.cpp](/home/al/projects/s7/s7l2/s7studio/src/local_schema_runner.cpp:197), [local_runtime_v2.hpp](/home/al/projects/s7/s7l2/s7studio/src/local_runtime_v2.hpp:28).

## P2: Use a cryptographically secure prepare token

### Current problem

Prepare tokens are generated with `std::random_device` and `std::mt19937_64`. C++ does not guarantee that this combination is cryptographically secure across platforms. The token gates commit and abort operations.

### Required behavior

Generate tokens from an operating-system cryptographic random source or an established cryptographic library, with sufficient entropy for an unguessable bearer token. Scope token validity to the prepared node/deployment/revision and enforce its expiry at commit and abort time.

### Acceptance criteria

- Token generation uses a documented CSPRNG API on every supported platform.
- Expired tokens are rejected based on parsed expiry/state, not merely removed opportunistically.
- Tokens are not returned in peer listings or logs beyond the protocol response that needs them.

Relevant code: [control_plane_mvp.cpp](/home/al/projects/s7/s7l2/s7runtime/src/control_plane_mvp.cpp:86), [control_plane_mvp.cpp](/home/al/projects/s7/s7l2/s7runtime/src/control_plane_mvp.cpp:691).

## P3: Align signal types with their portable representation

### Current problem

The schema declares multiple fixed-width integer signal types, but `SignalStorage` backs all non-complex types with `double`. Port metadata can therefore claim an integer type while the runtime storage and access paths operate on floating-point values. That ambiguity will hinder heterogeneous runtime implementations and wire compatibility.

### Required behavior

Specify whether integer signal values are stored and computed as their declared integer types or represented internally by a canonical numeric type with explicit range/rounding rules. Ensure ports, element access, serializers, and wire encoders all follow that contract. Do not rely on a language's implicit casts or native object layout.

### Acceptance criteria

- Every declared type has defined width, signedness, range, conversion, overflow, and serialization behavior.
- A value round-trips between C++, Python, Rust, and JVM implementations without changing its meaning.

Relevant code: [element.hpp](/home/al/projects/s7/s7l2/s7core/include/s7/element.hpp:29), [element.hpp](/home/al/projects/s7/s7l2/s7core/include/s7/element.hpp:82).

## P3: Integrate or clearly isolate the spectral plugin build

### Current problem

The top-level CMake configuration does not add `s7spectral`, while the plugin has a separate build configuration that uses `find_package(s7core)`. A normal project build therefore does not build/install this plugin as part of the repository build.

### Required behavior

Either integrate `s7spectral` as an optional top-level target with its FFTW dependency gated, or explicitly document and automate its standalone build/install path. Keep the default build usable when FFTW is unavailable if the plugin is optional.

### Acceptance criteria

- The supported build instructions reliably produce the advertised plugin, or clearly state that it is an independently built optional component.
- Plugin discovery paths match the chosen install output.

Relevant code: [CMakeLists.txt](/home/al/projects/s7/s7l2/CMakeLists.txt:51), [s7spectral/CMakeLists.txt](/home/al/projects/s7/s7l2/s7spectral/CMakeLists.txt:7).

## Suggested order

1. Add tests for snapshot-then-commit stateful behavior, model time zero, malformed frame rejection, and receive failure handling.
2. Enforce control authorization against the authenticated TLS peer and clarify deployment prepare/commit behavior.
3. Resolve candidate wire-format questions, approve the normative contract, then implement and test its codec before writing other-language clients.
4. Implement multiple-peer routing, bounded per-peer queues/backpressure, and deployment success semantics.
5. Define safe observation snapshots and bound Scope delivery/recording before using it with high-rate runtimes.
6. Address token generation, signal representation, observation timestamps/types, and plugin build integration.

This order addresses immediate correctness and trust boundaries first, then stabilizes contracts needed by heterogeneous nodes.
