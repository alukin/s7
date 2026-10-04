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
- **Wire format remains under review:** [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md) now describes candidate draft 0.3, explicitly not a conformance standard. Decisions recorded: every listed numeric type and all timestamp roles are mandatory capabilities; each stream declares the roles carried per packet; both ends persist sequence state; non-model clock domains are configured and must change when continuity across restart is not guaranteed. The exact crash delivery guarantee, descriptor serialization, fault messages, and conformance fixtures remain open. The candidate still does not match the prototype codec.
- **Control draft updated:** canonical JSON digesting uses RFC 8785 JCS plus SHA-256; configuration bundles have bounded direct/chunked transfer; one CA-policy-authorized Studio is configured; partial commits are reconciled through status queries and idempotent retries. Exact message schemas, diagram/schema representation, retention, and request replay cache policy remain open.
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

Relevant code: [transport.cpp](../s7transport/src/transport.cpp:256), [transport.cpp](../s7transport/src/transport.cpp:556).

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

Relevant code: [runtime.cpp](../s7core/src/runtime.cpp:9), [compiler.cpp](../s7core/src/compiler.cpp:160), [elements.hpp](../s7core/include/s7/elements.hpp:161).

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

Relevant code: [net.cpp](../s7net/src/net.cpp:133), [transport.cpp](../s7transport/src/transport.cpp:224).

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

Relevant code: [transport.cpp](../s7transport/src/transport.cpp:500), [transport.cpp](../s7transport/src/transport.cpp:565), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:200).

## P2: Approve a normative stream identity and packet layout before claiming conformance

### Current problem

Candidate draft 0.2 specifies a 128-bit stream UUID and an `S7DP` magic/version header. The current prototype codec uses a 64-bit `stream_id` and a different header layout. Implementations following the candidate cannot safely interoperate with the prototype. The draft has open review items and is not yet the approved MVP contract.

### Required behavior

Review and approve one versioned packet format, then keep the implementation, public types, examples, and wire-protocol document in sync. Do not silently reinterpret candidate fields or call the prototype conformant. Current candidate decisions require 128-bit stream identity, mandatory support for every listed numeric type and timestamp role, per-stream required timestamp roles, durable sender and receiver sequence state, and explicit clock domains. Crash delivery semantics, descriptor serialization, fault messages, and negotiation details still need resolution before codec implementation.

Also ensure that timestamp roles and their presence are encoded unambiguously; the draft distinguishes model, source, and processing time while the current packet representation has a single model timestamp and optional system timestamp.

### Acceptance criteria

- Independent implementations can encode/decode the same normative packet examples byte-for-byte.
- Protocol versioning rejects incompatible layouts explicitly.
- Stream identity remains stable and unique for the configured stream lifetime.

Relevant code: [transport.hpp](../s7transport/include/s7/transport.hpp:1), [transport.cpp](../s7transport/src/transport.cpp:210), [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md:1).

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

Relevant code: [transport.cpp](../s7transport/src/transport.cpp:523), [transport.cpp](../s7transport/src/transport.cpp:587).

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

Relevant code: [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:519), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:552), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:675).

## P2: Define a thread-safe consistency model for observation

### Current problem

`ObservationService` reads a runtime's current output values and step count directly. It has no synchronization with `NodeRuntime::step()`. Studio's current local runner samples on the same worker thread immediately after stepping, which avoids concurrent access in that path, but the public API does not state or enforce that restriction. Concurrent sampling can race with output mutation; a batch may also contain samples from different runtime steps.

### Required behavior

Define how observation obtains a consistent sample while execution is active. Options include restricting sampling to the runtime execution thread, providing an explicit synchronized snapshot operation, or publishing immutable snapshots at step boundaries. The API contract must say whether a batch represents one common step. Avoid presenting unsynchronized reads as safe merely because the observation methods are `const`.

### Acceptance criteria

- The API documents and enforces its threading contract.
- A multi-probe snapshot has a coherent step identity, or explicitly reports per-sample step identities when coherence is not guaranteed.
- Sampling cannot data-race with element output writes or `step_count_` updates.

Relevant code: [observe.cpp](../s7observe/src/observe.cpp:5), [runtime.cpp](../s7core/src/runtime.cpp:9), [runtime.hpp](../s7core/include/s7/runtime.hpp:15).

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

Relevant code: [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp:124), [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp:191), [main_window.cpp](../s7studio/src/main_window.cpp:1196), [scope_view_window.cpp](../s7studio/src/scope_view_window.cpp:133).

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

Relevant code: [observe.hpp](../s7observe/include/s7/observe.hpp:18), [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp:197), [local_runtime_v2.hpp](../s7studio/src/local_runtime_v2.hpp:28).

## P2: Use a cryptographically secure prepare token

### Current problem

Prepare tokens are generated with `std::random_device` and `std::mt19937_64`. C++ does not guarantee that this combination is cryptographically secure across platforms. The token gates commit and abort operations.

### Required behavior

Generate tokens from an operating-system cryptographic random source or an established cryptographic library, with sufficient entropy for an unguessable bearer token. Scope token validity to the prepared node/deployment/revision and enforce its expiry at commit and abort time.

### Acceptance criteria

- Token generation uses a documented CSPRNG API on every supported platform.
- Expired tokens are rejected based on parsed expiry/state, not merely removed opportunistically.
- Tokens are not returned in peer listings or logs beyond the protocol response that needs them.

Relevant code: [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:86), [control_plane_mvp.cpp](../s7runtime/src/control_plane_mvp.cpp:691).

## P3: Align signal types with their portable representation

### Current problem

The schema declares multiple fixed-width integer signal types, but `SignalStorage` backs all non-complex types with `double`. Port metadata can therefore claim an integer type while the runtime storage and access paths operate on floating-point values. That ambiguity will hinder heterogeneous runtime implementations and wire compatibility.

### Required behavior

Specify whether integer signal values are stored and computed as their declared integer types or represented internally by a canonical numeric type with explicit range/rounding rules. Ensure ports, element access, serializers, and wire encoders all follow that contract. Do not rely on a language's implicit casts or native object layout.

### Acceptance criteria

- Every declared type has defined width, signedness, range, conversion, overflow, and serialization behavior.
- A value round-trips between C++, Python, Rust, and JVM implementations without changing its meaning.

Relevant code: [element.hpp](../s7core/include/s7/element.hpp:29), [element.hpp](../s7core/include/s7/element.hpp:82).

## P3: Integrate or clearly isolate the spectral plugin build

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

Relevant code: [CMakeLists.txt](../CMakeLists.txt:51), [s7spectral/CMakeLists.txt](../s7spectral/CMakeLists.txt:7), [plugin_spectral.cpp](../s7spectral/src/plugin_spectral.cpp:53), [fft_element.cpp](../s7spectral/src/fft_element.cpp:69).

---

# Additional findings — full code review, 2026-10-04

Items below come from a full read of the code and documentation. "Verified" means the defect was reproduced; the others were found by code reading.

## P1: Port names dangle for Adder, Multiply, and Studio Scope — verified

### Current problem

`Port::name` is a `std::string_view`. `AdderT`, `MultiplyT`, and the Studio `ScopeElement` build each port name in a local `std::string` and register a view of it; the string is destroyed when the constructor loop iteration ends. Any later read of those port names is undefined behavior. AddressSanitizer reports `stack-use-after-return` when reading `s7::Adder{}.input_ports()[0].name`.

### Required behavior

Port names must refer to storage that lives as long as the element: either `Port` owns a `std::string`, or the element owns the name strings in stable storage. Remove the duplicated `ScopeElement` in the unused `local_runtime.hpp` while fixing the Studio copy.

### Acceptance criteria

- Adder/Multiply with 2 and N inputs and a multi-channel Scope report correct port names.
- Smoke tests read port names and pass under AddressSanitizer.

Relevant code: [elements.hpp](../s7core/include/s7/elements.hpp:92), [elements.hpp](../s7core/include/s7/elements.hpp:129), [local_runtime_v2.hpp](../s7studio/src/local_runtime_v2.hpp:35), [element.hpp](../s7core/include/s7/element.hpp:45).

## P1: Every stateful element delays its output by one step — verified

### Current problem

The runtime equates "has state" with "causal memory" (`has_causal_memory()` returns `has_state_`). All stateful elements are excluded from the topological schedule and stepped after the stateless pass with snapshot-then-commit, so their new outputs become visible downstream only on the next step. Elements with direct feedthrough therefore gain an extra unit delay: with `Constant(1) → FirFilter(taps=[1]) → Gain(1)`, step 1 yields `fir=1, gain=0`. The same applies to `IirFilter`, `Integrator`, `AnalogOscillator`, the noise generators, and every nested `Schema` (which is constructed as stateful), so wrapping elements in a composite changes timing. The causal scheduler also drops every edge that touches a stateful element, so stateful-to-stateful chains have no defined order other than the snapshot.

### Required behavior

Separate "keeps state" from "breaks instantaneous dependency". Only elements without direct feedthrough (e.g. `Delay`, `PreviousValue`, or elements that declare it per input) count as causal memory for cycle validation and snapshot-then-commit. Stateful elements with direct feedthrough are scheduled topologically with stateless elements. A nested schema must not add delay beyond what its contents define. Document the rule in the element contract and the formal model.

### Acceptance criteria

- `Constant → FirFilter([1]) → Gain` produces the constant at step 1.
- `Delay` and `PreviousValue` still delay exactly one step; feedback loops through them remain valid and deterministic.
- A loop closed only through `FirFilter` / `Integrator` without a delay element is rejected, or explicitly allowed only for elements that declare no direct feedthrough.
- A graph and the same graph wrapped in a nested schema produce identical traces.

Relevant code: [element.hpp](../s7core/include/s7/element.hpp:119), [compiler.cpp](../s7core/src/compiler.cpp:88), [runtime.cpp](../s7core/src/runtime.cpp), [schema.cpp](../s7core/src/schema.cpp:59).

## P1: Node identity uses the certificate CN and a shared state directory — verified

### Current problem

When a TLS certificate is configured, `resolve_runtime_instance_uuid()` writes the certificate **common name** into `node_uuid.txt` and uses it as `node_uuid`; with the test certificates the node UUID becomes `s7-runtime-node`, which `control_plane_smoke.cpp` asserts. The design requires the UUID from the SAN URI `spiffe://s7/node/<uuid>`. Without `S7_NODE_STATE_DIR`, every runtime on a host uses `~/.s7runtime/default-node`, so two local nodes share one `node_uuid` and one `peers.json`, and Studio merges them into one node. The control-plane smoke test constructs a `RuntimeService` with the real home directory, so running `ctest` overwrites the developer's node identity file.

### Required behavior

Extract `node_uuid` from the SAN URI and fail startup if the certificate has no valid S7 SAN URI or if it conflicts with configured identity. Require an explicit, distinct state directory per node (command-line option or schema/config field), defaulting only for single-node development. Tests must use temporary state directories.

### Acceptance criteria

- With the test certificate, `node_uuid` equals `11111111-2222-3333-4444-555555555555`.
- Two runtimes started per `LOCAL_HEADLESS_NODES_DEMO.md` report different `node_uuid`s and appear as two nodes in Studio.
- `ctest` does not create or modify files under `$HOME`.

Relevant code: [runtime_service.cpp](../s7runtime/src/runtime_service.cpp:441), [runtime_service.cpp](../s7runtime/src/runtime_service.cpp:584), [control_plane_smoke.cpp](../s7runtime/tests/control_plane_smoke.cpp:248).

## P2: Headless runtime never executes its graph while serving

### Current problem

`s7runtime` compiles the schema but calls `runtime.step()` only for schemas with a `simulation` block, and then only for a precomputed number of steps before serving. In `--serve-forever` / `--serve-seconds` mode the main loop only sleeps, so `NetSink` elements in `schema_minimal.json` never publish and a headless node never processes `NetSource` input. A committed deployment is not executed either (see "Make deployment readiness and commit reflect actual runtime state").

### Required behavior

Define the runtime execution loop for served nodes: continuous stepping (free-running or rate-limited by a configured step rate), start/stop driven by configuration or control-plane commit, and clean shutdown on signal.

### Acceptance criteria

- A served node with `Constant → NetSink` publishes packets that a peer receives.
- Stopping the process stops stepping cleanly; step rate is configurable and observable.

Relevant code: [main.cpp](../s7runtime/src/main.cpp:157).

## P2: Plugin unload after partial registration leaves dangling factories

### Current problem

If `s7_register_elements` registers some elements and then throws (for example on a duplicate type name), the loader calls `dlclose()`. The already-registered descriptors keep factory and metadata code that lived in the unloaded library; creating such an element crashes. `s7core` is also a static library linked separately into the host and every plugin, so plugin and host run different copies of registry and element code.

### Required behavior

Make registration transactional: register into a staging registry or remove the plugin's descriptors on failure, or never unload a library after its registration function was called. Link `s7core` as a shared library used by both host and plugins (or otherwise guarantee one copy of registry code).

### Acceptance criteria

- A plugin that fails midway leaves no descriptors from that plugin in the registry and the process keeps running.
- Host and plugins resolve registry/element symbols from one shared `s7core`.

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

0. Fix the verified 2026-10-04 defects: dangling port names, extra delay from stateful elements, certificate-bound node identity and per-node state directories; then the headless execution loop.
1. Add tests for snapshot-then-commit stateful behavior, model time zero, malformed frame rejection, and receive failure handling.
2. Enforce control authorization against the authenticated TLS peer and clarify deployment prepare/commit behavior.
3. Resolve candidate wire-format questions, approve the normative contract, then implement and test its codec before writing other-language clients.
4. Implement multiple-peer routing, bounded per-peer queues/backpressure, and deployment success semantics.
5. Define safe observation snapshots and bound Scope delivery/recording before using it with high-rate runtimes.
6. Address token generation, signal representation, observation timestamps/types, and plugin build integration.

This order addresses immediate correctness and trust boundaries first, then stabilizes contracts needed by heterogeneous nodes.
