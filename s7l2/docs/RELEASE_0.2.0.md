# S7 0.2.0 — parallel runtime, networking, certificate tools and printing

Runtime/node goals agreed with the maintainer on 2026-10-23; Studio diagram
and observation-graph printing added at the maintainer's request on 2026-10-24.
Certificate viewing and node certificate creation added on 2026-10-24 as part
of the networking release scope.
The maintainer subsequently assigned remote node control/restart, Trusted
Networks, runtime node configuration editing and other networking work to
0.2.0, excluding them from the 0.1.1 maintenance release.
Implementation of these release goals belongs to 0.2.0.
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

## Remote node administration and Trusted Networks

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

## Studio diagram and observation-graph printing

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

## Planning and release boundary

First agree the parallel execution contract, plugin/thread-safety rules and
benchmark matrix; then implement the scheduler and qualify the local/remote
multi-node workflow. Shared headless tests and protocol fixtures should cover
both workstreams. Studio remains an optional client; improvements to it should
support these workflows without moving execution logic into the GUI.
Plan and qualify Studio printing as an additional release workstream, with its
own viewer/output matrix and packaging checks.
Plan certificate viewing/creation and remote node administration alongside
multi-node setup, so networking acceptance uses the provisioning and
configuration/restart workflows as well as protocol fixtures.

Additional integration methods, implicit multi-rate conversion, durable stream
journals and a new remote-observation API are not implied by these goals. Their
scope requires separate decisions. Concrete graph sizes, node counts, platform
targets and performance thresholds remain to be chosen from measured baselines.
