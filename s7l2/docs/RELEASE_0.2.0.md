# S7 0.2.0 — runtime, networking, numerical research and Studio

Runtime/node goals agreed with the maintainer on 2026-10-23; Studio diagram
and observation-graph printing added at the maintainer's request on 2026-10-24.
Certificate viewing and node certificate creation added on 2026-10-24 as part
of the networking release scope.
The maintainer subsequently assigned remote node control/restart, Trusted
Networks, runtime node configuration editing and other networking work to
0.2.0, excluding them from the 0.1.1 maintenance release.
Implementation of these release goals belongs to 0.2.0.
The [0.1.2 preview notes](RELEASE_0.1.2_NOTES.md) record the released baseline;
some work originally assigned to 0.2.0 shipped there. Current source also
contains later hardware and packaging work. The project version is still
0.1.2; development artifacts do not constitute a 0.2.0 release.

Status reconciled 2026-11-17. This document retains the detailed release
requirements. The [work plan's status and acceptance
matrix](RELEASE_0.2.0_WORKPLAN.md#status-and-acceptance-matrix) is the single
release-status record, with evidence, owners and remaining gates. Implemented
baseline capabilities remain regression requirements; they are not pending
feature implementations. Existing measurements qualify only their recorded
source, platform and workload, not the final 0.2.0 candidate.

Scope update (2026-10-30): numerical integration and feedback-settling research
was assigned as a P1 0.2.0 workstream on a separate research branch. This
supersedes the 2026-10-25 *release assignment* while preserving its requirement
for review before production integration. Research results, including negative
results, are a release deliverable; a new production algorithm is conditional.

## Indexed serial runtime; parallel execution deferred

Indexed execution resolves causal, memory and coupled-trapezoid element
references once in the compiled graph and uses those references while stepping.
The original ID orders remain for diagnostics. The [runtime performance
investigation](RUNTIME_PERFORMANCE_ARCHITECTURE.md) records Release before/after
measurements, correctness checks and limits. This serial improvement is the
0.2.0 runtime-performance increment; no within-node parallel scheduler is a
0.2.0 goal or acceptance requirement.

A nested schema now behaves exactly like its inlined contents
([ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024)): the compiler
flattens it into its parent, so a feedback loop may close through causal
memory inside it, its exposed ports accept every signal type, errors inside it
are reported when the parent is validated, and it adds no per-step cost of its
own. A nested schema's own rate is declared by its ports and seeds its
elements. Only a `Schema` stepped on its own, outside a compiled graph, keeps a
private runtime, which resolves its exposed-output bindings once when compiled
and carries F64/C64 exposed ports only. The performance record documents
measurements. A step that faults in any phase now
publishes nothing and keeps the previously staged causal-memory values
([ADR-021](../../doc/S7_Architectural_Decisions.md#adr-021)). **S7 0.2.0 uses
sequential nested-schema execution. Parallel nested-schema scheduling remains
outside the release scope and is a possible subject of 0.3.0 research.**

Coarse execution regions, static element price tags, worker-pool behavior,
plugin concurrency and serial/parallel selection are open decisions. The
[0.3.0 planning record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md) preserves the
evidence, candidate ideas and unresolved contracts. It does not commit a
particular scheduler or guarantee parallel execution in 0.3.0.

<a id="numerical-integration-and-feedback-settling-research--p1-planned"></a>

## Numerical integration and feedback-settling research — P1 reports delivered

N0–N4 now have experimental specifications, implementations, measured results
and conclusions: [Phase 1 feedback research](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md),
[Phase 2 pole/zero and equal-work analysis](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md)
and [Phase 3 real-graph integration](NUMERICAL_INTEGRATION_PHASE3_REPORT.md).
Phase 2 corrects the provenance and evaluation-cost ambiguity in the historical
Phase 1 results. The requirements below describe the research assignment;
actual coverage and exclusions are defined by the N0 contract and reports.
The generic TPT/algebraic-loop candidate was not specified or qualified; that
is an explicit research limit, not an implemented general loop solver.

Production integration is no longer just a proposed PR. Opt-in F64 Euler
substeps and coupled two-stage Gauss–Legendre are implemented under the separate
[production contract](COUPLED_INTEGRATION.md). The [integrator guide](INTEGRATORS.md)
also documents the current forward/backward Euler elements and their limits.
These implementations do not turn the experimental feedback prototype into a
production feature or expand the reports' numerical envelopes.

Compare existing and experimental integration methods, investigate within-step
feedback settling and zero-delay approaches, and measure numerical accuracy,
stability, convergence and performance. Publish evidence-based recommendations
for future runtime work. The experimental methods, where mathematically
applicable, are forward Euler, symplectic Euler (Euler–Cromer), the existing
implicit trapezoidal integration, TPT/Zavalishin-style integration, and Euler
integration combined with iterative feedback settling. These are distinct
algorithms, not presumed equivalent. Record each integrator's state-update
formula separately from the algorithm that resolves its feedback equations.

Distinguish publication of causal memory across model steps, invalid
direct-feedthrough algebraic cycles in the current validator, bounded iterative
solution of an implicit equation (as in the [existing coupled Trapezoid
stage](TRAPEZOID_INTEGRATION.md)), and experimental local feedback iteration
intended to reduce artificial delay. A zero-delay loop may have no solution or
multiple solutions; simple iteration may diverge even when a solution exists.
No convergence or general loop support is assumed.

The original validation assignment below reuses committed models where possible.
The [N0 contract](NUMERICAL_FEEDBACK_RESEARCH_N0.md) freezes equations, parameters,
initial states, timestep grids, duration, method settings, reference provenance
and metric tolerances before comparison. Existing tests cover only their stated
envelopes in the [numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md),
example READMEs and research reports. Do not read the assignment as evidence
that every candidate applies to every model.

| Model and starting configuration | Independent reference | Measures |
| --- | --- | --- |
| Harmonic oscillator, `x'=v, v'=-x`, `(x,v)=(1,0)`; 200 s at 1 and 2 kHz | Analytical sinusoid and exact discrete recurrences where applicable | Phase, amplitude drift, energy, stability and frequency distortion |
| Nonlinear pendulum, `theta''=-9.81 sin(theta)`, `(theta,omega)=(1.5,0)`; 100 s at 1 and 10 kHz | Elliptic-function solution cross-checked by independent RK4, as in the [validated baseline](../examples/pendulum/README.md) | Energy conservation or drift, long-run behavior, period, phase-space trajectory and timestep convergence |
| Van der Pol, `x''-mu(1-x²)x'+x=0`, `mu=1,3`; initial `(x,v)=(0.1,0),(2,0),(4,0)`; 40/60 s at 1 and 2 kHz | Independently refined high-accuracy solver, extending the [teaching examples](../examples/van_der_pol/README.md) | Nonlinear oscillation, limit-cycle amplitude/period, transient convergence and sensitivity to method/feedback evaluation |
| Lorenz, `(sigma,rho,beta)=(10,28,8/3)`, initial `(1,1,1)` and `(1.000001,1,1)`; 40 s at 4 and 8 kHz | Independent high-accuracy short-horizon trajectory; independently sampled long-run statistics, extending the [teaching example](../examples/lorenz/README.md) | Early trajectory error, initial-condition sensitivity, long-run statistics, attractor geometry and numerical stability; no long-run pointwise equality gate |
| Linear first-order plant with P/PI control and nonlinear saturated PI control; 12 s at 1 and 2 kHz, based on the [control laboratory](../examples/control/README.md) | Analytical linear response/equilibrium where available and independently computed nonlinear response | Discrete-feedback delay, phase margin/stability, settling residual and convergence, iterations, computational cost and nonconvergence/multiple- or no-solution cases |

The central questions are:

1. Can local feedback iteration improve analog-computer-style simulation fidelity?
2. Under which conditions does iterative settling converge?
3. How do convergence rate and accuracy depend on timestep, loop gain, nonlinearity and topology?
4. Can settling reduce artifacts of explicit model-step delays?
5. How does settling interact with each integration formula?
6. What happens when feedback equations have multiple solutions or no solution?
7. Can the mechanism preserve deterministic causal execution?
8. What is its computational cost per accepted model step?
9. Is the complexity justified by measurable improvement beyond merely doing more evaluations?

Use the completed [indexed serial runtime](RUNTIME_PERFORMANCE_ARCHITECTURE.md)
as the execution baseline. Compare error versus timestep and cost, including
common timesteps and comparable computation budgets. Record runtime per model
step, element evaluations, feedback iterations, convergence failures, memory
where relevant and long-term stability. Separate truncation error, feedback
solver residual and modeling error where possible. Smaller local error in one
example does not establish a generally better method.

S7 remains a causal graph runtime with `Element::step()` as the fundamental
element interface. Preserve explicit causal-memory publication, signal types,
connections, deterministic execution for supported experiments and headless
operation. Do not introduce a global ODE solver as a replacement or a parallel
graph scheduler in this workstream; independent ODE solvers are permitted as
references. The coupled Trapezoid stage is a useful existing implementation,
but experiments must preserve its current semantics. Any proposed change to
causal ordering, publication or loop resolution needs separate architectural
review.

The **0.2.0 research acceptance requirement** was a dedicated research
branch containing experimental implementations, reproducible tests/benchmarks
and documented independent references, with comparative accuracy, convergence,
stability and performance results, known limits and failed experiments, and a
published technical report with recommendations. The linked reports and
artifacts supply this deliverable within their stated limits; the plan alone
is not evidence. Inconclusive or negative findings are valid outcomes.
Production integration was conditional and requires a demonstrated advantage on
appropriate models, numerical and causal-semantic review, preserved existing
behavior unless an explicit new mode is introduced, compatibility/regression
tests and an explicit maintainer decision. The separately implemented methods
remain subject to final release regression; no additional integrator or
general feedback-settling mechanism is required for 0.2.0.

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

Current evidence is partial: Python already carries F64 graph streams over
mTLS, and the [three-process transport lab](../../s7py/tools/transport_lab/README.md)
records mixed-language chain/reconnect checks. Python still lacks separate
data listeners and trusted-plaintext data. [C++/Python/Java frame
conformance](performance/transport-2026-11-12/FRAME_CONFORMANCE.md) qualifies
the frame-service/wire subset, not automatic deployed-frame negotiation or
the complete D0–D5 lifecycle. Whole-diagram feedback validation remains X1–X3
in the work plan. Java evidence does not add Java control-plane parity to the
0.2.0 requirements.

Cross-node feedback now has an initial C++ implementation following
[ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027) and the maintainer's
2026-11-17 approval of the strongest researched candidate. The shared
Studio/s7ctl path validates the whole flattened graph before deployment,
derives outgoing-first static orders, and checks node capabilities. Receivers
check the distributed wait graph and local compiler dependencies. The runtime
stays serial and feedback keeps explicit memory. The
[implementation/test guide](DISTRIBUTED_CAUSAL_SCHEDULE.md) records the initial
single-rate scope and pending real-loop qualification. The
[literature](CROSS_NODE_CAUSALITY_RESEARCH.md) and
[fourth research report](CROSS_NODE_CAUSALITY_PHASE4.md) retain performance and
applicability evidence; broader distributed release acceptance remains open.

Publish measured CPU, memory, throughput and latency for representative workloads
and both data security profiles. Qualify constrained hardware such as Raspberry
Pi separately when available; desktop results do not establish its performance.

## Remote node administration and Trusted Networks

Status (2026-11-17): open; the reviewed administration contract and headless
implementation are still required. Existing deployment/discovery operations
are a baseline, not evidence of node-settings apply or process restart.

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

Status (2026-11-17): open; test credential scripts and existing identity
summaries do not close the provisioning/viewer acceptance below.

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

## Studio element resizing

Status (2026-11-17): resizing and schema double frames are implemented.
Printing qualification remains open. The following records the original
presentation scope requested on 2026-10-25 and corrected on 2026-10-31:

- Make element shapes resizable in the diagram editor. Define resize handles,
  grid behavior and minimum dimensions that keep ports and labels usable.
  Preserve wire attachments/routing, selection, undo/redo and saved geometry
  through reopening, including nested schemas.
- Give schema-backed elements a double frame while keeping every element body
  text-only, with readable name and type.
- Use soft family colors for element fills and borders, with dark matching text.
  Mark elements explicitly declared stateless with a small text label; leave
  elements whose statefulness is unknown unmarked.

Current source differs from the original text-only presentation: basic elements
now use [mathematical block symbols](DIAGRAM_SYMBOLS.md), with instance/type or
method labels where space permits. That guide describes current rendering;
this refresh records the difference rather than reinstating the historical
text-only requirement. The user checked desktop rendering on 2026-11-16 and
reported that it looked fine. This does not qualify printed output.

These presentation changes must not change element identity, port semantics
or the executing causal graph. Check narrow/large shapes, zoom/display scaling
and color/grayscale readability. Carry resized geometry and double frames into
diagram printing below. No additional pictogram redesign is assigned here.

Implementation (2026-10-31): Studio now uses a selected element's bottom-right
handle for grid-snapped resizing, with port/wire updates, one undo step and
saved dimensions in both root and nested layouts. Schema-backed elements have
a double frame and text-only bodies. Muted family colors and a `stateless`
footer use registry metadata; element text keeps readable contrast. The diagram
tests cover resize,
undo/redo, cancellation, routing, clipboard and file round trips; rendered
examples were inspected. Printing qualification belongs to the printing goal.
Later on 2026-10-31 the minimum shrank from 7 × 5 to 2 × 2 steps (taller for
more ports, which are then one step apart), with body text scaled down to 60 %
below 7 × 5 steps and lower text lines dropped when they do not fit. Saved
diagrams with elements below 7 × 5 steps do not open in earlier 0.2.0 builds.

## Studio diagram and observation-graph printing

Status (2026-11-17): open; workflow/output decisions, implementation and
qualification remain. Acceptance must include the current mathematical block
symbols as well as saved sizes, labels and schema double frames.

The maintainer reaffirmed diagram and plot printing on 2026-10-25 alongside
resizable shapes. Printed diagrams must include saved element sizes and double
frames, with readable names/types and complete wire connections.

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

Status (2026-11-17): open; the design and schema-backed implementation below
remain future work.

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

## Test suite grouping and selection

Reminder added at the maintainer's request on 2026-10-31. Refine and group the
tests so a change runs the tests that cover it, not the whole suite. For
example, a Studio UI-only change (plot shortcuts, element painting, dialogs)
should not need the expensive numerical runs such as the double/coupled
pendulum tests.

Status (2026-11-17): area/cost grouping and documented selections remain open.
CTest labels are concentrated in Studio GUI tests (`gui`, `qt-regression`,
`rendering`, `quick3d`, `examples`); the proposed taxonomy is not implemented.
Historical measurement (2026-10-31): `ctest -R s7studio -j16` (42 tests) took
about 3 min 15 s on the 7940HS dev host, almost all of it in
`s7studio_coupled_pendulum` (~194 s),
`s7studio_gui_workflows` (~77 s) and `s7studio_nonlinear_pendulum` (~39 s);
most other tests took seconds. These are not current-suite timings.

Proposed scope for review:

- Label every test by area (core, runtime, networking/distributed,
  cross-language, Studio diagram, Studio viewers, Studio GUI workflows) and by
  cost (`fast`, `slow`), with long numerical/simulation runs in their own label.
- Document selections, e.g. `ctest -L studio -LE slow` for UI changes, and a
  table from changed paths to the labels to run, in the developer
  documentation and `AGENTS.md`.
- Keep the full suite, including slow numerical tests, for CI, merges to
  `main`, release qualification and changes to runtime, element or numerical
  code. Selection must narrow local runs, not reduce release coverage.
- Check whether the slow tests can be shortened (fewer steps, smaller cases)
  without losing what they verify; record the tolerances they keep.
- Verify timing-dependent tests found while measuring: in
  `s7studio_window_lifecycle`, `closeAndDestruct(close-paused)` and
  `closeAndDestruct(destruct-paused)` sometimes miss the `Stopping` state
  before the window goes (`stoppedBeforeDestruction`). On 2026-10-31 it failed
  68 of 160 runs with 16 copies in parallel on the dev host, and occasionally
  in `ctest -j16`. Current `closeAndDestruct` accepts either `Stopping` or
  `Stopped` before destruction, and `replaceWhileActive` explicitly releases
  its reused gate. The recorded missing-transient-state issue has a code
  correction; the old failure count is historical. This planning refresh does
  not claim a fresh repeat of the 160-run stress experiment.

## Video source/sink — planned addition, 2026-11-18

The maintainer requested `VideoSource` and `VideoSink` using native PipeWire,
with a webcam captured on one host and viewed on another as the first milestone.
Screen/window capture follows. Video remains a causal stream of numeric values;
the initial proposal uses existing U8 pixel-channel streams and explicit raster
geometry/rates rather than adding an image type to the core.

That initial per-pixel proposal is now a measured baseline. The maintainer's
subsequent clarification favors [whole-frame video processing and specialized
media transfer](VIDEO_IO_FRAME_DESIGN.md), with receiver-side transformations
and explicit numeric computation/result bridges. The final frame ownership,
execution and transport contracts remain proposed.

Compression is a **user-selectable, tunable per-stream option**: raw baseline,
existing LZ4/Zstandard lossless transport, then JPEG and H.264 image codecs with
explicit settings and receiver capability checks. New codec framing/descriptor
semantics require review; current numeric lossless formats are not relabeled.

The [implementation plan](VIDEO_IO_PLAN.md) records the phases, performance
requirements, first-host inventory, Windows Media Foundation recommendation and
successful compile/link-only capture API probe. The [V0 report](VIDEO_IO_SPIKE.md)
now records 16 two-host experiments, live PipeWire webcam/output/viewing through
GStreamer adapters, lossless codec choices and a 60-second paced check. These
bounded results do not ship native video providers or qualify physical desktop,
higher-resolution real-time or native Windows camera support. This work belongs to the
maintainer/Codex; D0 and D1/D2 remain assigned to teren_ukr. Final release inclusion
depends on the delivered and qualified subset, not the existence of this plan.

## Hardware and release distribution

Status (2026-11-17): the public hardware modules and distribution tooling are
implemented, with bounded verification. The [hardware guide](HARDWARE_IO.md)
defines the initial profile: one hardware boundary, one exact rate domain,
explicit activation through `s7hw` or an SDK owner. Studio can edit/validate
the plugin elements; ordinary Studio Run and runtime-service deployment do
not yet own hardware activation. Integrating those owners is not silently
added as a release requirement by this refresh.

The [live hardware report](HARDWARE_IO_LIVE_TESTS.md) records Linux audio and
radio experiments and their limits. Its two-host audio leg used SSH raw PCM,
not S7 transport; it cannot close the distributed acceptance matrix. Independent
hardware-clock alignment, drift correction and adaptive resampling remain
later research.

The maintainer's agreed distribution matrix is:

| Artifact | Initial target/profile | Existing evidence and remaining acceptance |
| --- | --- | --- |
| Existing Linux portable tarball | Linux x86-64; sound and SDR | Earlier portable-package evidence exists. Rebuild and verify a relocated candidate with the current hardware profile and final 0.2.0 source/version. |
| Native Fedora RPMs | Fedora 45 x86-64; sound and SDR | Six packages built; 17/17 focused regressions, 5/5 headless and 23/23 full installed checks passed. Final-version rebuild and upgrade qualification remain. |
| Native Ubuntu DEBs | Ubuntu 26.04 amd64; sound and SDR | Six packages built with the same 17/5/23 check results. Ubuntu 26.10 has no native package qualification; final-version rebuild and upgrade qualification remain. |
| Existing Windows portable ZIP | Windows x86-64; PortAudio sound, SDR excluded | 112/112 configured Wine CTests and 21/21 relocated checks passed. Native Windows sound/device and desktop/GPU qualification remain open; Wine results do not establish them. |

See [native package verification](NATIVE_LINUX_PACKAGES.md),
[Windows verification](WINDOWS_BUILD.md#sound-only-build-verification-2026-11-17)
and the [release collection workflow](../tools/release/README.md).
Analysis plugins remain available in both platform profiles. The packages
must not contain private providers or development fixture credentials.

All artifacts must come from the intended release source and version, with
dependency/profile provenance, notices, checksums and matching validation
records. Current local artifacts are 0.1.2 development builds, not 0.2.0
release candidates. Final full regressions and package checks belong to Q1
in the work plan; test selection must not narrow that release gate.

## Planning and release boundary

Reconcile the existing distributed implementations with the D0 contract and
conformance baseline; then close split-profile parity and the remaining
multi-node lifecycle gates. Within-node parallel scheduling and
its plugin/thread-safety contract belong to 0.3.0 planning. Shared headless
tests and protocol fixtures should cover their integration. Studio
remains an optional client; improvements to it should support these workflows
without moving execution logic into the GUI.
Plan and qualify Studio printing as an additional release workstream, with its
own viewer/output matrix and packaging checks.
Plan certificate viewing/creation and remote node administration alongside
multi-node setup, so networking acceptance uses the provisioning and
configuration/restart workflows as well as protocol fixtures.

Historical numerical experiments remain research; the separately implemented
production methods follow their documented contract and regression envelope.
Additional numerical or loop-resolution methods need a separate review.
Implicit multi-rate
conversion, durable stream journals and a new remote-observation API are not
implied by these goals. Their scope requires separate decisions. Concrete graph
sizes, node counts, remote host/supervisor targets and performance thresholds
remain to be chosen from measured baselines. The initial package targets above
are recorded evidence, not a promise of every distribution or native Windows
hardware capability. The work plan's matrix records the remaining decisions;
this refresh does not drop any agreed release feature.
