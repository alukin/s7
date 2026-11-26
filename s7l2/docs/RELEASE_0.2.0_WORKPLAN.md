# S7 0.2.0 work plan — qualification and remaining delivery

**Scope revised 2026-11-18; printing added on 2026-11-20; local vector ports/wires, Waterfall and a basic Video Player added as mandatory 0.2.0 targets by the maintainer on 2026-11-23. Planning and qualification remain open.** The [release contract](RELEASE_0.2.0.md)
supersedes older feature assignments. The primary acceptance example is a
**useful two-host engineering workflow** — source, processing/model,
independent numeric validation and observation — not a cross-host feedback
stress test. Feedback loops remain automatic scheduler regressions; physical
cross-host feedback is optional. The source still identifies as 0.1.2 and
previous experiments are not final candidate qualification.
The maintainer also approved an MIT-aligned education workstream:
[48 proposed exercises](../education/COURSE_MAP.md), with
[24 named 0.2.0 core labs](../education/IMPLEMENTATION_PLAN.md)
to be independently qualified, not counted as completed by declaration.

This file is the **single live status and acceptance record** for 0.2.0.
Keep detailed algorithm derivations, research data, architectural discussions
and historical implementation plans in their existing documents and Git
history. Do not duplicate them here or silently convert experiments into
production guarantees.

**Education delivery update, 2026-11-19:** the maintainer requested new
purpose-built diagrams rather than reuse of older examples. The
[core manifest](../education/CORE_MANIFEST.md) audits the 24 core targets and
records FN01–FN06, CT01–CT08, SP01/SP03, SP05–SP11 and SP14 (24 developed core labs). Their
[development evidence](../education/EVIDENCE.md)
does not close an educational or release gate.

## Status and acceptance matrix

Definitions: **Implemented** means code is present, not release-qualified;
**Evidence delivered** means bounded research/test artifacts are available;
**Partial** means some but not all acceptance cases are recorded;
**Open** means an acceptance decision, implementation, fix or test remains;
**Deferred** means explicitly excluded from 0.2.0. Historical passing tests
must not be reported as final-candidate tests.

| Item / release gate | Status at scope revision | Existing evidence | Required remaining acceptance | Lead |
| --- | --- | --- | --- | --- |
| **G1 — Local numerical and semantic regressions** | Implemented baseline; final gate open | [Numerical matrix](NUMERICAL_VALIDATION_MATRIX.md), [integrator review](INTEGRATOR_REVIEW_2026-11-17.md), [0.1.2 notes](RELEASE_0.1.2_NOTES.md) | Complete final-candidate numerical/causality/type/rate/rollback/reference tests, including supported coupled methods; preserve recorded envelopes | Maintainer/Codex |
| **G1/V — Local vector-valued signal ports and wires** | **Mandatory; D7 decision open; implementation/qualification open** | [Array/zero-copy architecture](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md), [AV00 scalar baseline](ARRAY_VECTOR_AV00_BASELINE.md) and independent FFT/convolution implementations | Rank-1 typed array ports/wires, minimal rank-2/3 U8 frame values, CPU bounded leases, typed shape validation, zero-payload-copy local fan-out, compiled binding, commit/rollback, nested-schema and scalar regression; deterministic saved diagrams and negative tests | Maintainer/Codex |
| **G1 / EDU — MIT-aligned teaching laboratories** | Curriculum proposed; per-lab qualification open | [course map](../education/COURSE_MAP.md), [existing examples](../examples/README.md), [lab standard](../education/LAB_STANDARD.md) | Create new educational diagrams using older examples only as references; qualify the agreed 24 named core labs with independent oracles, failure/limitation cases, source attribution, Studio workflows and release-package checks; list blocked cases explicitly | Maintainer/Codex |
| **G2 — Practical two-host engineering workflow** | Headless transport and static scheduler implemented in bounded profiles; end-to-end demo open | [ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027), [schedule contract](DISTRIBUTED_CAUSAL_SCHEDULE.md), [Phase 4](CROSS_NODE_CAUSALITY_PHASE4.md) | Two physical hosts, useful deterministic input → processing → recorded/observed output, independent reference and resource metrics; separate multiprocess feedback/invalid-cycle regressions | Maintainer/Codex, coordinated with teren_ukr |
| **G3 / D0 — Common distributed contract and test matrix** | Partial | C++/Python existing control and F64 data streams; shared fixtures; [transport lab](../../s7py/tools/transport_lab/README.md) | Freeze supported topology, security/transport profiles, host and resource envelopes; record expected pass/fail matrix; make cross-language checks required CI | teren_ukr |
| **G3 / D1 — Python data-profile parity** | Partial | Python F64 mTLS graph streams, ACK/RESUME and fault handling | Explicit split data listener and trusted-plaintext policy plus negative cases with C++; no implied Python cross-node static-feedback scheduling | teren_ukr |
| **G3 / D2 — Multi-node lifecycle** | Partial | Local mixed-language chain and reconnect checks | Headless forward chain, fan-out, slow consumers, gaps, restart/redeploy/reconnect, controlled recovery and revisions; no obligatory multi-host feedback loop | teren_ukr |
| **G3 — Failure and authorization boundary** | Partial | Existing [wire/stream contract](../../doc/S7_Network_Wire_Protocol_v1.md), control authorization and [schedule limits](DISTRIBUTED_CAUSAL_SCHEDULE.md) | Assert unauthorized, wrong-pin, profile mismatch, missing values, non-atomic network sends and explicit faults; document limits and operator recovery | Maintainer/Codex + teren_ukr |
| **G4 — Studio and packaged candidate** | Tooling present; final gate open | [Studio testing](STUDIO_GUI_TESTING.md), [Linux packages](NATIVE_LINUX_PACKAGES.md), [Windows checks](WINDOWS_BUILD.md) | Complete configured Linux/Studio/Wine regressions; candidate artifact rebuilding, relocation, notices, hashes and accurate native-vs-Wine qualification | Maintainer/Codex |
| **G4/W — Waterfall observer** | **Mandatory; D5/D6 and implementation/qualification open** | [Array/zero-copy architecture](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md), existing Studio spectral analysis | Whole 1D FFT/PSD array values from deterministic IQ reference -> independent read-only time-frequency waterfall, axes/window/normalization, bounded history, discontinuity, saved diagram, visual regression and package check; no required live radio | Maintainer/Codex |
| **G4/V — Basic Video Player observer** | **Mandatory; D4/D6 and implementation/qualification open** | [Array/zero-copy architecture](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md), video experiment notes | Generated/recorded U8 grayscale/RGB whole frames via actual local S7 array ports -> play/pause presentation, fit, frame/format status, bounded presentation skipping, image/pixel oracle, diagram reopen and package verification; no camera/codec provider required | Maintainer/Codex |
| **G4/P — Studio diagram/viewer printing** | **Mandatory P01–P09; implementation/qualification open** | [Printing contract](STUDIO_PRINTING_0.2.0.md), existing Studio viewer architecture | Diagram and supported viewer output PDF/printer paths; correct page scale, labels, snapshot, error/cancel, package and supported-platform evidence | Maintainer/Codex |
| **G4 — Reproducible engineering demonstration** | Open | [Distributed schedule smoke cases](DISTRIBUTED_CAUSAL_SCHEDULE.md) and research lab reports | Public practical diagram, independent reference, headless setup, observation/recording workflow, resource metrics and failure/recovery results | Maintainer/Codex + teren_ukr |

**The three new 0.2.0 items above are explicit maintainer-approved scope amendments.** They do not require the complete array-processing roadmap, live camera, video codecs or new remote tensor-wire protocols; follow [the bounded slice](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#5-mandatory-020-minimal-vertical-slice-new-scope).

**Not on the 0.2.0 critical path:** D3 runtime administration and restart
protocol, D4 full credential tooling and remote provisioning UX,
U3 signal generator, U4 certificate viewer, U5 Trusted Networks UI, video
providers V0–V5, general test-label taxonomy T1, within-node parallelism,
automatic coherence/hardware clock alignment and multi-rate distributed
scheduling. Their previous detailed requirements remain in Git history and
linked design documents; the [0.3.0 record](RELEASE_0.3.0.md) is a candidate
backlog, not an automatic commitment to deliver everything in that release.

## Ownership and coordination

- **Maintainer/Codex:** S7 formal semantics, local numerical regressions,
  educational lab development and verification, compiled causal schedule,
  the two-host engineering example, Studio and
  final packaging/release decision. Preserve serial execution and the
  existing production numerical contracts.
- **teren_ukr:** D0 common-subset definition, D1 Python split-profile parity
  and D2 multi-node lifecycle. Own the corresponding cross-language
  fixtures and required checks. Coordinate network/control contract
  changes with the maintainer before implementations diverge.
- **Shared boundary:** C++ remains the reference for execution semantics;
  Python conformance covers an explicitly named subset. Do not make
  Python support for every C++ feature a hidden acceptance criterion.
  Ownership can change by an explicit maintainer decision, not by an
  unnoticed work-plan edit.

No separate workstream may introduce distributed graph semantics based on
packet arrival time. Control identity and authorization remain verified at
the transport and control boundary. Studio is a client of headless services,
not a second simulation/control implementation.

## Remaining work, in dependency order

### New array-valued signals and viewers — mandatory narrow slice (G1/V, G4/W, G4/V)

Follow [array signals and zero-copy design](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md), the [source-based impact inventory](ARRAY_VECTOR_MIGRATION_INVENTORY.md) and the [Codex work plan](ARRAY_VECTOR_CODEX_WORKPLAN.md), not an unrelated frame/media runtime. **Preserve existing scalar element implementations; add new vector elements.** The revised Codex plan divides AV00–AV18 into **individually bounded unsplit or suffixed AVxxa/b/c/d sessions**, each at most five hours including verification and handoff, with mandatory further split if too large; it is not evidence that any gate has passed.

1. Implement local array-valued port descriptors, fixed shapes and CPU lease-backed zero-copy connections while preserving scalar native storage, existing numeric SignalType IDs, causal direct-feedthrough and causal-memory **per-rate-domain** publication/rollback (ADR-021/022); reject unsupported implicit/coupled numerical array trials and standalone array Schema::step(), while supporting flattened nested ports.
2. Provide deterministic array source/pass-through, whole-array VectorDelay, and a frame-valued FFT/PSD path with independent complex-IQ numerical oracle. Verify bounded buffers, fan-out identity, lifetime, shape/type mismatch, forced faults, scalar regression and exact saved-diagram headless execution. Do not rely solely on the existing scalar peak-only FFT plugin.
3. Implement the read-only Waterfall viewer with spectrum-row ordering, frequency/time axes, selectable range and bounded history; independently compare bins and selected display pixels, including gaps and lifecycle changes.
4. Implement the basic Video Player viewer for generated/recorded grayscale and RGB U8 whole frames delivered through S7 array ports. Include presentation-only pause/resume, fit, frame metadata, dropped presentation counters, save/reopen and explicit unsupported-format paths. Camera, decoding, FFmpeg and GStreamer are not 0.2.0 prerequisites.
5. Verify both viewers under G4/P **P08/P09** print/PDF and packaging, with coherent retained snapshots and independent image checks; the **separate P01–P07 printing implementation and qualification remains mandatory**, not hidden in AV17. Record actual source SHA, platform, results and exclusions; a nonblank image is not sufficient acceptance.

**Exit:** G1/V, G4/W and G4/V have tested concrete diagrams, independent references and complete Studio/print/package acceptance, all on the same final candidate as G1–G4. This is new work, not currently implemented.



### Review-driven architecture blockers and parallel execution branches

**Status:** D1 compatibility policy, D2 publication, D3 buffer ownership and D4 descriptor foundation accepted. D5–D7 and release qualification remain open. See the [independent review](ARRAY_VECTOR_INDEPENDENT_REVIEW_2026-11-23.md), [design decision register](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#open-decision-register--explicit-maintainer-approval-required) and [AV dependencies](ARRAY_VECTOR_CODEX_WORKPLAN.md#binding-dependency-dag-and-review-gates-supersedes-original-linear-ordering).

1. **Before invasive work:** AV00 records source SHA, scalar benchmarks, the installed SDK and rebuilt repository plugins, and ADR-021/022/024/029 behavior. Accepted D4 defines the AV01 shape/layout foundation. Accepted D3 defines producer reserves and optional retention. Resolve D2 publication before compiled commit work. D7 sets the standalone Schema boundary. D5 FFT/PSD and D6 provenance gate their dependent features.
2. **Shared runtime foundation:** AV01 -> AV02/AV03 and AV04 may proceed independently after prerequisites; AV05a/b compile slot binding, AV06a/b perform per-domain commit and fault/retained-lease tests. **No public array element before AV06b**. AV07a/b provides whole-array memory, AV08a/b supports **flattened** schema aliases and persistence; AV09a is an earlier *internal* fixture, AV09b public new scalar-preserving vector elements.
3. **Independent viewer branches:** after core safety, AV10a/b (reused FFT backend) -> AV11a/b PSD -> AV15a/b Waterfall; separately AV12 U8 deterministic whole frames -> AV16a/b Video Player. Both use AV13a/b bounded **committed-array** observation and AV14a/b Studio descriptor-aware **observer+ordinary** wiring/registry. **AV12 does not depend on AV15** and a Video Player need not wait for Waterfall.
4. **Observer and rate boundaries:** optional viewer retention has separate execution reserve and bounded drop/cancellation accounting. A whole C32[N] is one causal step, not N sample ticks. Rational graph-domain rate does not imply physical sample rate, sample index, frame identity or PTS. Metadata absence, gaps and propagation must be explicit; scalar Resampler is not an in-block converter. Per-domain output rollback does not undo earlier committed domains, private state or physical I/O.
5. **Printing and qualification:** AV17a/b owns **P08/P09** coherent viewer snapshots, help, packaging and installed SDK evidence; **P01–P07 remains an independent G4/P workstream**. AV18a/b/c/d independently qualify candidate-specific headless/numerical, memory/performance, Studio/complete P01–P09 and SDK/packages. AV completion alone does not close existing G1 numerical/EDU, G2 two-host, G3 lifecycle, G4 packages/printing gates. All evidence uses **one candidate SHA** or records explicit exclusions and approval.

**Required release evidence additions:** shape/rank/stride/overflow and array-trial negatives, zero-copy identity and copy ledger, direct and memory faults and multi-rate retry, tiny-pool producer progress and cancellation, retained lease lifetime and sanitizers, independent FFT/PSD/frequency-axis oracles, U8 grayscale/RGB pixel oracles including padded rows if supported, actual UI image and PDF-page review, baseline and candidate scalar timing and allocations, and repository plugins rebuilt against the installed SDK.

### Education phase — prepare and qualify the curriculum

The [education implementation plan](../education/IMPLEMENTATION_PLAN.md)
owns the detailed Codex handoff. The education track may progress in
parallel with the D0–D2 distributed workstream:

1. Audit the actual diagrams, generators and tests as implementation
   references. Per the maintainer's 2026-11-19 decision, create new
   purpose-built educational diagrams in `education/labs/`; do not
   deliver the older examples as labs. Track gaps in the
   [core manifest](../education/CORE_MANIFEST.md).
2. Verify primary-source topic alignment and any exact book references;
   write original S7 exercises, not copied MIT problems/answer keys.
3. Implement the **FN01–FN06, SP01/SP03/SP05–SP11/SP14 and
   CT01–CT08** core in reviewable lab-family PRs. Each requires
   an independent numeric oracle, tested saved diagram, clear
   initial-state/sample conventions, and one meaningful negative case.
4. Verify Studio open → run → observe → edit → rerun and installed
   package access; record which checks were automated or genuinely
   performed interactively. Preserve headless-runtime semantics.
5. Once locally verified, reuse SP06 as the preferred G2 two-host
   signal-processing demonstration if its supported transport and
   recorded-output path qualify.
6. Maintain an evidence-backed lab-status matrix, including exact
   candidate SHA, reference/test paths, commands, platform, rate
   envelope and blocked reasons. Change the named core only after
   explicit maintainer approval.

**Exit:** agreed educational core meets
[LAB_STANDARD.md](../education/LAB_STANDARD.md), with actual
test and Studio/package evidence on the release candidate.

### Step 1 — Freeze D0 and the candidate qualification matrix

1. Inventory existing C++, Python and Java implementation subsets. Document
   which operations and data profiles are **supported**, **supported after a
   listed D1 task**, or **not part of this release**.
2. Specify the test hardware (at least two physical Linux hosts), operating
   systems, CPU/memory, toolchain, process configuration and independent
   credentials. Include the serial rate, graph/stream sizes, recording length,
   expected sample counts, latency/throughput and memory/queue measurements.
   Choose meaningful performance acceptance bounds **before** the final
   qualification run; do not invent quantitative guarantees from a synthetic
   benchmark.
3. Freeze a useful reference diagram — for example, deterministic AM
   generation on a source host, demodulation/measurement on a processing
   host, and a numeric recording or supported observation. Freeze an
   **independent expected output** and tolerance envelope. Keep smaller
   separate tests for multiprocess feedback, nested causal memory, invalid
   cycles, mixed C++/Python traffic, blocked consumers, lost links, process
   restart and redeployment; record expected values or explicit faults.
4. Make shared fixture/conformance checks visible in CI and document where
   live two-host tests require opt-in test hardware. A test that cannot run
   in public CI must have repeatable local/host commands and archived results.

**Exit:** a reviewed matrix with no ambiguous claim that Python, Java or
Windows supports an untested distributed feature.

### Step 2 — Qualify the practical two-host workflow (G2)

<a id="feedback-loops-across-nodes--020-task"></a>

1. Run a meaningful deterministic source on one physical headless host and
   process or simulate that signal on another headless host. Use the current
   S7 control/data paths and `s7ctl check`. The example must not need
   new audio/video hardware or four independent node processes.
2. Record or observe output using **existing supported functionality**;
   explicitly state whether Studio observation is live or based on recorded
   data. A new remote-observation API is not a hidden release gate.
3. Compare ordered values and derived measurements with an independent
   formula or external numerical reference and, where valid, the equivalent
   local model. Specify input, initial conditions, model time, sample rate,
   comparison tolerances and any wall-clock pacing differences.
4. Regress cross-node feedback with explicit causal memory in separate
   local multiprocess tests: memory placement, nested memory, invalid
   algebraic cycles, stale ordering and unsupported capabilities. Use
   [scheduler test settings](DISTRIBUTED_CAUSAL_SCHEDULE.md) for
   single-token feedback. Real cross-host loops add evidence but are
   **not mandatory**; packet arrival never defines causality.
5. Measure signal throughput, end-to-end lag, CPU, RSS and bounded queue
   occupancy on the two test hosts. No hard real-time claim is implied.

**Exit:** one reproducible engineering experiment with independently
checked results and observable data, plus passing scheduler/invalid-cycle
regressions. Do not block the release on three hosts or a complex loopback.

### Step 3 — Close D1/D2 lifecycle and security gaps (G3)

1. Finish Python split-profile listener/client and trusted-plaintext
   configuration only within the frozen D0 scope. Cover wrong identity,
   wrong endpoint/security scheme, unauthorized control and attempted
   policy downgrade. Reuse the shared wire/control fixtures.
2. Exercise the two-host forward chain, plus local mixed C++/Python
   processes and extra processes as needed for fan-out, asymmetric progress,
   backpressure, disconnect/reconnect, gaps, faults, controlled recovery,
   restart and redeployment. Check active revisions and stream state.
3. Inject failures into the **practical two-host chain** and small feedback
   regression tests separately. Check that missing samples are not invented,
   faults are reported, and completed-step output respects local
   publication semantics. Already-sent packets cannot be rolled back.
4. Use independently generated node/control credentials for the physical
   hosts via existing documented tools/manual provisioning. Demonstrate
   authorization with the correct control-purpose EKU and pinned fingerprint.
   **Do not** make a new cert-issuing program, GUI viewer, settings protocol
   or supervisor/restart service a release gate.
5. Include bounded stress/resource cases and relevant sanitizer checks;
   keep crash-persistent replay and exactly-once external actuation outside
   the contractual claim.

**Exit:** repeatable fail/recover scenarios with checked stream states and
precise, documented limits. Failure-injection success means predictable
failure semantics, not continuous operation through every fault.

### Step 4 — Close G1 and package G4

1. Re-run all applicable numerical tests, independently referenced
   oscillators/control examples, nonlinear validation, nested/rollback/rate
   checks and supported opt-in coupled integration. Preserve model
   initial-state, sample-time and comparison conventions.
2. Run the agreed education suite, reconciling the canonical lab
   diagrams, independent reference tests, source mappings, teaching
   guides and supported Studio interaction evidence.
3. Run Studio diagram/edit, viewer/analysis, lifecycle and distributed
   preflight regressions. Retain screenshots or recorded UI artifacts where
   a visual property matters. **Implement G4/P printing** of diagrams,
   recorded viewers and coherent snapshots from live viewers, to PDF and
   system printers, with layout/cancel/error tests and package checks.
   Verify the [required matrix](STUDIO_PRINTING_0.2.0.md). The separate
   signal generator instrument UI is not required.
4. Build the final source/version into Linux portable x86-64, Fedora 45 RPM,
   Ubuntu 26.04 DEB and Windows portable ZIP artifacts in their documented
   profiles. Check installed/relocated operation, dependencies, notices,
   licensing and SHA-256 checksums.
5. Run full applicable Linux and Wine suites, cross-language fixtures and
   relevant sanitizers on the candidate. Record unsupported tests as
   exclusions with reasons; **never count unfinished or skipped tests as
   passes**. Native Windows camera/audio/device evidence is distinct from
   Wine regression evidence.
6. Publish the practical two-host example — signal/model goal, diagram,
   independent result reference, observation or recording workflow —
   alongside the separate scheduler regressions and release notes. Record
   candidate SHA, platform, measurement provenance, limits and exact
   reproduction commands.

**Exit:** all G1–G4 acceptance records refer to one identifiable candidate,
or a documented maintainer-approved revision of the release contract.

## G4/P — mandatory printing acceptance

The 2026-11-20 scope amendment restores diagram and viewer-output
printing as a **0.2.0 release blocker**. Follow
[STUDIO_PRINTING_0.2.0.md](STUDIO_PRINTING_0.2.0.md) for supported
diagram/Scope/Phase Portrait/3D/live/analysis presentations, PDF output,
physical printing, coherent snapshot semantics, quality, cancellation,
platform and package evidence. This does not change G1–G3.

## Acceptance artifacts

Before tagging 0.2.0, gather:

| Artifact | Minimum contents |
| --- | --- |
| Numerical regression summary | Test command, candidate SHA, pass/fail/skip, independent reference and numerical envelopes |
| Educational qualification matrix | Core lab IDs; course/source status; canonical diagrams, independent oracles, negative tests, Studio evidence, package checks, tested candidate SHA and blocked/excluded reasons |
| Two-host engineering demonstration | Public signal/model diagram, independent reference and captured outputs, supported observation/recording workflow, host inventory, commands and performance results |
| Failure/recovery report | Scenario, injected fault, active revision, expected/actual stream status, output prefix, gap/recovery behavior and limits |
| Interoperability matrix | Supported C++/Python combinations, control/data profiles, fixtures and negative security cases; Java boundary explicit |
| Performance record | Rate, traffic, packet/batch settings, latency/lag, CPU, RSS, queue/log occupancy and reproducible host identifiers |
| Array/viewer qualification | Vector shape/type and causal fault tests, no-payload-copy local fan-out measurements, deterministic FFT/PSD and frame pixel oracles, Waterfall/Video Player saved-diagram and GUI evidence, lifecycle/resource bounds and candidate SHA |
| Printing qualification matrix | Supported diagram/viewer inventory, PDF and printer results, checked page geometry/symbols/legends/axes, snapshot consistency, failure/cancel cases, candidate SHA and platform |

| Release/package manifest | Version/SHA, artifact checksums, packaging and relocation checks, notices, CI/local results and platform exclusions |

A headline test count without the exact candidate and test selection is not
sufficient evidence. Release criteria take priority over attractive additions
or further optimization experiments. A proposed educational lab is not
release-qualified without checked evidence.

<a id="numerical-research-milestones--p1-planned"></a>

## Completed and retained research/baseline

The N0–N4 numerical investigations are delivered as separate research reports:
[N0](NUMERICAL_FEEDBACK_RESEARCH_N0.md),
[Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md),
[Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md) and
[Phase 3](NUMERICAL_INTEGRATION_PHASE3_REPORT.md). Results include limitations
and negative experiments. Production Euler substeps and coupled
Gauss–Legendre are implemented separately under
[COUPLED_INTEGRATION.md](COUPLED_INTEGRATION.md). The initial integrated
cross-node scheduler and its research are in
[DISTRIBUTED_CAUSAL_SCHEDULE.md](DISTRIBUTED_CAUSAL_SCHEDULE.md).
The [integrator review](INTEGRATOR_REVIEW_2026-11-17.md), [sample-rate
contracts](../../doc/S7_Sample_Rate_Model.md), [frame services](FRAME_TRANSPORT.md),
[hardware qualification](HARDWARE_IO_LIVE_TESTS.md) and
[Studio analysis](STUDIO_ANALYSIS_TESTING.md) likewise supply existing
baseline/evidence, not new feature assignments.

For historical task breakdowns (B1/B2, N0–N4, R1–R7, S1–S3, F1, H1,
X1–X3, U1–U5, T1, PKG, Q1 and D0–D5), consult this file's Git history
before the **2026-11-18 scope revision** and the original linked design
documents. This condensed work plan intentionally keeps **only** active
0.2.0 acceptance work.

## Stop rule and release decision

Stop accepting unrelated new capabilities into 0.2.0 **except the explicit 2026-11-23 G1/V, G4/W and G4/V amendment**. Fix regressions or
inconsistencies revealed by G1–G4; do not require production camera/video I/O,
FFmpeg/GStreamer integration, PKI, remote administration, signal-generator,
full tensor/video/spatial processing or general parallel/multi-rate features
to close a gate. **G4/P printing explicitly remains required for both new viewers.** Multi-host feedback stress tests are optional
additional evidence, not the pre-MVP showcase. Any substantive gate change needs an explicit
maintainer decision, an updated contract and a visible release-note exclusion.
