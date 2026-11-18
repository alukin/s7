# S7 0.2.0 work plan and distributed development

Working plan recorded 2026-10-30; status reconciled 2026-11-17 against current
source and checked-in evidence. The maintainer and Codex own architecture,
development planning, simulation accuracy, integrator research and Studio
usability. teren-ukr leads the distributed work, building on his Python-node
work. The ownership split reflects that agreement; the steps below are a
proposed sequence. The [0.2.0 release scope](RELEASE_0.2.0.md) assigns the
separate numerical research branch to this release as a P1 research deliverable,
without requiring production adoption of an experimental algorithm. The
research reports and separately implemented production methods are now
available. Some original 0.2.0 items already shipped in the
[0.1.2 baseline](RELEASE_0.1.2_NOTES.md); keep them as regression requirements,
not pending implementations.

Update, 2026-11-18: the maintainer confirmed that **D0 and D1/D2 belong to
teren_ukr**. The cross-node scheduling implementation was accepted as ready;
broader release qualification remains distinct. The maintainer/Codex next
workstream is [VideoSource/VideoSink](VIDEO_IO_PLAN.md): first capture a webcam
on one host and view it on another, then add screen capture and Windows support.
The subsequent [frame-oriented design](VIDEO_IO_FRAME_DESIGN.md) supersedes the
per-pixel production baseline: process complete frames, use suitable media
transfer, and expose numeric computations/results through explicit elements.
Compression is a per-stream, user-selectable and tunable option. This is a new
planning item, not implemented video support or a closed release gate.
The [Full HD FFmpeg experiment](VIDEO_IO_FULL_HD.md) subsequently passed native
synthetic 1080p at nominal 30 fps and receiver grayscale; actual camera input was
720p upscaled to 1080p at a lower measured rate. Production
[GStreamer adapters belong to 0.3.0](RELEASE_0.3.0.md#gstreamer-adapters).

## Status and acceptance matrix

This is the single status record for the [detailed release scope](RELEASE_0.2.0.md).
**Implemented** means present in source with the linked bounded evidence;
**Evidence delivered** means research artifacts/results exist;
**Partial** means some acceptance cases exist but the workstream is not closed;
**Open** means a decision, implementation or qualification remains;
**Deferred** means outside 0.2.0. No status implies a final-candidate test pass.
Historical test counts below are evidence references, not tests rerun by this
planning refresh. Unqualified device/platform cases must stay explicit in the
release notes; they are not automatically added as support commitments.

| Item | Status at 2026-11-17 | Evidence / current boundary | Remaining acceptance |
| --- | --- | --- | --- |
| B1 — Indexed serial execution | Implemented | [Performance record](RUNTIME_PERFORMANCE_ARCHITECTURE.md); compiled element references replace per-step ID lookup. | Preserve numerical/publication behavior in final regressions. |
| B2 — Nested-schema flattening | Implemented | [ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024), `s7core/src/flatten.cpp`, 0.1.2 notes. Flattening is local to a node. | Final nested/type/rate/rollback regressions; cross-node cycles remain X1–X3. |
| N0–N4 — P1 numerical research | Evidence delivered | [N0](NUMERICAL_FEEDBACK_RESEARCH_N0.md), [Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md), [Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md), [Phase 3](NUMERICAL_INTEGRATION_PHASE3_REPORT.md), runners and compact results. | Retain reproducibility/provenance and reported exclusions. Generic TPT/algebraic-loop support was not qualified and is not a new production requirement. |
| NI — Production coupled methods | Implemented | [Production contract](COUPLED_INTEGRATION.md); opt-in F64 Euler substeps and Gauss–Legendre, separate from the research prototype. | Final eligible-region, numerical-failure/publication and reference regressions; no broader numerical claims. |
| R1–R6 — Model time and rates | Implemented | [Sample-rate model](../../doc/S7_Sample_Rate_Model.md), 0.1.2 notes; exact rates, durations, filter-rate checks and explicit Resampler. | Preserve domain-rate and explicit-conversion behavior in final regressions. |
| R7 — Rate-following test fixtures | Open | Explicit-rate C++ construction remains supported; the planned fixture review is not recorded as complete. | Review pins and exercise run/domain-rate adoption without removing intentional mismatch tests. |
| S1–S3 — SDR numeric formats | Implemented | [ADR-023](../../doc/S7_Architectural_Decisions.md#adr-023), conversion/CI12 wire fixtures and capability checks. | Preserve native-width conversion and unsupported-type rejection in final regressions; Windows omits SDR hardware modules. |
| F1 — Frame transport/elements | Implemented, bounded | [Service](FRAME_TRANSPORT.md), [scalar boundary elements](FRAME_ELEMENTS.md), [789 cross-language checks](performance/transport-2026-11-12/FRAME_CONFORMANCE.md). | Retest the implemented subset. Automatic frame deployment/negotiation and hardware-clock coordination are not implied. |
| H1 — Initial hardware profile | Implemented, bounded | [Hardware guide](HARDWARE_IO.md), [Linux live evidence](HARDWARE_IO_LIVE_TESTS.md): explicit `s7hw`/SDK owner, one boundary/rate domain. | Preserve offline/no-device tests and document device-specific limits. Studio Run/service activation and independent-clock alignment are not included by this refresh. |
| V0–V5 — Video source/sink | Numeric and Full HD media evidence delivered, 2026-11-18; production providers planned; maintainer/Codex | [V0 report](VIDEO_IO_SPIKE.md): 16 numeric two-host cases. [Full HD FFmpeg report](VIDEO_IO_FULL_HD.md): 5 whole-frame cases, 3,000 decoded frames, synthetic 1080p/30 stability, receiver grayscale and upscaled 720p webcam. Windows MF capture compile/link probe passed. | Review/implement the whole-frame provider/run-owner and authorized media-session contracts; qualify screen capture, compression quality, camera timing and Windows capture/viewing. Native 1080p camera and physical desktop viewing remain unqualified. Production GStreamer adapters are a [0.3.0 goal](RELEASE_0.3.0.md). |
| D0 — Distributed baseline/contract | Partial | C++/Python implementations, shared fixtures and local lab evidence exist; the complete release matrix and required cross-language CI gates are not closed. | Freeze the supported common subset, hosts, profiles, workload/resource thresholds and required CI checks. |
| D1 — Python split-profile parity | Partial | [Python node](../../s7py/README.md) already carries F64 graph streams over mTLS with ACK/RESUME, replay, backpressure and faults. | Add/qualify separate data listeners and trusted-plaintext policy against C++ and shared fixtures, including rejection cases. |
| D2 — Local multi-node lifecycle | Partial | [Three-process lab](../../s7py/tools/transport_lab/README.md) records mixed-language chain/reconnect checks. | Complete fan-out, bidirectional causal topologies after X1–X3, slow consumers, redeploy/restart, sequence gaps and controlled recovery with asserted revisions/state. |
| D3 — Headless administration | Open | Deployment configuration is not node startup/listener configuration. | Review settings read/validate/stage/commit, revisions, atomic persistence, reconciliation and supervisor restart; implement C++/CLI/fixtures and supported Python parity. |
| D4 — Provisioning / remote hosts | Open | Fixture credentials and local lab runs do not establish real provisioning. | Agree profiles/CSR/signing/replacement, deliver headless tooling and qualify at least two physical hosts using independent credentials and both data profiles. |
| D5 — Distributed qualification | Partial | [Synthetic capacity baseline](SAMPLE_TRANSPORT_BENCHMARK.md) and local lab results exist. | Measure the integrated D2–D4 workflows on named hosts: throughput, latency, CPU, memory, queues/logs, recovery and stress/sanitizer/platform cases. |
| X1–X3 — Cross-node feedback | Initial C++ implementation; real-loop qualification pending | [ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027), [implementation/test guide](DISTRIBUTED_CAUSAL_SCHEDULE.md), and [four research rounds](CROSS_NODE_CAUSALITY_PHASE4.md). Shared Studio/s7ctl preflight checks the flattened graph and derives outgoing-first schedules; nodes verify version, wait graph and local dependencies. Serial runtime/memory semantics are retained. | Complete real-loop checks, wider fan-out/resource/revision/failure-prefix qualification and release regressions. Initial distributed scope is one rate domain, without rate changers or coupled numerical regions. |
| U1 — Studio resizing/presentation | Implemented | Resize/routing/undo/save tests; [current mathematical symbols](DIAGRAM_SYMBOLS.md). User reported desktop rendering fine on 2026-11-16. | Final UI regressions; printed geometry/symbols remain U2. Historical text-only scope differs from current rendering. |
| U2 — Diagram/viewer printing | Open | Page metadata is not a print workflow. | Decide output/viewer matrix and tiling/3D capture; implement preview/PDF/printer workflows and qualify coherent snapshots, layout, cancellation/failures and package dependencies. |
| U3 — Software signal generator | Open | UTG932E-inspired interaction is a design reference, not an implementation. | Settle waveform/reset/transition scope; deliver two-channel schema composition, Studio controls and headless/rate/waveform/save/Scope tests. |
| U4 — Certificate viewer | Open | Existing identity summaries are not the full viewer. Headless creation belongs to D4. | Implement accurate parse/verification status and negative cases, then qualify generated credentials through packaged Studio/headless workflows. |
| U5 — Trusted Networks/admin UI | Open | Existing discovery/deployment UI is a baseline. | Consume reviewed D3 services, preserve Studio/diagram/node ownership and qualify endpoint/credential/pin changes and reconnect. |
| T1 — Test grouping and selection | Open; lifecycle correction present | GUI labels exist. The historical missing-`Stopping` test assumption has a correction in current source. | Deliver area/cost labels, path-to-test guidance and preserved full release coverage; record any new timing/stress measurements separately. |
| PKG — Release distribution | Implemented tooling; qualification partial | [Native Linux](NATIVE_LINUX_PACKAGES.md): six RPMs + six DEBs, each 17 focused / 5 headless / 23 full installed checks. [Windows](WINDOWS_BUILD.md): 112 Wine tests / 21 relocated checks. [Collector](../tools/release/README.md). | Rebuild Linux tarball + Fedora 45 RPMs + Ubuntu 26.04 DEBs + Windows ZIP from final source/version; repeat package/relocation checks and qualify native-package upgrades. Keep native Windows sound/desktop limits explicit. |
| Q1 — Final release candidate | Open | Current source/artifacts still use version 0.1.2; historical passes are not final 0.2.0 qualification. | Close agreed open gates or obtain an explicit scope revision; run full configured Linux/Wine regressions, supported cross-language fixtures, local Studio checks and relevant sanitizers; record package checks, provenance, notices, checksums and precise qualification limits for the same candidate. |
| Within-node parallelism / independent hardware clocks | Deferred | [0.3.0 planning](RELEASE_0.3.0_RUNTIME_PARALLELISM.md); no scheduler or adaptive clock-alignment commitment. | No 0.2.0 implementation gate; keep explicit resampling and present hardware-profile limits. |

Distributed delivery order (teren_ukr's D0/D1/D2 track): reconcile D0 and use the
accepted initial X1–X3 implementation for D1/D2 and qualification. The
maintainer/Codex track starts with V0's two-host video spike; video descriptor
integration coordinates with the distributed track. Review D3 before its implementation or U5;
coordinate D4's headless credentials with U4 before remote-host acceptance.
Printing and the schema-backed generator can proceed independently. Close
R7/T1 alongside focused work, then qualify the common release candidate through
PKG/Q1. Numerical research does not need to restart to unblock these tracks.
Ownership and detailed acceptance below retain the existing agreements; no
open feature is removed from the release by this status refresh.

## Ownership and decision boundaries

| Area | Lead | Review boundary |
| --- | --- | --- |
| Causal model, runtime and protocol architecture, release gates | Maintainer and Codex | Agree on semantics and an executable contract before implementations diverge. |
| Distributed transport, multi-node lifecycle, headless administration and Python interoperability | teren-ukr | C++ remains the reference implementation. Changes to its protocol, authorization or causal behavior require maintainer/Codex review. |
| Within-node parallel execution (0.3.0 planning) | Maintainer and Codex | Deferred from 0.2.0. Scheduling, cost and plugin-safety decisions remain open; the indexed serial runtime, with sequential nested schemas and compiled exposed-output bindings, is the reference. |
| Numerical integration and feedback-settling research (P1, evidence delivered) | Maintainer and Codex | Preserve the reports' reproducibility and limits; separately implemented production methods follow their reviewed contract and need final regression. |
| Studio editing, observation and administration UI | Maintainer and Codex | Headless services expose typed operations; Qt remains a client. Coordinate request/result types with the distributed work. |
| Hardware profile, packaging and release qualification | Maintainer and Codex | Keep explicit activation, platform profiles and bounded qualification; collect artifacts from the final source/version. |
| Video capture/output, viewing and compression | Maintainer and Codex | Whole-frame FFmpeg direction with explicit numeric bridges; coordinate frame/codec/media-session contracts with teren_ukr. D0/D1/D2 ownership stays with him. GStreamer adapters are assigned to 0.3.0. |

Small PRs should follow these boundaries. A Python implementation does not
define a new S7 protocol or numerical rule by itself: the language-neutral
contract, C++ reference behavior and shared fixtures must agree. Conversely,
tests should expose a C++ defect rather than copying it into Python.

The [high-rate sample transport plan](SDR_TRANSPORT_IMPROVEMENT_PLAN.md), proposed
2026-11-12, stages descriptor validation, synthetic throughput measurement and a
reviewed frame/metadata/buffer contract before block execution changes. It uses
these ownership boundaries but does not itself expand the 0.2.0 release gate or
approve the hardware-clock and scheduler decisions deferred to later work.
The frame service, scalar boundary elements and cross-language fixture slice
have since been implemented; their limits are recorded under F1 above.

## Distributed goal and architecture

The goal is a qualified workflow for several node processes on one host and
at least two real hosts, including mixed C++/Python nodes where the documented
common subset permits it. Deployment, streaming, discovery, configuration,
restart and recovery must form one observable lifecycle. The release must state
which graph sizes, rates, node counts, operating systems and resource limits
were measured; a local two-node smoke test is a baseline, not remote acceptance.

Keep these existing boundaries:

1. `s7core` validates, compiles and executes each node's causal graph. A network
   boundary is explicit `NetSink`/`NetSource` behavior; packet arrival time does
   not determine graph causality. The scheduler may change only under a separate
   within-node contract.
2. `s7transport` owns sessions, wire framing, stream ordering, ACK/RESUME,
   backpressure and data-security enforcement. `s7net` owns the network
   elements and uses transport services; `s7runtime` owns node lifecycle,
   deployment and control policy. `s7ctl` and Studio are clients.
3. Control mutations require the transport-verified `ControlPeer`, control
   purpose EKU and pinned fingerprint. Split control/data listeners keep mTLS
   control and explicit encrypted or trusted-plaintext data. There is no
   downgrade fallback or authorization by JSON trust labels.
4. Studio discovery credentials, diagram `runtime_nodes`, and runtime listener
   settings have different owners. `GET_NETWORK_CONFIGURATION` returns the
   deployed diagram, not node startup settings. New administration operations
   need their own versioned request/result contract, validation, revision and
   idempotency rules. A restart is performed by an identified supervisor; the
   runtime cannot promise to restart its own process after it exits.
5. A completed step is the observation boundary. Network metrics and faults
   report identity, revision, stream and sequence without mutating the graph.
   Existing payload replay across a crash is not guaranteed; any broader
   durability claim requires a separate retention and acknowledgement design.

The [split transport contract](SPLIT_TRANSPORT.md),
[control protocol](../../doc/S7_Control_Protocol_MVP.md),
[transport architecture](S7_Transport_Architecture.md) and
[runtime architecture](../../doc/S7_Runtime_Architecture.md) are starting
references. The transport architecture contains future proposals; this plan
does not add NAT traversal, relays, consensus, automatic failover, remote
observation or implicit multi-rate conversion to 0.2.0.

## Development steps for the distributed work

Each step should produce a reviewable PR with the changed contract, focused
tests and stated limits. Validate the C++ reference contract and shared
protocol fixtures before claiming Python conformance.

The status matrix above records current progress. These steps retain the
acceptance contract; D1 and D2 start from implemented data streams and recorded
local tests, not from a control-only Python node.

| Step | Remaining deliverable and acceptance |
| --- | --- |
| D0. Baseline and contract | Inventory the C++/Python common subset and missing cases. Record supported hosts, security profiles, resource and performance measures. Turn the control/wire fixtures and Python checks into required CI gates. Write the multi-node acceptance matrix before changing behavior. |
| D1. Python split-profile parity | Extend the existing F64 graph data plane with the documented split data listener/client and explicit encrypted/trusted-plaintext policies. Interoperate with a C++ node; verify identity/policy rejection, order, ACK/RESUME, bounded send log, disconnect and explicit fault. Shared frame-service conformance does not establish deployed split-profile support. |
| D2. Local multi-node lifecycle | Exercise at least three processes, including a C++/Python mix: fan-out, bidirectional streams, different wall-progress rates, redeployment, restart, sequence gap and controlled recovery. Give each node a separate identity/state directory. Assert the committed revision and stream state after each transition. |
| D3. Administration contract and headless implementation | First review a protocol design for read/validate/stage/commit of node settings, optimistic revisions, atomic persistence, apply timing, lost-connection reconciliation and supervisor-owned restart. Then implement the C++ reference, CLI and shared fixtures; add Python parity for the supported subset. Test unauthorized requests and changed endpoint/credential/pin cases. Do not reuse deployment `GET_NETWORK_CONFIGURATION` for node settings. |
| D4. Provisioning and remote hosts | Agree certificate profiles, signing/CSR workflow, key protection and replacement behavior. Implement a headless credential tool and use independently generated credentials on at least two physical hosts. Verify mTLS control and both explicit data profiles, restart/reconnect and packaging without test-fixture keys. Studio's certificate viewer belongs to the UI track. |
| D5. Qualification | Measure throughput, latency, CPU, memory, queue/log occupancy and recovery time on named hardware for representative topologies. Run stress/sanitizer and platform checks, document failures and limits, and close the acceptance matrix. Keep constrained-device results separate until measured there. |

D0 must define the supported subset and missing cases for the remaining D1
work. D2's existing local chain/reconnect evidence does not cover its full
lifecycle matrix. Within-node parallelism is outside the 0.2.0 delivery
path and is recorded for 0.3.0 planning. D3's protocol review precedes its
implementation. D4's generated credentials
precede claims about remote-host provisioning. Studio's settings and node UI
should consume the reviewed D3 service rather than introduce a second protocol.

<a id="numerical-research-milestones--p1-planned"></a>

## Numerical research milestones — P1 evidence delivered

The maintainer and Codex own N0–N4. The work was conducted on
`research/numerical-feedback-0.2.0` independently of D0–D5 and Studio delivery.
The [release scope](RELEASE_0.2.0.md#numerical-integration-and-feedback-settling-research--p1-reports-delivered)
retains the original model assignment, architecture constraints and acceptance gate;
[open problems](OPEN_PROBLEMS.md#simulation-accuracy--numerical-research-for-020)
records unresolved decisions. Existing indexed serial execution is the
performance baseline, not a pending numerical milestone.

| Milestone | Delivered evidence and review boundary |
| --- | --- |
| N0 — Experimental specification | [N0 contract](NUMERICAL_FEEDBACK_RESEARCH_N0.md) fixes updates, held inputs, publication/failure semantics, model/timestep/reference matrix and trial eligibility; [Phase 3 specification](NUMERICAL_INTEGRATION_PHASE3_SPEC.md) adds the real-graph substep/Gauss experiments. |
| N1 — Experimental implementations | Separate Python pure-graph and C++ real-graph research runners, linked in the reports, implement bounded trials with explicit nonfinite/nonconvergence failures. They are research tools, not the production executor. |
| N2 — Numerical validation | [Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md) covers oscillator, pendulum, Van der Pol, Lorenz and control cases; [Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md) records the corrected 284-case rerun and extends linear structure; [Phase 3](NUMERICAL_INTEGRATION_PHASE3_REPORT.md) records 246 oscillator/RC/RLC cases. Apply only the reports' stated references, envelopes and exclusions. |
| N3 — Performance analysis | Phase 1 direct Release runtime baselines, Phase 2 corrected total-evaluation/equal-work analysis and Phase 3 error-versus-work measurements. Historical Python timings and synthetic solver counts do not establish general production throughput or a full memory bound. |
| N4 — Research conclusions | All three reports contain recommendations, negative results and limits. Backward-Euler feedback trials were not recommended as a generic refinement; generic TPT/algebraic-loop support remains unqualified. Phase 3 motivated a separate production design. |

The research evidence is delivered; retain the branch/source provenance,
reproduction commands and failed experiments. In particular, Phase 2 resolves
the historical Phase 1 provenance/cost ambiguity. A production change has a separate
review: demonstrated benefit, numerical and causal-semantic approval, preserved
existing behavior or an explicit mode, regression/compatibility checks and an
explicit maintainer decision. No new integrator or settling mechanism is a
required production capability for 0.2.0.

The separate production work originally proposed on
`feature/coupled-integration-0.2.0` is now implemented and included in the 0.1.2
baseline: opt-in F64 Euler substeps and coupled two-stage Gauss–Legendre. Its
[numerical and causal contract](COUPLED_INTEGRATION.md) and
[implementation design](COUPLED_INTEGRATION_0.2.0_DESIGN.md) define the supported
graphs and failure/publication behavior. Default Euler and specialized
trapezoid contracts remain in place. Final regression of these methods is NI/Q1;
no additional production algorithm is required to close N0–N4.

## PR review agreement

The maintainer requests a Codex review of every distributed PR before merge.
Each PR should identify its D-step, the contract or fixture it uses, C++
reference impact, Python subset impact, deterministic test results and
operational limits.
Review checks include:

- causal order, step boundaries, model time and stream sequence after faults;
- TLS peer identity, authorization, explicit data policy and no fallback;
- bounded queues, cancellation, graph replacement, shutdown and restart;
- idempotent control operations, revision conflicts, atomic persistence and
  reconciliation after lost replies;
- C++/Python interoperability through the same fixtures, plus local and real
  process tests where the PR claims them;
- build, sanitizer, package and platform evidence appropriate to the change.

No PR is accepted solely because it builds or because a Python-only test
passes. A new wire or control field needs a documented contract and conformance
fixture. A cross-module numerical or scheduler change needs the simulation
track's review as well. Reviews occur when PRs are submitted; this plan does
not establish an unattended monitor.

## Our parallel work

1. **Architecture and development plan:** close the 0.2.0 scope and acceptance
   matrix first. Audit graph replacement, stream ownership and shutdown; retain
   the existing indexed serial measurements and unresolved within-node parallel
   questions for 0.3.0 planning. Nested-schema exposed outputs are bound once
   at compilation (done 2026-10-31); nested schemas stay sequential in 0.2.0,
   and their parallel scheduling is a possible 0.3.0 research topic. Review D3 administration semantics and
   integration boundaries before implementation, keeping the language-neutral and C++
   documents aligned.
2. **Simulation accuracy, research and Studio usability:** extend independent
   numerical references and supported envelopes for saved models before
   changing execution. Preserve the delivered N0–N4 reports and separately
   implemented methods without adding a global solver. Any further numerical
   adoption needs a new reviewed proposal. Preserve diagram sizing and improve
   printing and instrument controls through focused Studio
   owners; build the signal generator from schema elements with headless tests
   and Studio controls. Certificate viewing and network settings UI consume
   reviewed headless contracts from the distributed track.

## Model time and rate consistency — 0.2.0 tasks

Decided 2026-10-31 ([ADR-022](../../doc/S7_Architectural_Decisions.md#adr-022);
element classes in the [sample rate model, section 8](../../doc/S7_Sample_Rate_Model.md#8-model-time-and-element-rate-classification-2026-10-31)).
Owners: maintainer and Codex.

| Step | Deliverable | Status |
| --- | --- | --- |
| R1 | Model time checked against every resolved element rate; rational model-time rate for deployments, stream rates and real-time pacing; network stream elements declare their rate. | Implemented |
| R2 | Rate-following elements adopt their domain's rate; Studio stops rewriting `dt`/`sample_rate_hz`. | Implemented |
| R3 | Durations in time units (seconds) with loader migration from stored sample counts; refuse a duration that is not a whole number of samples. | Implemented |
| R4 | Design rate for `FirFilter`/`IirFilter`; refuse to run at another rate. | Implemented |
| R5 | Rate domains in `NodeRuntime` and the `Resampler` element: orders 0/1/2 (default 0), pass-through at equal rates, causal interpolation, anti-aliasing when lowering the rate. | Implemented |
| R6 | Studio: the run dialog's `dt` drives every rate; mismatch errors suggest a `Resampler`; palette entry; user documentation. | Implemented |
| R7 | Review C++ tests and fixtures with explicit `dt`/`sample_rate_hz` pins; use run/domain-rate adoption where rate following is intended, while retaining explicit pins in tests that verify pin/mismatch behavior. | Open |

Feedback across rate domains is rejected in 0.2.0 and recorded as research in
[open problems](OPEN_PROBLEMS.md#ratedt-inference-and-validation--implemented-multi-rate-execution-deferred).
The initial device-paced hardware owner is implemented (H1). Alignment of
independent hardware clocks, drift correction and networked-audio clock
coordination remain later research; hardware support as a whole is not deferred.

## Nested schemas behave like their inlined contents — 0.2.0

Decided 2026-10-31 ([ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024)).
The compiler flattens every nested schema into the parent graph, so
feedthrough is per path and a loop may close through causal memory inside a
nested schema; a nested schema's own rate is declared by its ports and seeds
its elements. Flattening is local to one node's compilation and never
crosses a network boundary; a stream may start or end at a nested schema's
exposed port. Implemented in current source (`s7core/src/flatten.cpp`) and
included in the 0.1.2 baseline; the earlier feature-branch/PR #10 note is
historical. Open limit: loops across nodes are not validated as a whole
([open problems](OPEN_PROBLEMS.md#nested-schemas--flattened-adr-024-one-limit-recorded-2026-10-31)).

## Feedback loops across nodes — 0.2.0 task

Recorded 2026-10-31 as a 0.2.0 task by the maintainer
([open problem](OPEN_PROBLEMS.md#nested-schemas--flattened-adr-024-one-limit-recorded-2026-10-31)).
Deployment resolves types over the whole diagram, but each node validates only
its own part, in which `NetSource` and `NetSink` are direct-feedthrough stream
ends. A loop that leaves a node through one stream and returns through another
is never checked for causal memory; without memory on its path, the nodes wait
on each other until `max_wait_ms` expires.

| Step | Deliverable | Status |
| --- | --- | --- |
| X1 | Decide whether a stream may ever count as causal memory, or is always a direct wire (affects whether a cross-node loop needs an explicit `Delay`), and record it in an ADR. | Accepted: ADR-027, streams are direct boundaries |
| X2 | Check cycles over the whole diagram before `PREPARE_DEPLOYMENT` (deployment client, used by Studio and `s7ctl`), treating each stream as decided in X1; a loop without causal memory refuses the deployment, naming the elements and streams on it, before any node is prepared. Nested schemas are flattened for the check (ADR-024). | Implemented in shared client, receiver and compiler |
| X3 | Tests: a refused cross-node loop without memory, an accepted one with a `Delay` on either node, a loop through a nested schema's internal memory, and Studio's deploy path reporting the refusal. | Automated checks implemented; real-loop qualification pending |

Research started 2026-11-17 before choosing X1: the
[literature record](CROSS_NODE_CAUSALITY_RESEARCH.md) includes textbooks,
published papers and MIT/Stanford teaching notes. The
[initial experiments](CROSS_NODE_CAUSALITY_DESIGN.md),
[ACK/transport follow-up](CROSS_NODE_CAUSALITY_PHASE2.md),
[performance comparison](CROSS_NODE_CAUSALITY_PHASE3.md) and
[outgoing-first/queue C++ comparison](CROSS_NODE_CAUSALITY_PHASE4.md) preserve
separate evidence and applicability limits. The maintainer then authorized
implementation of the strongest candidate (2026-11-17). ADR-027 records that
direction; the [implementation guide](DISTRIBUTED_CAUSAL_SCHEDULE.md) describes
actual behavior and tomorrow's checks. Research results do not qualify general
distributed liveness or device deadlines. D0–D5 and broader release acceptance
remain separate work.

## SDR sample formats — 0.2.0 tasks

Decided and implemented 2026-10-31 ([ADR-023](../../doc/S7_Architectural_Decisions.md#adr-023)). S7's integer and complex integer types exist for SDR I/Q
([wire protocol, section 8](../../doc/S7_Network_Wire_Protocol_v1.md)). The
formats the common devices' host libraries deliver:

| Device | Converter | Host sample format | S7 type |
| --- | --- | --- | --- |
| RTL-SDR (RTL2832U) | 8 bit | unsigned offset binary; measured zero near 127.4 (gr-osmosdr), not 128 | CU8 |
| HackRF One | 8 bit | signed 8-bit I/Q, interleaved, receive and transmit | CI8 (exact; normalized c/128) |
| ADALM-Pluto (AD9361/AD9363) | 12 bit | two's complement; receive `le:S12/16>>0` (low 12 bits, sign-extended), transmit `le:S16/16>>0` (top 12 bits used) | CI12 |
| LimeSDR | 12 bit | `I16` (12 bits shifted to the top, per community reports); `I12` packed 3 bytes per I/Q pair on the link | CI12 |
| USRP (UHD) | 12–16 bit | link formats sc16, sc12 (B2xx), sc8 (B2xx/N2xx, scaled by `peak`) | CI16, CI12, CI8 |
| Airspy | 12 bit | `INT16_IQ` by default; optional 12-bit packing on USB | CI12 |

| Step | Deliverable | Status |
| --- | --- | --- |
| S1 | CU8: `Convert` in `value` mode to or from CU8 is refused (an offset-binary code has no plain numeric value; it used to clip half the range); normalized (c−128)/128 is a documented convention, with DC removed explicitly in the graph. Signed↔unsigned normalized conversions are refused at validation instead of at the first step. | Implemented |
| S2 | SDR elements publish samples at the converter's native width: CU8, CI8, CI12 or CI16. 12-bit samples are never published as CI16 (low-aligned Pluto receive data would read 1/16 of full scale, about −24 dB); normalized `Convert` CI12 → CI16 gives the high-aligned transmit format. The rule for every SDR element, in the element developer guide, with conversion tests. (Decided first as "12 bits at the top of CI16"; superseded by CI12 once S3 was approved.) | Implemented |
| S3 | `CI12` (`0x0207`), a pair of I12 packed on the wire (3 bytes per I/Q pair, 25% less than CI16), with the arithmetic of the other complex integer types. Nodes advertise the types they transport in `NODE_INFO.data_protocol.signal_types`; a node without the list has the original catalog (ids up to `0x0206`), and the deployment client refuses a CI12 stream to a node that does not list it. Wire fixtures include CI12. | Implemented |

The next joint review should close the remaining D0 subset/qualification
decisions, X1 semantics and D3 administration design questions. The status
matrix above records work already delivered and the open acceptance gates;
the detailed scope and evidence documents retain their own limits.
