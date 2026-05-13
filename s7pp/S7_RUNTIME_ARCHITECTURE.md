# S7 Runtime Architecture Notes

This note captures the current direction for growing S7 from an in-process modeling library into a distributed runtime with a separate GUI, future live I/O, and pluggable compute nodes.

The goal of this document is architectural clarity, not immediate implementation.

## 1.1 Decisions fixed on 2026-05-10

The following items are now considered chosen baseline direction unless a later design note explicitly supersedes them:

- `v1` scope includes all three execution shapes:
  - separate runtime and GUI,
  - local worker processes,
  - remote computation nodes,
- orchestration uses one authoritative centralized GUI controller,
- the execution model is hybrid rather than scalar-only,
- the plugin ABI starts as experimental/internal first,
- placement is manual in the initial implementation,
- default failure handling should prefer `degraded_warning` with `transient_recoverable` retry/reconnect behavior when safe and meaningful,
- strict reproducibility is expected for deterministic sources such as file-backed inputs,
- live sources are not expected to guarantee full determinism,
- graph/session, deployment, and GUI workspace state should remain separate artifacts,
- trust should be based on X.509 certificates,
- element type IDs should follow a lower-case dot-separated format,
- every computation node must expose a control-plane server endpoint so the GUI can connect directly, inspect capabilities, and control that node.

## 1. Goals

S7 should grow toward a system with:

- a headless runtime executable for graph execution,
- a separate GUI executable for schema editing and visualization,
- optional worker executables for heavy operations such as FFT or GPU processing,
- support for local and remote execution,
- future live sources/sinks such as audio, video, SDR, file replay, and file recording,
- a plugin system for loading element libraries from shared libraries,
- runtime-side discovery so the GUI can know which elements are available on each computation node.

`v1` is expected to cover local and remote execution, but some subsystems may begin as simpler first implementations while preserving the chosen architecture.

## 2. Main Process Roles

## 2.1 `s7-runtime`

Responsibilities:

- own the execution graph and scheduling,
- load built-in elements and external plugin libraries,
- expose control-plane interfaces,
- choose transport for local or remote data edges,
- manage worker processes and remote peers,
- expose a node-local control server endpoint,
- publish host capabilities and available element catalog.

## 2.2 `s7-gui`

Responsibilities:

- graph editing,
- parameter editing,
- deployment view,
- oscilloscope / spectrum / waterfall visualization,
- querying one or more runtimes for available elements and host capabilities.

The current architecture assumes one authoritative centralized GUI that orchestrates multi-node control decisions for active sessions.

The GUI should not own authoritative execution state. The runtime remains the source of truth.

The GUI may disconnect while long-running computations continue. Reconnection should be treated as a normal supported workflow.

## 2.3 Optional workers

Examples:

- FFT worker,
- GPU worker,
- file transcoding worker,
- analysis worker,
- future live media workers.

Workers may run:

- in-process,
- as local processes,
- on remote hosts.

Each computation node should be directly reachable through its own control-plane server endpoint so the GUI can connect to any node, inspect its catalog/capabilities, and manage it without requiring another node to proxy every request.

## 2.4 Session and artifact separation

The architecture should treat at least the following as separate persisted artifacts:

- graph/model definition,
- deployment and placement plan,
- GUI workspace/layout state.

This separation is required because the GUI may exit while runtimes continue executing for long periods.

Placement should be stored in deployment artifacts rather than embedded into the graph model so the same graph can target different host layouts.

## 2.5 Execution model direction

S7 should evolve toward a hybrid execution model.

The architecture should support a mix of:

- scalar/sample-step elements,
- block-processing elements,
- frame-oriented elements.

Adapters between domains may be required. The current scalar semantics remain valid, but future FFT, media, and SDR elements must not force a redesign of the scheduler or transport model.

## 3. Control Plane vs Data Plane

These must remain separate.

## 3.1 Control plane

Use a message-oriented bidirectional protocol for:

- session connect / disconnect,
- graph load / patch / deploy,
- parameter changes,
- start / stop / pause,
- runtime health,
- plugin catalog queries,
- capability advertisement,
- queue statistics,
- error reporting.

This is the correct place for the GUI/runtime conversation.

Every computation node should run a control-plane server so the GUI can connect directly to it for discovery and control.

## 3.2 Data plane

Use a frame/block transport for:

- sample blocks,
- FFT windows,
- spectra,
- video frames,
- SDR/IQ buffers,
- file-source and file-sink payloads.

For same-host inter-process data exchange, shared memory should be the preferred transport.
For cross-host exchange, use a streaming network transport.

## 4. Transport Strategy

All data movement should share one logical frame model while allowing different physical transports.

### Transport kinds

- `inproc` — direct in-process buffers,
- `shm` — same-host inter-process shared memory,
- `remote` — network stream transport,
- `file` — file-backed replay / record path.

### Architectural rule

The runtime should select transport automatically when possible:

- same process -> `inproc`,
- same host but separate process -> `shm`,
- different host -> `remote`.

Explicit bridge/source/sink elements may still exist for integration with external tools, but normal runtime planning should not require users to wire transport details manually.

## 5. Frame Envelope

Future live I/O should use the same envelope as remote and local processing.

A frame or packet should carry at least:

- `epoch_id` — processing run identity,
- `stream_id` — logical stream identity,
- `sequence_no` — strict monotonic order within the stream,
- `pts` — presentation or capture time,
- `duration` or `sample_count`,
- `format` — sample or frame format,
- `flags` — discontinuity, gap, keyframe, overflow, etc.,
- payload bytes or payload descriptor.

Optional later fields:

- `sample_offset`,
- `origin_host_id`,
- `payload_hash`,
- `prev_hash`,
- `dependency_refs`,
- logical clock fields.

### Important semantic rule

Wall-clock time is useful for display and prototype alignment.
Strict ordering must be represented separately by `sequence_no` and `epoch_id`.

Hashes may be added later for traceability and integrity, but should not replace monotonic ordering fields.

## 6. Local Fast Path: Shared Memory

For local separate processes, high-rate numeric data should not flow over the control channel.

Preferred Linux-oriented prototype design:

- shared memory via `memfd_create` or POSIX shared memory,
- block/ring queues,
- notification via `eventfd` or equivalent,
- queue policy chosen per stream class.

### Recommended queue policies

- `latest` — keep only newest item for meters / preview,
- `realtime_overwrite` — bounded preview queues, drop oldest when overloaded,
- `lossless_bounded` — bounded queue with backpressure for FFT/file/SDR capture,
- `mailbox` — latest-value control-rate update.

Shared-memory payloads should still carry the same frame metadata as remote transport.

## 7. Plugin Model

The runtime must be able to load element libraries from shared libraries.

## 7.1 Why plugins matter

Plugins allow:

- adding new DSP/control elements without rebuilding the runtime,
- optional dependencies such as CUDA, video, SDR, or vendor SDKs,
- host-specific capabilities,
- controlled rollout of experimental elements,
- separate packaging of element families.

## 7.2 Library loading model

Each runtime should scan one or more plugin directories on startup or on explicit rescan.

Examples of plugin roots:

- system plugin directory,
- user plugin directory,
- project-local plugin directory.

On Linux, plugins can be shared libraries loaded via `dlopen`/`dlsym`.

## 7.3 ABI recommendation

Use a small C ABI at the plugin boundary, even if plugin internals are implemented in C++.

Reason:

- fewer ABI break problems across compilers/standard library versions,
- easier version negotiation,
- clearer failure modes when plugin/runtime versions differ.

However, the first iterations should be treated as experimental/internal rather than a permanently frozen public ABI. The runtime should therefore validate versions strictly and reject mismatches rather than trying to preserve compatibility prematurely.

Recommended export shape:

- one well-known entry symbol,
- one plugin manifest function,
- one registration function,
- explicit API version fields.

Conceptually:

- runtime loads library,
- runtime queries manifest,
- runtime verifies compatibility,
- runtime asks plugin to register element factories and metadata.

## 7.4 Plugin manifest contents

A plugin manifest should include at least:

- plugin id,
- display name,
- semantic version,
- supported runtime API version,
- vendor / author,
- build target/platform,
- optional dependency tags,
- list of element descriptors provided by the plugin.

## 7.5 Element descriptor contents

Each exported element type should publish metadata sufficient for GUI authoring and runtime planning.

Minimum descriptor fields:

- stable type id, e.g. `s7.dsp.fft.r2c`,
- display name,
- category path, e.g. `DSP/Transforms/FFT`,
- short description,
- plugin id,
- version,
- input port descriptors,
- output port descriptors,
- parameter schema,
- stateful vs stateless,
- direct-feedthrough property,
- supported execution domains,
- supported transport kinds,
- placement hints,
- hardware requirements,
- optional visualization hints.

Type ID format should be lower-case dot-separated segments.

Recommended pattern:

- `<vendor>.<domain>.<family>.<element>[.<variant>]`

Examples:

- `s7.core.math.sum`,
- `s7.core.state.integrator.real`,
- `s7.dsp.fft.r2c`.

### Example placement/hardware fields

- `supports_inproc`,
- `supports_local_process`,
- `supports_remote_host`,
- `requires_gpu`,
- `requires_cuda`,
- `requires_sdr_device`,
- `prefers_shm_input`,
- `prefers_block_processing`.

This metadata is how the runtime tells the GUI what is actually available on this node.

## 8. Runtime Element Catalog

Each computation node should maintain a local catalog composed of:

- built-in elements,
- successfully loaded plugin elements,
- current host capabilities,
- currently unavailable plugins with diagnostics.

The catalog should distinguish:

- `loaded` — usable now,
- `disabled` — present but administratively disabled,
- `incompatible` — API/version mismatch,
- `failed` — load error,
- `unsupported` — present but host capability missing.

That distinction is important for GUI diagnostics.

## 9. How the GUI Learns What Is Available

The runtime should expose capability and catalog discovery over the control plane.

## 9.1 Initial handshake

When the GUI connects, the runtime should send or provide:

- runtime id,
- host id,
- runtime version,
- platform info,
- transport capabilities,
- available plugin catalog,
- queue/transport features,
- optional device inventory summary.

## 9.2 Catalog query / subscription

The GUI should be able to:

- fetch a full catalog snapshot,
- subscribe to catalog updates,
- request details for one element type,
- ask which hosts provide a given element type.

This matters when:

- plugins are rescanned,
- a GPU becomes unavailable,
- an SDR device is attached or removed,
- a remote worker host joins or leaves.

## 9.3 Multi-host authoring

For distributed authoring, the GUI should maintain a view like:

- element available on all selected hosts,
- available only on some hosts,
- unavailable on current deployment target.

This allows the editor to:

- warn before deployment,
- filter the palette by host,
- show placement constraints,
- preserve graph nodes whose backing plugins are temporarily unavailable.

## 10. Runtime Planning with Plugin Availability

The planner should use the element catalog and host capabilities as first-class inputs.

Rules:

- a node can only be placed on a host that provides its element type,
- hardware requirements must match host capabilities,
- transport selection must respect element transport support,
- join/sync nodes should declare alignment policy explicitly,
- deployment should fail early with a useful explanation when constraints cannot be satisfied.

For the initial implementation, placement should be manual. The architecture may later add assisted or automatic placement, but the first deployment model should let the user explicitly choose which node/host runs each computation element or worker-backed component.

## 11. Safety and Failure Isolation

Loading arbitrary shared libraries into the runtime process has risk.

Therefore the architecture should support two plugin execution modes:

### 11.1 In-process plugin

Best for:

- simple DSP/control blocks,
- low overhead,
- trusted code.

### 11.2 Out-of-process worker-backed plugin

Best for:

- GPU-heavy processing,
- unstable vendor SDKs,
- crash-prone external integrations,
- privileged device access,
- components with large binary dependency trees.

In this mode, the element type still appears in the catalog, but its execution is delegated to a managed worker process.

This gives the GUI one consistent authoring model while preserving runtime stability.

### 11.3 Preferred failure policy

When failures occur, the default architectural preference is degraded mode when that can be done safely and transparently.

Default mode combination:

- `degraded_warning` for reduced-capability continuation,
- `transient_recoverable` for automatic retry/reconnect paths.

Examples:

- visualization branch may stop while the main computation continues,
- one optional worker-backed analysis path may restart or remain unavailable,
- a runtime may mark a capability as unavailable without terminating unrelated processing.

Hard-stop behavior is still appropriate when correctness or safety would otherwise be compromised.

## 11.4 Determinism policy

The architecture should distinguish between deterministic and live/non-deterministic sources.

- deterministic sources, such as file-backed replay with preserved data, should support strict ordering and reproducible processing behavior,
- live sources, such as real-time audio/video/SDR capture, should preserve strict per-stream ordering but are not expected to guarantee full end-to-end determinism.

This distinction should appear in source metadata and in future testing/replay modes.

## 12. Suggested Control-Plane Messages

The exact protocol can evolve, but the runtime should conceptually support messages/events like:

- `runtime.hello`
- `runtime.capabilities.get`
- `runtime.catalog.get`
- `runtime.catalog.subscribe`
- `runtime.catalog.changed`
- `runtime.plugin.rescan`
- `runtime.plugin.load_errors`
- `runtime.host.resources`
- `runtime.node.listen`
- `graph.deploy.validate`
- `graph.deploy.start`

The key point is that catalog discovery is not a special case; it is a normal capability of the control plane.

The control plane should also carry certificate-based peer identity and authorization context.

## 12.1 Trust model

Node-to-node and GUI-to-node trust should be based on X.509 certificates.

At minimum, the architecture should leave room for:

- mutual authentication,
- certificate-based runtime identity,
- host/node authorization decisions,
- certificate rotation and revocation,
- secure remote control and deployment.

The exact PKI operational model can be refined later, but the architecture should assume authenticated peers rather than anonymous control connections.

## 13. Practical First Implementation Path

A good incremental plan is:

1. keep current built-in element registry,
2. define an in-memory `ElementDescriptor` structure,
3. expose a catalog snapshot from runtime to GUI,
4. add shared-library scanning and manifest loading,
5. add plugin-provided factories,
6. add plugin status diagnostics,
7. later add out-of-process worker-backed element types.

This path keeps the current project usable while opening the door to distributed execution.

## 14. Architectural Conclusions

The following decisions should now be treated as baseline direction:

- the GUI is separate from the runtime,
- one authoritative centralized GUI orchestrates active multi-node sessions,
- `v1` includes separate GUI/runtime, local workers, and remote nodes,
- control and data paths are separate,
- every computation node exposes a control-plane server endpoint,
- same-host inter-process data should prefer shared memory,
- execution direction is hybrid: scalar, block, and frame capable,
- future live sources/sinks should use the same frame envelope as other streams,
- ordering uses `epoch_id` plus monotonic `sequence_no`, not wall-clock alone,
- plugins are loaded from shared libraries through a small but initially experimental/internal boundary,
- each runtime node publishes an element/capability catalog so the GUI knows what can run there,
- heavy or risky plugins may be represented as worker-backed elements instead of in-process code,
- placement is manual in the first implementation,
- default failure policy combines `degraded_warning` with `transient_recoverable` retry/reconnect where safe,
- deterministic-source replay should be strict, while live sources are not expected to be fully deterministic,
- trust is based on X.509 certificates,
- graph, deployment, and GUI workspace state are separate artifacts,
- element type IDs use lower-case dot-separated naming.

These rules should let S7 grow from a local modeling runtime into a distributed modular compute system without invalidating current core semantics.

