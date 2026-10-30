# S7 0.2.0 — runtime, networking, diagram visuals, printing and signal generator

Runtime/node goals agreed with the maintainer on 2026-10-23; Studio diagram
and observation-graph printing added at the maintainer's request on 2026-10-24.
Certificate viewing and node certificate creation added on 2026-10-24 as part
of the networking release scope.
The maintainer subsequently assigned remote node control/restart, Trusted
Networks, runtime node configuration editing and other networking work to
0.2.0, excluding them from the 0.1.1 maintenance release.
Implementation of these release goals belongs to 0.2.0.
The [0.1.1 preview notes](RELEASE_0.1.1_NOTES.md) record the current released
baseline. The [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) assigns distributed
delivery and the parallel architecture, simulation and Studio work, with
development steps and PR review gates.

Scope clarification (2026-10-25): integrator and feedback-settling research is a
separate project with no release assignment yet, outside both 0.1.1 and 0.2.0.
The maintainer and Codex will work on that research separately. Its findings
require review before any release integration is considered; see
[OPEN_PROBLEMS.md](OPEN_PROBLEMS.md).

## Parallel execution within one runtime

The [runtime performance investigation](RUNTIME_PERFORMANCE_ARCHITECTURE.md)
proposes compiling the connection DAG and execution records as the basis for
this scheduler while preserving the simple `Element::step()` contract. Its
measurements are provisional; performance targets require benchmark gates.

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

## Remote node administration and Trusted Networks

The [Studio design boundaries](STUDIO_DESIGN_0_2.md) map document, discovery,
request lifecycle, viewer and headless runtime ownership for this work.

Provide the remote node control, configuration and restart workflow as part of
0.2.0. This includes ST-009 (Settings → Trusted Networks…), Studio discovery and
credential settings, runtime node configuration editing, and effective network
policy/status. The release assignment supersedes earlier 0.1.1 planning.
Existing 0.1.0 networking remains available during 0.1.1; menu reorganization
there does not expand its behavior.

Plan three configuration owners explicitly:

- Studio: discovery endpoints, CA/client certificate/private-key paths and
  refresh preferences, with one settings model shared by Settings and nodes UI.
- Diagram: verified control/data endpoints and data security profiles in
  `runtime_nodes`, applied through the existing deployment transaction.
- Runtime node: listener enablement, bind/advertise addresses and ports,
  `websocket.data_server.security`, outgoing connection/TLS settings and
  `control` controller-purpose/fingerprint authorization configuration.

Current `s7ctl config`/GET_NETWORK_CONFIGURATION retrieves the active deployment;
its name does not imply access to node startup/listener configuration.
RuntimeService's deployment host keeps node settings outside graph deployment.
No existing operation writes listener configuration or restarts the process.
Design the actual administration contract before implementation: configuration
read/validate/stage/commit behavior, persistence and application timing,
revision conflicts, cancellation/recovery, and process supervision/restart
ownership. A local file editor can complement that workflow; saving a draft
must not claim remote installation, activation or restart.

Control mutations must continue to use transport-verified ControlPeer identity,
required certificate purpose and pinned fingerprint. A JSON trust field or
plaintext data policy cannot grant control authority. Separate changing trust
material from checking a node's current identity, and preserve explicit encrypted
or trusted-plaintext data policy without fallback. Keep credential creation and
inspection coordinated with the certificate tools below.

Acceptance must cover configuration validation and unrelated-field preservation,
failed/atomic persistence, stale revisions, unauthorized operations, changed
control endpoints/credentials/pins, lost connections during apply/restart,
idempotent retry/reconciliation and observed effective state after reconnect.
Test both data profiles, committed-deployment policy agreement, restart/stream
recovery and separate Studio/diagram/node ownership with real node processes.
Define supported remote hosts, supervision backends and packaged workflows during
0.2.0 planning; source inspection alone does not qualify remote administration.

## Certificate viewer and node certificate creation

Provide a certificate viewer and a node certificate creation program as part
of the networking workflow. Both are explicit 0.2.0 goals, outside the tentative
0.1.1 maintenance scope. Existing test-certificate generation scripts and
Studio's certificate file fields/identity summaries do not provide this complete
user workflow.

The viewer should open certificate files and present subject/issuer, serial,
validity, SHA-256 fingerprint, S7 node UUID from URI SAN, endpoint SANs, key
usage/Extended Key Usage and chain information where available. Distinguish
parsing/displaying a certificate from verifying its chain, node identity and
authorization. Show what was actually checked, against which trust material,
and why a check failed. Keep private-key material out of the viewer/readout.
Plan integration with Studio's node details and Trusted Networks settings.

The creation program should produce credentials usable by the existing runtime
and control clients, with a headless workflow for node provisioning. Define
runtime-node and Studio/control-node certificate profiles, including S7 UUID
identity (`spiffe://s7/node/<uuid>`), endpoint identities and the appropriate
TLS usages. Control-node issuance must preserve the required control-purpose
EKU and explicit fingerprint pinning; creating a certificate does not authorize
it automatically. Supply clear output paths and instructions for configuring
the certificate, private key, CA chain and control pin.

Before implementation, settle the program/CLI name, Studio entry points,
CA creation/import and signing versus CSR workflows, permitted algorithms,
validity defaults, key protection and renewal/replacement behavior. Reuse the
existing identity/control contracts; do not distribute the development fixture
CA or private keys as real credentials. Use independent generated credentials
in acceptance, with deliberate handling of existing output files and write
failures. Do not introduce online issuance or remote administration implicitly.

Acceptance includes:

- Viewer coverage for runtime/control/CA certificates, malformed input,
  expired/not-yet-valid certificates, untrusted chains and missing/mismatched
  node identities or usages, with accurate explanatory status.
- Newly created credentials loaded by real runtimes and Studio/headless
  control clients; successful mTLS discovery, split encrypted data transport
  and an authorized deployment using the intended control profile and pin.
- Rejection of incorrect identity, control purpose or pin. Trusted-plaintext
  data configuration must not weaken the separate control authorization path.
- Certificate/key matching, key/file protection, cancelled/failed output and
  replacement/renewal configuration, including the effect on control pins.
- Packaged Linux and Windows viewer/tool workflows and a documented multi-node
  provisioning walkthrough that does not depend on the source checkout or test
  fixture credentials.

Keep credential tooling independent of runtime graph execution and keep Qt out
of headless provisioning paths. Full PKI lifecycle services (such as an online
CA or revocation distribution) require separate scope decisions.

## Studio element resizing and type pictograms

Requested by the maintainer on 2026-10-25 for 0.2.0:

- Make element shapes resizable in the diagram editor. Define resize handles,
  grid behavior and minimum dimensions that keep ports and labels usable.
  Preserve wire attachments/routing, selection, undo/redo and saved geometry
  through reopening, including nested schemas.
- Add pictograms inside element bodies to distinguish element types visually.
  Use project-owned rendering and element/library metadata; retain readable
  names/types and an explicit fallback when a pictogram is unavailable.
  Cover built-in, schema-backed, plugin and observer elements.

These are presentation changes: resizing or choosing a pictogram must not
change element identity, port semantics or the executing causal graph. Plan
narrow/large shapes, zoom/display scaling and color/grayscale readability.
Carry the resized geometry and pictograms into diagram printing below.

## Studio diagram and observation-graph printing

The maintainer reaffirmed diagram and plot printing on 2026-10-25 alongside
resizable shapes and type pictograms. Printed diagrams must include their saved
sizes and body pictograms with readable labels and complete wire connections.

Provide printing for diagrams and observation graphs. This is missing
functionality, not a tested feature of 0.1.0: the current Studio has page-layout
metadata but no diagram/graph print workflow. No printing acceptance has been
performed. This goal belongs to 0.2.0, not the tentative 0.1.1 bug-fix release.

Plan the print workflow before implementation, including print preview, printer
selection and PDF output. Define diagram scope (active/root/nested diagram),
paper size, orientation, margins, fit-to-page and whether multi-page tiling is
required. Reuse existing diagram page settings where appropriate. Print the
diagram content without editor selection handles or unrelated UI controls.

For observation graphs, define the supported viewer matrix: Scope, Live
Oscilloscope Time/Spectrum/XY/Correlation, Phase Portrait and 3D trajectory.
Specify the printed range, visible channels/overlays, axes, units, legends and
useful run/model-time identification. Settle how a 3D view is captured during
planning. Printing a live graph must use one coherent captured display/data
snapshot; opening a print dialog must not implicitly pause the simulation or
change its acquisition, recording or causal execution.

Acceptance must include:

- Small and large diagrams, nested schemas, page sizes/orientations, scaling,
  readable labels and complete wires; explicit checks for clipping and page
  boundaries.
- Each supported observation view, including multiple channels, units,
  comparison overlays and live/frozen/paused/completed runs where applicable.
  Verify output against the selected range and snapshot.
- Preview and PDF inspection for resolution, font/line readability, margins,
  color and grayscale output; preserve vector content where practical.
- Printer/dialog cancellation, output failures and responsive operation during
  live runs; verify that printing leaves the model and observation data intact.
- Relocated Linux/Windows package checks for required printing components.
  Record native print-dialog and physical-printer evidence separately from PDF
  and automated rendering checks; neither establishes physical printing alone.

Exact layouts, tiling scope, supported printers and test platforms remain
planning decisions. Keep printing in Studio; headless runtime modules must not
gain GUI dependencies.

## Software signal-generator element

Added at the maintainer's request on 2026-10-25. Plan a reusable signal-generator
schema element for 0.2.0, using the UNI-T UTG932E function/arbitrary waveform
generator as the interaction reference. Its [manufacturer user manual](https://storage.googleapis.com/uni-tdocuments/Library/UTG900E_Series_User_Manual_REV2a_HQ.pdf)
covers the UTG900E series. The maintainer's hardware photo and downloaded manuals
remain local reference material.

Proposed scope for design review:

- Two independently configured outputs, waveform selection, frequency/period,
  amplitude, DC offset, phase and output enable. Include duty cycle where relevant.
- Begin with sine, square/pulse, triangle/ramp and DC. Decide the noise, arbitrary
  waveform, sweep and modulation scope explicitly during 0.2.0 planning.
- Pair numeric entry with familiar rotary controls, keep per-channel state
  visible, and use the live oscilloscope as the accompanying software test bench.
  Detailed UI optimization is deferred to that workstream.
- Implement causal, deterministic generation in model time through schema
  composition and existing source primitives where appropriate. Keep runtime
  operation headless; put interactive controls in Studio. Define phase/reset
  behavior, runtime parameter transitions, units/types and rate limitations.
- Verify waveform shape, frequency, amplitude/offset, phase and duty cycle across
  model rates, independent channels, enable/reset transitions, saved diagrams,
  headless runs and scope observations. Report sampling/aliasing limits explicitly.

This release item plans software emulation. Hardware control and complete UTG
feature parity require separate scope decisions. Implementation remains future
work and is independent of the integrator-research branch.

## Planning and release boundary

Agree the parallel execution contract, plugin/thread-safety rules and benchmark
matrix before implementing the within-node scheduler. Establish the distributed
contract and conformance baseline in parallel; then the Python data plane and
multi-node work can advance independently of scheduler implementation. Shared
headless tests and protocol fixtures should cover their integration. Studio
remains an optional client; improvements to it should support these workflows
without moving execution logic into the GUI.
Plan and qualify Studio printing as an additional release workstream, with its
own viewer/output matrix and packaging checks.
Plan certificate viewing/creation and remote node administration alongside
multi-node setup, so networking acceptance uses the provisioning and
configuration/restart workflows as well as protocol fixtures.

Additional integration methods, implicit multi-rate conversion, durable stream
journals and a new remote-observation API are not implied by these goals. Their
scope requires separate decisions. Concrete graph sizes, node counts, platform
targets and performance thresholds remain to be chosen from measured baselines.
