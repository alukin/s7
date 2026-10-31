# S7 0.2.0 — runtime, networking, numerical research and Studio

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
delivery, simulation and Studio work, with
development steps and PR review gates.

Scope update (2026-10-30): numerical integration and feedback-settling research
is a P1 0.2.0 workstream, conducted on a separate research branch. This
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

Nested schemas now resolve their exposed-output bindings once when their
private runtime is compiled, instead of looking up each internal output by
element ID on every step. The bindings do not change output order or
publication timing. A nested schema's exposed ports carry F64 and C64 only. Any other
type is now rejected when the parent graph is compiled, instead of failing on
a later step or misreading an input; top-level exposed ports accept any type. The performance record documents binding lifetime,
measurements and remaining overhead. A step that faults in any phase now
publishes nothing and keeps the previously staged causal-memory values
([ADR-021](../../doc/S7_Architectural_Decisions.md#adr-021)). **S7 0.2.0 uses
sequential nested-schema execution. Parallel nested-schema scheduling remains
outside the release scope and is a possible subject of 0.3.0 research.**

Coarse execution regions, static element price tags, worker-pool behavior,
plugin concurrency and serial/parallel selection are open decisions. The
[0.3.0 planning record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md) preserves the
evidence, candidate ideas and unresolved contracts. It does not commit a
particular scheduler or guarantee parallel execution in 0.3.0.

## Numerical Integration and Feedback-Settling Research — P1 planned

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

The planned validation matrix below reuses committed models where possible.
N0 in the [work plan](RELEASE_0.2.0_WORKPLAN.md) freezes equations, parameters,
initial states, timestep grids, duration, method settings, reference provenance
and metric tolerances before comparison. Existing tests cover only their stated
envelopes in the [numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md)
and example READMEs; the additional method/settling comparisons are planned,
not validated.

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

The **0.2.0 research deliverable** is complete only when a dedicated research
branch contains experimental implementations, reproducible tests/benchmarks
and documented independent references, with comparative accuracy, convergence,
stability and performance results, known limits and failed experiments, and a
published technical report with recommendations. This plan alone does not
complete the milestone. Inconclusive or negative findings are valid outcomes.
Production integration is optional and requires a demonstrated advantage on
appropriate models, numerical and causal-semantic review, preserved existing
behavior unless an explicit new mode is introduced, compatibility/regression
tests and an explicit maintainer decision. No new integrator or settling method
is required in the production runtime for 0.2.0.

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

## Studio element resizing

Requested by the maintainer on 2026-10-25 for 0.2.0; presentation scope
corrected on 2026-10-31:

- Make element shapes resizable in the diagram editor. Define resize handles,
  grid behavior and minimum dimensions that keep ports and labels usable.
  Preserve wire attachments/routing, selection, undo/redo and saved geometry
  through reopening, including nested schemas.
- Give schema-backed elements a double frame while keeping every element body
  text-only, with readable name and type.
- Use soft family colors for element fills and borders, with dark matching text.
  Mark elements explicitly declared stateless with a small text label; leave
  elements whose statefulness is unknown unmarked.

These presentation changes must not change element identity, port semantics
or the executing causal graph. Check narrow/large shapes, zoom/display scaling
and color/grayscale readability. Carry resized geometry and double frames into
diagram printing below. Element pictograms are outside the 0.2.0 release scope.

Implementation (2026-10-31): Studio now uses a selected element's bottom-right
handle for grid-snapped resizing, with port/wire updates, one undo step and
saved dimensions in both root and nested layouts. Schema-backed elements have
a double frame and text-only bodies. Muted family colors and a `stateless`
footer use registry metadata; element text keeps readable contrast. The diagram
tests cover resize,
undo/redo, cancellation, routing, clipboard and file round trips; rendered
examples were inspected. Printing qualification belongs to the printing goal.

## Studio diagram and observation-graph printing

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

Establish the distributed contract and conformance baseline; then the Python
data plane and multi-node work can advance. Within-node parallel scheduling and
its plugin/thread-safety contract belong to 0.3.0 planning. Shared headless
tests and protocol fixtures should cover their integration. Studio
remains an optional client; improvements to it should support these workflows
without moving execution logic into the GUI.
Plan and qualify Studio printing as an additional release workstream, with its
own viewer/output matrix and packaging checks.
Plan certificate viewing/creation and remote node administration alongside
multi-node setup, so networking acceptance uses the provisioning and
configuration/restart workflows as well as protocol fixtures.

Additional integration methods are experimental under the numerical research
workstream; production adoption needs the review above. Implicit multi-rate
conversion, durable stream journals and a new remote-observation API are not
implied by these goals. Their scope requires separate decisions. Concrete graph
sizes, node counts, platform targets and performance thresholds remain to be
chosen from measured baselines.
