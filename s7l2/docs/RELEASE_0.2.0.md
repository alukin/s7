# S7 C++ 0.2.0 — parallel runtime and local/remote nodes

Goals agreed with the maintainer on 2026-10-23. These are the two main C++
release goals after the 0.1.0 preview; implementation belongs to 0.2.0.
The [0.1.0 checklist](RELEASE_0.1.0.md) remains the current release gate.

## Parallel execution within one runtime

Run independent work in one compiled causal graph concurrently using a bounded,
reusable worker pool. Keep a single-thread execution mode as the reference and
allow the worker count to be configured. Begin with graphs that contain useful
independent branches or components; define the scheduling plan and benchmark
workloads before fixing performance targets.

Preserve dependency ordering, existing element arithmetic and model time.
Causal-memory outputs publish at the step boundary; dependent direct-feedthrough
elements run only after their inputs are ready; next-step state is staged under
the existing contract. Coupled implicit integration keeps its simultaneous-state
semantics. Parallelism does not introduce a global ODE/event solver.

Define which elements/plugins may execute concurrently and serialize or reject
unsafe work explicitly. Lifecycle transitions and graph replacement occur at
acknowledged safe boundaries. Observation stays read-only and coherent after a
completed step, preserving the current stepping-thread contract. Do not promise
transactional rollback of arbitrary plugin state or external side effects.

Acceptance includes comparison with the single-thread reference, seeded state
and noise behavior, insertion/scheduling-order variation, stateful feedback,
coupled integration, failure/shutdown under load and sanitizer stress coverage.
Measure throughput, CPU use, memory and scheduling overhead for small and large
graphs at several worker counts; report both speedups and regressions on stated
hardware. Cross-platform numerical checks use explicit tolerances where needed.

## Multiple nodes on local and remote hosts

Advance the existing two-node MVP into a qualified workflow for several runtime
processes on one host and nodes across real remote hosts. Every process keeps
its own identity and state directory. Deployment, configuration, discovery,
stream diagnostics, redeployment and recovery must work together over repeated
lifecycle cycles.

Use the [split control/data transport](SPLIT_TRANSPORT.md) from 0.1.0: mTLS
control with encrypted data by default and explicit trusted-plaintext data for
loopback/trusted LAN deployments. Keep cross-node connections explicit in the
schema. Preserve per-stream causal order, backpressure and failure visibility;
packet arrival time must not define causality. Physical clock synchronization
and pacing retain their documented roles rather than defining graph semantics.

Acceptance includes several local processes, at least two physical hosts,
fan-out and bidirectional stream topologies, different producer/consumer rates
of wall-clock progress, link interruption/reconnect, slow consumers, repeated
redeployments, process restart, visible sequence gaps and controlled recovery.
Existing crash-durability limits remain explicit unless separately implemented.
Test the supported common protocol subset with Python nodes and provide reusable
conformance checks for further language implementations.

Publish measured CPU, memory, throughput and latency for representative workloads
and both data security profiles. Qualify constrained hardware such as Raspberry
Pi separately when available; desktop results do not establish its performance.

## Planning and release boundary

First agree the parallel execution contract, plugin/thread-safety rules and
benchmark matrix; then implement the scheduler and qualify the local/remote
multi-node workflow. Shared headless tests and protocol fixtures should cover
both workstreams. Studio remains an optional client; improvements to it should
support these workflows without moving execution logic into the GUI.

Additional integration methods, implicit multi-rate conversion, durable stream
journals and a new remote-observation API are not implied by these goals. Their
scope requires separate decisions. Concrete graph sizes, node counts, platform
targets and performance thresholds remain to be chosen from measured baselines.
