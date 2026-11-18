# S7 0.2.0 — reproducible causal execution

Scope revision agreed by the maintainer on **2026-11-18**. This replaces the
earlier feature-accumulation plan for 0.2.0; the historical assignments and
decisions remain available in Git history and in their linked design/research
documents. The source version is still 0.1.2 until an actual release candidate
is prepared. An implemented feature or passing historical experiment does not
by itself qualify a 0.2.0 release.

The single release objective is:

> **Demonstrate reliable, reproducible causal simulation on local and
> distributed nodes, with numerical validation, explicit execution semantics,
> and integrated observation.**

**A useful engineering example is the centerpiece of 0.2.0.** Build a
signal-processing or simulation diagram, run parts on two physical headless
hosts, and inspect reproducible results. Distribution is a means, not the
purpose of the demonstration. The existing numerical, runtime, transport
and Studio capabilities supply the foundation. Multi-host feedback loops
remain valuable scheduler tests, **not** a pre-MVP showcase or release
blocker. Remaining work is qualification and necessary fixes for the
four release gates. The maintainer also approved a substantial
**MIT-aligned educational laboratory** workstream for signals, systems and
control. This increases the educational ambition without silently expanding
the runtime/hardware feature set.

The [work plan](RELEASE_0.2.0_WORKPLAN.md) is the authoritative live
[status/acceptance matrix](RELEASE_0.2.0_WORKPLAN.md#status-and-acceptance-matrix).
This file states the release contract, not a claim that any gate has passed.

## Supported initial profile and invariants

- **Local execution:** the C++ reference runtime and Studio's embedded runtime;
  sequential, compiled, indexed dispatch with explicit causal-memory
  publication. Nested schemas behave like their inlined contents
  ([ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024)).
- **Distributed execution:** the initial static, single-rate causal schedule
  across at least two physical Linux hosts with C++ nodes. Cross-node feedback
  must contain explicit causal memory. The graph is validated before PREPARE;
  nodes verify schedule capability, membership, dependency order and the
  combined stream wait graph. No dynamically inferred network dependencies.
  See [ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027) and the
  [implemented schedule contract](DISTRIBUTED_CAUSAL_SCHEDULE.md).
- **Language interoperability:** C++/Python control and F64 stream
  interoperability within an explicitly documented common subset. A mixed
  C++/Python topology is an acceptance case, but Python participation in
  *cross-node feedback scheduling* is not required unless that capability is
  independently implemented and qualified. Java's existing frame/wire
  conformance does not imply Java control-plane parity.
- **Security and operations:** existing identity-bound mTLS control,
  certificate-purpose and pinned-fingerprint authorization, explicit data
  security profiles and stream ordering/backpressure/faults. Use documented
  manual or existing-script provisioning and startup configuration for the
  release experiment. No new remote administration or PKI application is
  required.
- **Measurement:** model time, sample rate, physical clock/pacing, transport
  timing and wall-clock latency remain separate concepts. A numerically
  correct result does not prove a real-time deadline. Supported workload
  sizes, host specifications, achieved rates, resource usage and exclusions
  must accompany the release evidence.

A release example must not require hardware adapters, new video providers,
specialized actuators or private credentials. The current hardware and frame
services remain available in their separately documented bounded profiles.

## MIT-aligned educational laboratory collection

Develop **original S7 teaching labs** aligned by topic with MIT 6.3000,
6.3100, historical 6.003 and 6.3010/6.011. The
[education overview](../education/README.md),
[48-exercise curriculum map](../education/COURSE_MAP.md),
[primary-source record](../education/SOURCES.md),
[verification standard](../education/LAB_STANDARD.md) and
[Codex implementation plan](../education/IMPLEMENTATION_PLAN.md)
define the syllabus and execution requirements.

The initial **0.2.0 qualification target is 24 specifically named core
labs** in foundations, signal processing and control. The other 24
proposed labs are later/stretch candidates. Reuse canonical existing
`examples/` diagrams before creating new ones. No listed lab is
automatically qualified by its existence in the repository.

An accepted educational lab must have precise source/topic attribution,
student instructions and model conventions, exact saved-diagram headless
execution, an independent analytical or numerical reference, a deliberate
failure/limitation case, tested Studio open/run/observe/edit/rerun and
release-package access. If a target is blocked, report it explicitly;
the maintainer may revise the named target rather than claiming false passes.

SP06, a three-tap FIR lab, is the preferred candidate for the practical
G2 two-host engineering demo **after** its local numerical reference passes.
Advanced image processing, physical hardware and inference research do not
implicitly become 0.2.0 feature gates.

S7 is an independent MIT-aligned resource, **not** an official MIT course
resource or MIT-endorsed product. Avoid reproducing copyrighted textbook
problems, diagrams and solution keys.

## Acceptance gates

All four gates are mandatory on the **same final source revision and candidate
artifacts**, except where an explicitly documented platform-specific exclusion
applies. Required pass/fail criteria and evidence owners are in the
[work plan](RELEASE_0.2.0_WORKPLAN.md).

### G1 — Local simulation and numerical correctness

1. Run the current numerical/reference matrix on local C++ graphs, including
   linear and nonlinear examples (oscillator, pendulum, Van der Pol, Lorenz and
   feedback/control where supported). Use independent analytic solutions,
   discrete recurrences or external reference solvers, with each model's
   documented metrics and tolerance envelope. No impossible long-horizon
   pointwise equality requirement for chaotic systems.
2. Regress indexed serial execution, nested-schema flattening, types, exact
   rate handling and explicit resampling, causal-memory publication,
   numerical-failure rollback and observation semantics.
3. Regress production integrators, including opt-in F64 Euler substeps and
   coupled Gauss–Legendre, against their defined eligible regions and
   independent references. The research prototypes do not become production
   methods merely by being tested.
4. Demonstrate repeatable recorded observations that identify the run,
   channels and model steps without modifying execution.
5. Qualify the approved educational core against **actual saved diagrams**
   and independent reference calculations, explicit initial/sample
   conventions and negative teaching cases. Historical examples and
   screenshots do not establish educational acceptance.

The [numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md),
[simulation semantics](SIMULATION_MODEL.md), and
[integrator review](INTEGRATOR_REVIEW_2026-11-17.md) supply existing contracts
and evidence. Historic test counts are not the release-candidate result.

### G2 — Useful two-host execution and causal correctness

1. Demonstrate a practical, **headless two-host signal-processing or
   simulation chain** with deterministic input, meaningful processing/model
   computation and a recorded numeric result. Prefer the independently
   qualified educational SP06 (three-tap FIR) source → filter → measurement
   lab, or approve a comparably useful existing chain. Hardware acquisition
   may be shown additionally but cannot be required.
2. Validate the result against an **independent analytical or numerical
   reference**, not only a second S7 execution. Compare with an equivalent
   local chain where meaningful. Record sample rate/count, signal ordering,
   measured values and known tolerances.
3. Preserve tests for static causal scheduling, including initialized-memory
   cross-node feedback, nested memory, direct-feedthrough cycle rejection,
   stale/invalid order, missing capability and explicit packet/batch
   semantics. Local independent headless processes are enough for required
   scheduler regressions. A cross-host feedback experiment is **recommended
   extra evidence**, not a release gate. Neither three hosts nor four
   processes are required for the flagship example.
4. Record operating-system and hardware profiles, graph and input provenance,
   security mode, measured outputs, throughput, lag, CPU, memory and queue
   occupancy. Distinguish model-time equivalence from wall-clock performance.

The initial supported distributed profile remains single-rate and serial
per node. Use existing observation or recorded-output paths: **a new remote
observation protocol is not a 0.2.0 prerequisite**. If Studio cannot inspect
remote data live, document the existing recording/inspection workflow and
the limitation rather than claiming live observation.

The [cross-node scheduling research](CROSS_NODE_CAUSALITY_PHASE4.md) and
[implementation guide](DISTRIBUTED_CAUSAL_SCHEDULE.md) remain normative
for that component's supported behavior. Automated feedback correctness
is distinct from the practical two-host product demonstration.

### G3 — Observable network lifecycle and failure behavior

1. Exercise authenticated deployment, active revision verification,
   disconnect/reconnect, fan-out, a slow consumer, bounded backpressure,
   explicit stream faults, controlled recovery, process restart and
   redeployment. Each transition must assert the resulting graph/stream
   state and observed sequence continuity or gap.
2. Demonstrate a mixed C++/Python topology using the frozen shared subset,
   with the supported control/data profiles and required fixtures. Qualify
   Python split-data-listener and trusted-plaintext behavior before claiming
   that parity; otherwise document and exclude that profile explicitly.
3. Test unauthorized control requests, wrong identity/purpose/pin, incompatible
   capability and prohibited security-profile fallback. Do not promote fixture
   credentials to a deployable provisioning claim.
4. Make the **failure boundary** observable: a failed local step does not
   publish success; no missing network value is invented; externally sent
   packets are not rolled back by local step rollback. Document the recovery
   action and any committed-prefix limitations.

This gate establishes a supported **fault-handling contract**, not
distributed-step atomicity, durable replay through crashes, exactly-once
physical actuation or bounded real-time response under arbitrary failures.
The current [network transport contract](../../doc/S7_Network_Wire_Protocol_v1.md)
and [multi-node lab](../../s7py/tools/transport_lab/README.md) define baseline
behavior, not completed acceptance.

### G4 — Reproducible release, Studio and package qualification

1. Run the **full applicable** CTest/numerical, network and protocol-fixture
   suites for the final candidate. Include Studio editor/observer workflows
   and targeted sanitizers. Partial local runs and prior reports must be
   labelled as such. Focused test selection may accelerate development but
   cannot shrink release coverage.
2. Build and verify the existing intended distribution matrix from the same
   source/version: Linux portable tarball, Fedora 45 RPMs, Ubuntu 26.04 DEBs,
   and the Windows x86-64 ZIP in its documented sound-only profile.
   Relocation/package tests, licensing/notices and checksums are mandatory.
   Wine qualification must not be described as native Windows hardware testing.
3. Publish one **reproducible two-host engineering demonstration**: diagram
   and application goal, input and processing stages, independent reference,
   headless deployment and observation/recording commands, measured output,
   timing/resource data, process/link failure behavior and explicit
   limitations. Keep private credentials and device identities out of Git.
   Do not require a new remote-observation API to produce the evidence.
4. Verify educational diagrams, guides and course navigation in
   supported installed/relocated artifacts. Record Studio evidence
   separately from headless numerical passes.
5. Publish release notes with supported operating systems, tested graph and
   rate envelopes, known regressions, unsupported features and reproducible
   test/benchmark provenance, including the qualified education index.

See [native Linux packages](NATIVE_LINUX_PACKAGES.md),
[Windows build evidence](WINDOWS_BUILD.md), and
[release collection](../tools/release/README.md). Existing development
packages and historical passes are not 0.2.0 final-candidate artifacts.

## Baseline already in the source; preserve in regression

These are **not** requests to reimplement existing functionality:

| Area | Existing scope and evidence |
| --- | --- |
| Indexed serial dispatch and nested-schema flattening | [Runtime performance record](RUNTIME_PERFORMANCE_ARCHITECTURE.md), [ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024) |
| Numerical integration and feedback research | [N0](NUMERICAL_FEEDBACK_RESEARCH_N0.md), [Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md), [Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md), [Phase 3](NUMERICAL_INTEGRATION_PHASE3_REPORT.md), [production coupled contract](COUPLED_INTEGRATION.md) |
| Exact rates, supported numeric formats and explicit conversions | [Rate model](../../doc/S7_Sample_Rate_Model.md), [0.1.2 baseline](RELEASE_0.1.2_NOTES.md) |
| Typed transport, frame services and cross-language fixtures | [Frame transport](FRAME_TRANSPORT.md), [frame elements](FRAME_ELEMENTS.md), [interoperability](../../doc/S7_Frame_Interoperability.md) |
| Audio/SDR hardware owners and initial evidence | [Hardware I/O](HARDWARE_IO.md), [live tests](HARDWARE_IO_LIVE_TESTS.md) |
| Studio mathematical symbols, viewers and analysis | [Symbols](DIAGRAM_SYMBOLS.md), [analysis testing](STUDIO_ANALYSIS_TESTING.md), [viewer guide](STUDIO_OBSERVATION_VIEWERS.md) |

<a id="numerical-integration-and-feedback-settling-research--p1-planned"></a>

### Numerical research record (historical 0.2.0 assignment)

The N0–N4 reports, negative results and independent reference data are
**delivered research evidence**. Forward/backward Euler, trapezoid, symplectic,
local iterative settling and TPT hypotheses retain their distinct mathematical
contracts. Generic algebraic-loop solving was **not** established. The
production subset is explicitly limited by
[COUPLED_INTEGRATION.md](COUPLED_INTEGRATION.md) and
[INTEGRATORS.md](INTEGRATORS.md). No new solver, general TPT implementation or
feedback-settling algorithm is a 0.2.0 release gate. The original assignment
and its experiment matrix remain in Git history; the [N0 contract](NUMERICAL_FEEDBACK_RESEARCH_N0.md)
and subsequent reports are the canonical experiment records.

## Deferred work and exclusions

The following items are **not 0.2.0 completion gates**. Their design records
remain valid and are linked from the [0.3.0 candidate backlog](RELEASE_0.3.0.md);
deferral does not promise delivery of every item in 0.3.0.

| Deferred item | Boundary |
| --- | --- |
| Production VideoSource/VideoSink, screen/window capture, codecs and native Windows video | Current [whole-frame FFmpeg experiments](VIDEO_IO_FULL_HD.md) remain evidence, not shipping providers |
| Remote node-settings read/stage/commit, supervisor restart and Trusted Networks UI | Existing secure deployment/control remains in scope; new administration APIs do not |
| Certificate viewer, full node/Studio certificate-creation program and renewal UX | Existing independently provisioned mTLS credentials are enough for the release demo |
| Diagram/viewer printing and instrument-style software signal generator | Existing Studio editing/observing stays in scope |
| Test area/cost-label taxonomy and developer selection tooling | Full release regressions remain mandatory |
| Intra-node/nested-schema parallelism; general multi-rate distributed scheduling | Remain research/architecture decisions, not hidden release requirements |
| Coherence-domain alignment, automatic hardware-clock correction, remote observation APIs and crash-durable stream journals | Not promised by this release |

<a id="software-signal-generator-element"></a>

The historical 0.2.0 [signal-generator proposal](STUDIO_INSTRUMENT_CONTROLS.md)
and original instrument-style requirements have moved to
[the 0.3.0 candidate backlog](RELEASE_0.3.0.md#studio-instruments-printing-and-ui).

## Release decision

**Release 0.2.0 only after G1–G4 pass**, or after the maintainer explicitly
changes this contract and publishes the resulting exclusions. Do not postpone
a qualified release for unrelated new features. Do not interpret a research
result, a single platform check or an attractive Studio demonstration as proof
of distributed reliability. Large cross-host feedback loops are
additional scheduler evidence, not pre-MVP product milestones.
