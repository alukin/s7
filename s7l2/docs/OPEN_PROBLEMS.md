# S7 Open Problems

## Mixed observer/full-viewer load — 0.1.1 qualification

The original synthetic published-source check with 12 selected observers, four
active dock previews and four open full viewers sustained 1,000.007 source
steps/s without capture errors, but its 458 ms maximum UI timer gap exceeded
the 250 ms target. Process RSS reached 3,026 MB alongside 175 MB of recording
payload. The 30-minute journal-free live-preview soak stayed below the latency
target and finished below its warm RSS.

Scope and Phase Portrait now limit drawn geometry to the physical plot size
without reducing the captured samples. A matched two-minute mixed workload
reached 271 ms maximum gap, with eight gaps over 250 ms, and 968 MB peak RSS
after rebasing on the merged Scope hover change. Earlier runs ranged from
279 ms to 1,528 ms maximum gaps, so the latency result remains variable.
The final full-viewer-only baseline reached 187 ms and 823 MB peak RSS over
60 seconds. The remaining long mixed-load gaps correlate with scrolling, but
retaining hidden dock scenes did not eliminate them (293 ms maximum, six gaps
over 250 ms, 1.04 GB peak RSS). Reducing dock refresh to 5 Hz and sharing a
QML engine also failed the full two-minute latency gate. Those experiments
were removed. The mixed-load gate remains open: isolate the remaining UI work
or obtain an explicit release-scope decision before claiming it
passed. Evidence and commands are in RELEASE_0.1.1.md and
STUDIO_GUI_TESTING.md. These are software-rendered published-source UI
measurements, not NodeRuntime throughput or physical-GPU measurements.

2026-10-28: profiling on a second, faster host showed the GUI thread saturated
by synchronous Qt Quick rendering and window composition, not by S7 data work
(OBSERVER_UI_LATENCY_PROFILING.md). Plot renders are now paced across
event-loop passes. Dock previews are capped at 10 fps, data-driven status text
updates at 10 Hz, and 3D uses cheaper multisampling. On that host `mixed full`
fell from a 156 ms to an 82 ms maximum gap. On an emulated slow host (4 CPUs,
2 GHz) it fell from 394 ms with 261 gaps over 250 ms to 201 ms with none.
Full-viewer frame rates dropped (11 to 6 fps; 3.8 to 1.3 fps slow). The gate
has not been re-run on the original qualification host.

## Live observer rebinding — follow-up to 0.1.1

The maintainer accepts fixed observation bindings for 0.1.1 but explicitly wants
observers to support source changes during execution later. No release is
assigned yet. Define a non-invasive subscription update at an acknowledged causal
step boundary, with binding generations, clear source/continuity markers, and
stale-work rejection. Decide how new taps become available without restarting
the graph, how missing sources and incompatible types are reported, and whether
recording continues in a new segment. Full viewers and dock previews should
share the binding/settings owner. This is observation reconfiguration; it does
not imply arbitrary live edits to the executing functional graph.

**Additional 0.1.0 requirement (2026-10-23):** separate control and data listeners
with mTLS control and explicit encrypted/trusted-plaintext data modes for
loopback and trusted LANs. This is required before the preview, with Python and
other headless nodes as interoperability targets. Implementation and acceptance
are tracked in [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) and the
[transport contract](SPLIT_TRANSPORT.md).

**New P0 release requirement (2026-10-22):** a separate bounded live
oscilloscope for continuous runs. Existing `-1` execution is continuous but
Scope recording grows on disk. See DEVELOPMENT_PLAN.md's priority section;
the [prototype](LIVE_OSCILLOSCOPE_PROTOTYPE.md) now implements bounded
acquisition, raw hover inspection and [FFT spectrum](STUDIO_SPECTRUM.md).
Constellation/correlation, triggers, shared cursors/measurements/export and
operational qualification remain open under the [design](LIVE_OSCILLOSCOPE_DESIGN.md).

For 0.1.0, the [Linux preview checklist](RELEASE_0.1.0.md) defines the release
gates and explicitly deferred work. Historical open items below are retained
as backlog and do not expand that agreed scope.

Date: 2026-10-21
Status: living document — update in place as items are resolved or found; do not create a new dated copy.

This file replaces three earlier documents (`CODE_REVIEW_REQUIRED_FIXES.md`, `PROJECT_STATUS_AND_PLAN_2026-09-20.md`, `PROJECT_REVIEW_AND_DEVELOPMENT_PLAN_2026-10-14.md`), which tracked mostly-resolved findings and a stale plan and are now removed from the working tree. Their history (what was found, how it was fixed, and which fix superseded which) is still available with `git log --follow -- docs/CODE_REVIEW_REQUIRED_FIXES.md` and `git show <rev>:docs/CODE_REVIEW_REQUIRED_FIXES.md` etc.; resolved prerequisites are retained below only where they explain the current simulation baseline and remaining limits.

This document lists *what* is unsolved. For *how and in what order* to address it, see `docs/DEVELOPMENT_PLAN.md`.

## Linux acceptance increment — 2026-10-21

The Studio-enabled Release suite passed 73/73 tests. A relocated package passed
12 checks in a fresh Fedora container without system Qt or source access, and
a 90-second visible-GUI run measured complete four-channel capture and bounded
local control/event-loop latencies. See
[LOCAL_SIMULATION_ACCEPTANCE.md](LOCAL_SIMULATION_ACCEPTANCE.md) for the exact
workload, metrics and limits. This advances C1/P1/A1 without closing them:
native Windows, physical display/accessibility, actual disk exhaustion and
longer/broader workloads remain open.
Hosted headless and Studio CI passed on both the fix commit `3026b84` and
package implementation `89628cc`; run links are in DEVELOPMENT_PLAN.md. Focused
local Clang ASan/UBSan core/plugin/control checks also passed (3/3). Earlier
dated statements below describe
prior evidence and do not supersede this record.

## R1 completion audit — 2026-10-20

Resolved in the R1 increment: pause now waits for a complete worker boundary;
active schema/settings/probes and observer routing are frozen per run; queued
callbacks and mailbox samples are generation-checked; close/destruction joins
the worker before member/plugin teardown. Scope reruns archive previous live
series as labelled comparisons, and phase views reset partial coordinates.
Dedicated runner/window regressions cover transitions, cancellation, errors and
recovery, edits during runs, observer changes and running/paused shutdown.
Stop/reset remain cooperative: an element step that never returns can prevent
joining. Recording ownership/completeness is handled by O1a/O1b below; measured throughput remains release acceptance work.

## Automated GUI suite priority

**G1 / P1 / local acceptance passed; hosted execution unverified:** Qt Test
workflows and Qt Quick Test production scenes run under isolated Linux Xvfb,
with screenshot/log/JUnit artifacts, an intentional failure-artifact self-check,
and actual Qt Quick 3D pixel/camera tests. Unavailable Xvfb/OpenGL report skips.
The final 4/4 CTest GUI run passed using Mesa llvmpipe, with 10 passing Qt Test
cases and no rendering skips. The existing 61 tests also passed after the change;
the diagnostic/watchdog refinement has its own passing self-check. Root GitHub Actions now
provisions and runs the suite; hosted execution remains unverified. See
STUDIO_GUI_TESTING.md and DEVELOPMENT_PLAN.md for coverage and final evidence.

## Release audit inventory — 2026-10-20

Task identifiers and acceptance tests are in DEVELOPMENT_PLAN.md's release audit.

- **D1 / P0 / resolved:** diagram and schema-library saves now share checked
  QSaveFile replacement with direct-write fallback disabled. The atomic-file
  regression verifies actual injected write failure preserves the destination,
  invalid paths, successful replacement/reopen and recovery. Actual disk exhaustion
  remains untested; the focused regression and Studio build pass.
- **D2 / P0 / implementation complete; coverage expanding:** destructive document
  transitions share Save/Discard/Cancel handling and compare serialized document
  contents, including nested schemas and local run settings. A failed save cancels
  replacement. Focused tests cover close/open, undo, nested prompting, failed Save,
  saved settings and worker shutdown during running/paused replacement. Native
  dialog interaction and the wider editor gesture matrix remain acceptance work.
- **O1 / P0 / capture implementation validated:**
  authoritative worker capture now precedes preview delivery. Viewers attach
  read-only sources; storage backpressure preserves model-time semantics.
  Forced overflow retains all 220,000 samples despite 20,000 skipped preview
  updates. Injected partial-write/final-flush failures stop the run and preserve
  incomplete metadata; invalid/complex probes fail before stepping. The 69-test suite passed, and the final comparison refinement passed all 15
  affected/GUI checks; evidence is in DEVELOPMENT_PLAN.md.
- **C1 / P1 / CI placement/provisioning implemented; hosted validation open:** the
  workflow now lives at repository-root .github, uses s7l2 as its source directory,
  provisions compiler/Qt/Xvfb/Mesa dependencies and runs headless/Studio and GUI
  checks with diagnostic artifacts. Local GUI and reporting checks pass. Actual
  hosted execution, sanitizer and Windows job coverage remain release work.
- **L1 / P1 / relicensed 2026-10-21; notice inventory open:** the maintainer
  decided the layered licensing of ADR-019 (`../../LICENSING.md`). The maintainer
  holds the copyright; the only external contributor (`teren-ukr`, 24c4674,
  2801427) gave permission; `s7spectral` is first-party and only built like a
  third-party plugin. Every file has SPDX information (`reuse lint` passes
  locally). Remaining: run `reuse lint` in CI; complete the bundled-library
  notices and source offers (`../../THIRD_PARTY_NOTICES.md`, P1).
  Vendored QSchematic/GPDS/Tabler keep their MIT licences.
- **R1 / P0 / implementation complete:** explicit acknowledged lifecycle states,
  immutable active configuration, generation-isolated delivery, worker error
  recovery, observer transitions and safe shutdown have deterministic Qt Test
  coverage. Full-suite verification is recorded in DEVELOPMENT_PLAN.md.
- **N1/U1/A1 / incomplete coverage and usability:** N1a local model time, duration
  boundaries, lifecycle alignment and initial saved-model execution parity are
  validated. N1b adds immutable requested/effective configuration, defaults/nested definitions,
  seeds, rate and build/element identities to saved results with Run details and
  tested saved-config replay. N1c adds the bounded linear analytical/Studio parity matrix (see NUMERICAL_VALIDATION_MATRIX.md). Wider nonlinear/control references, external input histories
  and UI acceptance remain; existing pendulum/viewer results are retained as historical
  evidence, not fresh release acceptance. Real GPU/package/Windows validation remains
  unverified. DISPLAY variables alone do not establish a usable display.

## Next-release scope — decided 2026-10-19

The next release has one goal: **good simulation quality in `s7studio` with its bundled runtime**. [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) defines the local, single-rate workflow and release acceptance criteria. The previous P0 question about the next release target is closed. Broader distributed/production work below remains a backlog, not a prerequisite for this release; local numerical, observation and lifecycle defects take priority regardless of the historical P1/P2 labels.

## P1 — Local simulation quality: remaining release gaps

- **Numerical coverage is partial.** N1a fixes local timestamp drift from rounded periods and finite-duration overshoot. Focused tests compare every sample from the same saved linear/nonlinear diagrams through headless and Studio paths, and cover first/final publication plus gated pause/resume/rerun. All 71 registered tests have passed across the complete run and three affected reruns after updating the older nonlinear timestamp reference; details are in DEVELOPMENT_PLAN.md. Scalar analytical/convergence tests and the forward-Euler/symplectic oscillator regressions exist, but do not validate every bundled diagram, timestep or integration method. N1c now validates all ten saved linear diagrams at 1/2 kHz against analytical references through headless and Studio worker recordings, including first/final publication and timestep refinement (NUMERICAL_VALIDATION_MATRIX.md). Extend that bounded matrix to further supported configurations. In particular, continuous oscillator energy conservation is not a valid exact requirement for forward Euler. More demanding nonlinear/chaotic examples need explicit experimental limits until validated.
- **Recording policy now separates capture from presentation; operational envelope remains unmeasured.** LocalSchemaRunner records selected probes on its worker before the bounded preview mailbox, using bounded RAM and synchronous temporary-file backpressure. Preview overflow does not truncate authoritative data; storage failure stops capture/run and marks the retained prefix incomplete. Deterministic tests inject disk-full partial writes and flush failures; actual filesystem exhaustion/crash recovery is not claimed. Bulk projection/export/import is still synchronous UI work, and UI/source reads may wait for serialized storage I/O. Measure supported throughput, rate/duration/channel counts, disk use and UI latency; add asynchronous bulk operations without changing causal execution. Temporary capture is not durable persistence.

- **View and run lifecycle coverage needs expansion.** The 2026-10-19 Scope corrections address repeated thinning of early history, replacement of recordings by the last 20,000 points on open/reopen, and Auto Y changing the time axis. `s7studio_scope_view` checks preview resolution/bounds and zoom preservation; `s7studio_scope_run` runs the actual two-integrator example at 1/10/100 kHz for ten model seconds, opens Scope afterward and reopens it, checking the full time extent and curve. The new `s7studio_scope_lifecycle` regression covers two-channel pendulum pause/resume, stop while paused, reset/rerun, hide/reopen, live save/export, imported disk-backed zoom, comparison persistence and malformed-file rollback. [Saved results](SCOPE_RESULTS.md) can be overlaid across runs. R1 adds phase-coordinate reset and running/paused shutdown coverage. Observation enabled/disabled equivalence, large-file responsiveness and measured shutdown under load remain open.
- **Supported performance and packaging are unspecified.** Discretization frequency defines model time, not guaranteed steps per wall-clock second. Record throughput, UI responsiveness, pause/stop latency, queue/preview memory and disk use on stated hardware; smoke-test packaged Studio and required local plugins without a separate service. Studio-enabled CI is a release requirement; native Windows support requires native evidence, not only Wine.

## P1 — Validation, CI, and whole-system testing

- **Root CI and local GUI coverage implemented; hosted execution remains unverified.**
  Repository-root `.github/workflows/ci.yml` now provisions Fedora 44 dependencies,
  checks the supported compiler/Qt requirements through CMake, builds `build-tests`,
  runs headless/Studio checks and the Xvfb Qt Test/Qt Quick Test suite, reports skips
  explicitly, and retains diagnostics. Local GUI acceptance is 4/4; actual GitHub
  Actions execution has not been observed. Sanitizer and Windows/Wine jobs remain
  broader release infrastructure work. Test-target assertions stay active in
  Release/RelWithDebInfo through `-UNDEBUG` (or `/UNDEBUG`).

- **Two-node integration placement and coverage need follow-up after the local release.** `s7tests/` now contains numerical/reference and diagram-harness tests. The two-node scenario remains `s7_two_node_deployment`, registered by `s7ctl/CMakeLists.txt` and driven by `tools/test_two_node_deployment.sh` (`LOCAL_HEADLESS_NODES_DEMO.md`); it covers deployment, a restart/sequence-gap fault and recovery. Decide later whether to move its driver; standalone clean shutdown and multi-cycle restart/recovery remain broader integration gaps.
- **No dedicated concurrency audit or stress coverage.** Runtime graph replacement, stream ownership, endpoint shutdown, stepping, and Studio worker/UI handoff span modules; the existing tests are clean under ThreadSanitizer but are smoke-level, not a targeted lock/lifetime audit or deterministic stress suite, and nothing runs them under a sanitizer in CI.
- **Cross-language conformance is not a required check (broader backlog).** The wire and control fixtures (`s7transport/tests/fixtures/`, `s7runtime/tests/fixtures/`) are checked against a reference Python decoder/generator, but nothing requires another-language conformance check to pass before a release that changes these protocols; drift between this implementation and the sibling Python implementation in `../doc/` is possible without being caught.

## P1 — Observation architecture: Studio's `Scope` contradicted ADR-011/012 — diagram observer restored

- **`Scope` used to be implemented as a real Functional Graph element, which the project's own architecture decisions rule out.** `S7_Architectural_Decisions.md` ADR-011 (*"Observation becomes an independent subsystem"*) and ADR-012 (*"Probes are not Elements. They are not part of the Functional Graph"*) establish that observation points must be external, non-invasive taps — matching `S7_Terminology.md`'s definition of `Scope` as an *Observation Client*, and `ObservationService`'s actual API (`ProbePoint{element_id, port}`, read-only, no graph participation). But `ScopeElement` (formerly `s7studio/src/local_runtime_v2.hpp`) was a genuine `s7::Element` subclass with real input ports, wired into `schema.elements`/`connections` like any computational element, compiled and stepped by `s7::Compiler`/`NodeRuntime`. It only worked because Studio's own local schema runner special-cased the type name `"Scope"` outside `ElementRegistry`; headless `s7runtime`/`s7ctl` could not compile a schema containing one at all.
- **Architecture boundary fixed (2026-10-15); visible Scope observer restored in this change.** `Scope` remains outside `schema.elements` and runtime `connections`, consistent with ADR-011/012. `DiagramProbe` remains available as a lightweight tap and for phase portraits. The diagram now also supports an **Observers** palette category with a multi-input `Scope`; observation wires are green and dashed, serialize as diagram layout, and are excluded from the functional net/schema. The final `+` input grows when connected, and double-click opens one multi-channel plot window. Existing probe markers remain supported and are not silently converted on load. The three bundled example diagrams now explicitly represent their former Scope plots with observer nodes and observation links.
- **Probe editing integrity updated (U1, 2026-10-21):** add/rename/remove are undoable; element/port deletion removes associated probes in the same command and undo restores them. Clipboard subgraphs retain probes and internal observer wiring. Phase views use retained read-only sources for reopening; broader long-run performance qualification remains open.
- **Related duplication, now superseded by a real design rather than a shared-function patch:** `s7::Schema` has no schema-wide discretization field; Studio reconstructs one from the diagram's own top-level `dt` and pushes it into every element's `"dt"`/`"sample_rate_hz"` parameter via `MainWindow::updateSchemaDiscretizationParams` (Qt-only, `s7studio/src/main_window.cpp`). See the next entry, "Rate/`dt` has no formal model," for the actual fix.

## Rate/`dt` inference and validation — implemented; multi-rate execution deferred

- **Full design: `../../doc/S7_Sample_Rate_Model.md`.** `dt`/sample rate was, until this item's first increment below, an ordinary per-element parameter with no cross-element consistency check anywhere (`s7::Schema` had no rate field at all; `sample_rate` exists only at the network-stream boundary, derived ad hoc from the diagram's `dt`; Studio's propagation is a Qt-only UI convention, not a compiler/validator guarantee). This was judged a **hard prerequisite for any simulation testing**, not just a Stage 2–style blocker: nothing used to *prove* two integrators in one schema share a rate, it was only ever hand-checked.
- **First increment implemented (2026-10-14):** `s7::Rate` (`s7core/include/s7/rate.hpp`, the same rational `{numerator, denominator}` shape the wire protocol already uses), `Element::declared_rate()` (a new virtual, nullopt by default; overridden by `EulerIntegratorT`/`TrapezoidIntegratorT` from their `dt` parameter and `AnalogOscillatorT` from `sample_rate_hz`), `Schema::rate()`/`set_rate()` (the schema-level seed), and `RateResolver::resolve(schema, instances)` (`s7core/include/s7/rate_inference.hpp` + `.cpp`) — a union-find over elements connected by plain wires, pinned by any `declared_rate()` in the set or by the schema seed, reporting a named conflict or an "undetermined" error otherwise. Tested in isolation by `s7core_rate_inference_smoke` (registered in `s7core/CMakeLists.txt`).
- **Deliberately narrower than the full design, for now:** the implementation gives each *element* one rate shared by every port (`Variable`, in the design doc's terms) — true of every element that exists today. It does **not** implement the design's `PortRateRule::RatioOf` kind (an element with input/output ports at different, explicitly related rates) since no rate-changing element exists yet to need it; build that in when one does (`DEVELOPMENT_PLAN.md` Phase 5 item 7), per the project's own "don't design for hypothetical future requirements" guidance, rather than before anything needs it.
- **Wired into `Validator::validate()`/`Compiler::compile()` as a mandatory gate (2026-10-14).** `s7core/src/validation.cpp`'s `check_schema` now calls `RateResolver::resolve()` after type/connection/cycle checks pass; "no element pins a rate and the schema has no seed" is a hard validation error for every schema, not just an opt-in check. Making this mandatory immediately failed every existing schema/test with no rate-pinning element and no seed (10 of 25 `ctest` cases, most of `s7core`'s own other smoke-test schemas, every non-simulation JSON example, the two-node deployment test); all were fixed rather than loosening the gate: a new `"dt"` field (seconds, side-effect-free) on `s7::Schema`'s two JSON-loading paths (`s7runtime`'s own schema format, `s7core`'s schema-library format — both `parse_schema_definition`/`build_schema_from_object`, falling back to `execution.step_rate_hz`/`simulation.sample_rate_hz` for `s7runtime` schemas that set those for their own reasons and have no explicit `dt`), `GraphHost::stage()` seeding a deployed graph's schema directly from the already-threaded `NodePart::model_time_step_ns` (exact, not round-tripped through a double) since a deployed graph may not carry its own `"execution"`/`"simulation"` block, and a blanket `schema.set_rate(s7::Rate::from_hz(1))` added to every C++ test schema that doesn't otherwise care what rate it runs at. `Schema::ensure_compiled_runtime()`'s internal recompiled copy now also carries the original schema's `rate_` forward (it previously silently dropped it). **Studio fixed (2026-10-15).** It was unbuildable in the sandbox used to verify the mandatory-gate change above, so the regression shipped unnoticed: `s7studio_smoke` (and any Studio diagram with no rate-pinning element) started failing with "rate cannot be determined" the moment the gate went in. Confirmed by actually configuring and building Studio (Qt6 is available in this environment after all) and running its full `ctest` suite. Fixed in `s7studio/src/local_runtime_v2.hpp`'s `buildSchema()` (reads the same `"dt"` JSON field `s7runtime`/the schema-library format already use) and `LocalSchemaRunner::prepareRuntime()` (`s7studio/src/local_schema_runner.cpp`, seeds the schema from the run's own authoritative `dt` directly, since that is the actual value the runner steps at regardless of what the JSON says). All 8 `s7studio_*` tests pass again. Lesson: a change verified only in a headless sandbox build can silently break the GUI build; `S7_BUILD_STUDIO=ON` should be exercised before calling a cross-cutting `s7core` change like this one complete.
- The design extends S7's existing type-inference mechanism (`s7core/include/s7/type_inference.hpp`'s `TypeResolver`/`SignalTypeSet`/`PortTypeRule`/`TypeVariable`) only at the naming/concept level, not algorithmically: type inference narrows a *finite, enumerable* set of allowed `SignalType`s by intersection, but a rate is an unbounded rational, so the implemented resolver is a union-find over equality constraints (mirroring `TypeResolver::resolve`'s role, not its internal set-narrowing mechanics). A rate conflict between connected elements with no rate-changing element between them is a validation error naming both elements, mirroring how a `SignalType` conflict already works — never an implicit/auto-inserted fix (a deliberate divergence from Simulink, which can auto-insert its analogous Rate Transition block; S7's own "nothing converts implicitly" rule for `Convert` argues against doing the same here).
- Grounded against GNU Radio (enforces rate consistency at the ratio level via a block's declared relative rate; hardware source blocks report actual runtime rate/time via stream tags, separate from a flowgraph's design-time assumption) and Simulink (Sample Time Colors — every block/wire colored by its resolved sample time, exactly the "know the rate at every wire" property wanted here; a Rate Transition block with direction-dependent behavior, hold going to a slower rate, delay going to a faster one).
- Deliberately scoped to inference/validation (a static, compile-time property), not to how a genuinely multi-rate graph would *execute* (single global tick vs. per-rate-domain scheduling vs. `NetSource`/`NetSink`-style internal buffering for a hardware boundary) — that harder question is out of scope for the design doc and folds into `DEVELOPMENT_PLAN.md` Phase 5's "distributed scheduling and coherence domains" item, broadened to the local case.
- **Practical effect on this plan:** every stage in `VISUAL_NUMERICAL_TESTS_PLAN.md` has no hardware elements and no rate-changing elements, so the resolver confirms exactly one rate for the whole graph in every case — the design doesn't block *that* outcome, and the implemented headless diagram runner (`s7tests/diagram_harness.hpp`) runs schemas through that mandatory gate. Stage 1.1 (`linear_scalar_smoke.cpp`), built against the old hand-propagated convention, has been revisited (2026-10-15): a `test_rate_resolver_confirms_one_rate()` check now calls `RateResolver::resolve()` directly and confirms its schema resolves to exactly 1/`dt`, rather than merely re-passing the now-mandatory gate by inspection.

## P1 — No generic trigonometric/math element blocks nonlinear modeling — `Trig` implemented, Stage 2 unblocked

- **`s7core` had no element that applies a nonlinear scalar function (sin, cos, square, sqrt, ...) to an arbitrary wired input.** Confirmed by inspection of `s7core/include/s7/elements.hpp`: the only arithmetic elements were `Gain`, `Adder`, `Multiply`, `Accumulator`, and the integrators/filters; `AnalogOscillator` computes `sin` only on its own private internal phase accumulator, not on a general input, so it could not be reused for this. This surfaced while planning `VISUAL_NUMERICAL_TESTS_PLAN.md`'s Stage 2 (the mathematical pendulum, `v̇ = −ω₀²·sin(x)`): the dynamics themselves could not be expressed as an S7 causal graph at all without such an element — not specific to testing or to observation/diagnostics, any engineer modeling a system with a trigonometric nonlinearity hit the same wall. (Van der Pol's `(1−x²)` term and Lorenz's `xy`/`xz` terms need no new element — `Multiply` already covers them.)
- **Implemented (2026-10-15):** `Trig` (`s7core/include/s7/elements.hpp`'s `TrigT<Type>`, aliased `s7::Trig` for `F64`), a single generic element with a `function` enum parameter (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`; default `sin`), `Feedthrough::Direct`, no state — following Simulink's Math Operations precedent (one parameterized `Trigonometric Function` block, not one class per operation) and S7's own idiom (`Convert`'s `mode`/`rounding`/`overflow` parameters). `s7::math` (`s7core/include/s7/float_types.hpp`) previously had only `sin`/`sqrt`/sundry; it now also has `cos`/`tan`/`asin`/`acos`/`atan`/`sinh`/`cosh`/`tanh` for every floating type (`F16`/`F32`/`F64`/`F128`, both GCC-native and the Clang/libstdc++ non-native path, including the Windows/libquadmath externs). Tested by `core_smoke.cpp`: every function against its `std::` reference, an unknown function name rejected, a real registry-driven `sin(x)` graph (the pendulum's own term), and pinning `Trig` to an unsupported (integer) type rejected. Verified on both the native Linux build and the Windows cross-build (`build-win`, `ctest` under Wine).
- **Scoped to real floating-point types only, not complex** — a deliberate narrowing from the original "generic over floating/complex-floating types the way `IirFilter`/`TrapezoidIntegrator` already are" framing: `s7::math` has no complex-analytic `cos`/`tan`/`asin`/... anywhere (native mode's generic `std::complex<T>` overloads are unverified for `s7::float16_t`/`s7::float128_t`; the Clang/libstdc++ non-native path has no complex trig wrappers at all, only `abs`/`arg`). Pinning `Trig` to a complex type is refused the same way an unsupported type already is for any other element. Add complex support (and the hand-rolled non-native formulas it would need) only if something actually requires complex trig — Stage 2's pendulum, Van der Pol, and Lorenz all only need real arithmetic.
- This unblocks `VISUAL_NUMERICAL_TESTS_PLAN.md` Stage 2 onward (see its section 1.5 and execution order, section 6) — `Trig` was the last remaining hard prerequisite alongside the rate model.

## P1 — Production-readiness gaps (after the local simulation release)

- **No production credential lifecycle.** Certificate enrollment authority, certificate profiles and role OIDs (the control-node key-purpose OID is currently a development placeholder, `2.25.…`, pending real S7 OIDs), rotation/revocation, key protection, bootstrap trust, and recovery after loss of the control Studio are all undefined. The identity/provisioning document is explicitly a proposal, not a production spec; treat current test certificates as fixtures only.
- **No independent security review has been performed** of TLS setup, certificate parsing and authorization, plugin loading, file/path handling, canonical JSON and digest validation, or deployment persistence.
- **No documented operational limits or runbook:** maximum graph/schema/library size, plugin trust policy, stream buffer/log sizing, persistent-state backup/restore, disk-full behavior, service supervision, and upgrade/restart behavior are all unspecified.
- **Crash-recovery scope is undecided.** Today a process crash faults the streams it took part in and loses unacknowledged in-memory data; `RECOVER_STREAM` continues without the lost data, and lossless delivery holds only while both processes stay alive. This is a known, documented MVP limit, not a defect — but whether durable crash recovery is ever in scope, and if not, how consistently that is surfaced to an operator across CLI/Studio/docs, is still an open decision.
- **Feature claims lack an operational profile.** "Lossless," "real time," and "distributed" have precise meanings in the protocol docs, but there is no single place stating supported graph size/rate, latency and clock assumptions, expected hardware, restart behavior, and resource ceilings for an operator to plan against.

## P2 — Broader math-element gaps — listed blocks implemented

- **Checked against the Stage 2–5 test cases in `VISUAL_NUMERICAL_TESTS_PLAN.md`:** the pendulum needs `Trig` and `Divider`; Van der Pol's `x²` and Lorenz's `xy`/`xz` terms need only `Multiply`, and `ComplexToMagnitudePhase` already exists for Stage 5's frequency-response work. Step/ramp/impulse test inputs are all constructible from existing elements (`Constant`; `EulerIntegrator` fed by a `Constant` for a ramp; `Adder(weights=[1,−1])` combining a `Constant` with its own `Delay` for an impulse) — convenient dedicated source elements would be nicer, not necessary.
- **The listed Math Operations/Discontinuities gaps are implemented in `s7core`:** `MathFunction` (`sqrt`, `exp`, natural `log`, parameterized `pow`, `abs`, `reciprocal`), binary `Power`, `Saturation`, configurable 2–64 input `MinMax`, and `Sign`; `Divider` was added for the full double-pendulum model. These operate on real numeric types as documented in `ELEMENT_DEV_GUIDE.md`. This closes the specific list above, not all of Simulink's libraries or complex-valued math operations.

## Simulation accuracy — numerical research for 0.2.0

- **Release assignment (2026-10-30, planned):** the maintainer and Codex own
  integrator and feedback-settling research as a P1 0.2.0 workstream on a
  dedicated branch. This supersedes the 2026-10-25 *unassigned* decision,
  which had already removed research from 0.1.1. The [0.2.0 scope](RELEASE_0.2.0.md#numerical-integration-and-feedback-settling-research--p1-planned)
  defines the experimental matrix and release gate; [N0–N4](RELEASE_0.2.0_WORKPLAN.md#numerical-research-milestones--p1-planned)
  define delivery. New production methods remain conditional, and historical
  0.1.1 notes retain their original dated decision.

- **Historical feedback-settling input (maintainer, 2026-10-24):** the first S7
  implementation, roughly 30 years ago, used iterations to settle feedback-
  connected diagram regions; the maintainer reports that even 5–10 iterations
  significantly improved simulation accuracy. The maintainer subsequently
  clarified that these 30-plus-year-old recollections are fuzzy: poor Euler
  accuracy in feedback was solved then, but the actual method is not remembered.
  Neither Euler–Cromer nor the exact loop-settling algorithm is established as
  that historical solution. Record this as a research input,
  not measured evidence for the current runtime or an implemented 0.1.1 change.
  Revisit how estimates are refined within one model step, which states are
  held fixed and committed once, convergence/error handling and the applicable
  model classes. Repeatedly calling stateful element steps is not an equivalent
  settling operation: it can advance state more than once. Preserve explicit
  causal memory and local analog-computer networks. The current coupled
  Trapezoid endpoint solve already performs bounded within-step fixed-point
  iteration; any broader feedback-settling method needs its own contract and
  independent numerical comparisons. Its exact historical implementation
  remains unknown.

- **Research task — Euler feedback accuracy:** N0–N4 must reproduce the
  long-run drift reported for `2int_osc`, then compare explicit Euler,
  Euler–Cromer/Symplectic, coupled Trapezoid, TPT/Zavalishin-style methods and
  Euler with iterative settling only where their equations apply. Separate
  amplitude, energy, phase and state errors; vary timestep and run length
  against independent references. Specify trial evaluation, held state and
  single commit before prototyping settling. Do not infer the historical
  algorithm or promise a fixed accuracy multiplier. Research does not authorize
  production ordering changes or a global solver.
  The maintainer explicitly reaffirmed the analog-computer approach and named
  Siebert's *Circuits, Signals, and Systems* as the original inspiration.
  [SIMULATION_MODEL.md](SIMULATION_MODEL.md) records the verified book reference,
  relevant chapter reading plan and current full-text access limitation. Treat
  the book as a requested design reference, not as a method already studied or
  evidence that the earlier algorithm has been recovered.

- **Found (2026-10-19) via `examples/2int_osc.s7diagram`:** the continuous harmonic oscillator (x' = −v, v' = x, x(0)=1) conserves amplitude 1.0 (eigenvalues ±i), while the realization built from two plain `EulerIntegrator` elements has growing amplitude. Root-caused, not just observed: forward Euler applied simultaneously to both state variables gives the discrete step matrix `[[1, −dt], [dt, 1]]`, whose eigenvalues are `1 ± i·dt` with modulus `sqrt(1+dt²) > 1` — every step multiplies the amplitude by that factor. Verified headlessly (dt=0.001): measured amplitude matched the exact closed-form prediction `amplitude(t) = (sqrt(1+dt²))^(t/dt)` to floating-point precision at every checkpoint from t=10s to t=200s (+10.5% drift by t=200s). This is a property of forward Euler on any marginally-stable ("center") linear system, not an S7 defect.
- **Partial mitigation implemented (2026-10-19):** `SymplecticIntegrator` (`s7core/include/s7/elements.hpp`'s `SymplecticIntegratorT<Type>`, semi-implicit Euler / Euler-Cromer: `v[n+1] = v[n] + dt·a[n]`, `x[n+1] = x[n] + dt·v[n+1]`, one `acceleration` input, `position`/`velocity` outputs; the bound requires acceleration to depend only on position, not velocity). Verified against the same oscillator (x'' = −x via `Gain(−1)` feeding its own `acceleration` input): amplitude stayed within [0.99975, 1.00025] over a 400 s run (vs. plain Euler's +10.5% at only 200 s) — bounded, not drifting. Regression-tested by `s7tests/symplectic_oscillator_smoke.cpp` (`s7tests_symplectic_oscillator_smoke`), which pins both the plain-Euler drift (against the closed-form growth factor) and the symplectic integrator's bound. **Equivalent to an existing-element composition, found afterward:** `Gain(dt)` feeding an `Accumulator` (`Feedthrough::Direct`, so its updated value is visible the same step) for velocity, wired into an `EulerIntegrator` (causal memory, closes the loop) for position, is the identical semi-implicit Euler recursion — verified to bound amplitude the same way. `SymplecticIntegrator` is the one-element convenience for this specific x''=a(x) pattern, not the only way to reach it; an earlier claim in this conversation that no composition of existing elements could do this was wrong.
- **Coupled-linear comparison and implicit trapezoid implemented (2026-10-19):** nine diagrams in [`examples/double_pendulum`](../examples/double_pendulum/README.md) cover slow, fast and mixed modes with Euler, Symplectic and implicit Trapezoid. The earlier supplied-sample-plus-delay arrangement was first order and had substantial energy growth. Default Trapezoid is now causal memory with a coupled implicit endpoint solve; its diagrams need no artificial delays. Tests require second-order state convergence and bounded quadratic energy error on this linear model at 1/10/100 kHz over 20 seconds. The solver uses at most 64 fixed-point iterations and reports nonconvergence; this does not establish stiff-system support. See [the contract](TRAPEZOID_INTEGRATION.md) and the examples README for current results. Broader nonlinear/stiff/chaotic comparative research remains open.
- **Nonlinear single-pendulum baseline validated (2026-10-19):** [three diagrams](../examples/pendulum/README.md), 100 seconds at 1/10 kHz, independent elliptic/RK4 reference and fixed error budgets. Symplectic and implicit Trapezoid meet these budgets; Euler fails because of accumulated energy/phase drift. Full bundled-runner Scope recording and CSV are verified against headless samples with zero drops and exact double round trips across six runs. This closes this pendulum baseline, not stiff/chaotic comparisons or general recording-overload qualification.
- **Broader comparison now planned, not validated:** the 0.2.0 research matrix
  extends beyond the existing single-pendulum and coupled-linear baselines to
  Van der Pol, Lorenz and feedback control. Existing teaching diagrams and
  focused smoke checks do not establish method superiority, general stiff
  support or long-run chaotic trajectory agreement. The full nonlinear double
  pendulum and higher-order symplectic methods such as Störmer–Verlet remain
  possible follow-up questions, not additional required 0.2.0 methods.

Unresolved research decisions are separated below; these are questions, not
claims about an implemented settling method:

- **Mathematical:** Which model classes and loop equations admit a unique
  solution? When do multiple or no solutions occur? Which energy, period,
  limit-cycle or statistical properties are meaningful for each model?
- **Numerical algorithm:** Which update equation and feedback-evaluation rule
  does each candidate actually implement? Under what timestep, gain,
  nonlinearity and topology is iteration contractive? How should residual,
  tolerance, iteration cap and failure be defined? Can local settling improve
  fidelity and reduce explicit-delay artifacts without merely buying accuracy
  through more evaluations? How do methods interact in mixed graphs?
- **Runtime architecture:** Can trial evaluations preserve deterministic causal
  execution, staged publication and one state commit while excluding unsafe
  stateful or side-effecting dependencies? Direct-feedthrough algebraic loops
  are currently invalid; any new loop-resolution, ordering or publication
  contract needs separate architectural review. No parallel scheduler belongs
  to this workstream.
- **Performance:** Compare error at equal timesteps and equal computational
  budgets against indexed serial execution. Measure accepted-step time,
  element evaluations, iteration counts, convergence failures, memory and
  long-run stability. Determine when added complexity pays for itself.
- **Production integration:** Which, if any, method demonstrates a meaningful
  advantage, preserves existing behavior and passes numerical, causal and
  regression review? The maintainer must decide explicitly; inconclusive or
  negative research can still satisfy the 0.2.0 research deliverable.

## P2 — Code structure and maintainability

- **Large multi-responsibility translation units** (measured 2026-10-14): `s7studio/src/main_window.cpp` (2412 lines), `s7transport/src/transport.cpp` (1772), `s7runtime/src/control_plane.cpp` (1605), `s7studio/src/diagram/diagram_scene.cpp` (1477), `s7runtime/src/runtime_service.cpp` (1387). File size alone is not a correctness problem, but reviewing changes, isolating tests, and reasoning about ownership/concurrency in these files is already difficult and will get harder. (Suggested split boundaries are in `docs/DEVELOPMENT_PLAN.md`, Phase 3.)
- **Schema-backed element libraries are plain JSON files.** The 2026-09-20 plan's preference for a zipped/archive-based library format for easier distribution was never implemented; `docs/SCHEMA_LIBRARY_WORKFLOW.md` describes only a JSON `document_kind: "s7.element_library"` file. Open design decision, not a defect: decide whether archive packaging is still wanted before building it.

## P3 — Deferred architecture capabilities (documented MVP limits, not defects)

- **Within-node parallel execution — 0.3.0 planning:** indexed serial
  execution is the 0.2.0 performance increment. The corrected FFT branch
  benchmark shows that independent expensive work can benefit, while FIR and
  small-workload results are mixed; neither prototype is a production graph
  scheduler. Region partitioning, static price tags, plugin and stateful-element
  safety, worker policy, rollback, graph replacement and the serial fallback
  remain open decisions. See the [0.3.0 planning record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md)
  and [performance evidence](RUNTIME_PERFORMANCE_ARCHITECTURE.md). No
  within-node parallel scheduler is required for 0.2.0.
- `SOURCE_TIME` is carried and checked like the other timestamp roles, but no element produces it yet; needs a real or deterministic acquisition-time source (e.g. an SDR input) to validate against.
- Streams are not relayed through other nodes; identity, buffering, latency, and failure-propagation semantics for a relay need to be specified before adding one.
- Distributed scheduling and coherence domains are undefined; `NetRuntime` and synchronized multi-node execution do not exist. Needs formal invariants and executable examples before implementation.
- No snapshot/restore; element-state serialization needs a versioning and validation contract first.
- No remote observation; must be added through an explicit network/data contract that preserves local observation's non-invasive, same-thread guarantee.
- Over plain WebSocket, node identity is only a claim (`S7-Node` header) — it keeps consumers apart but is not trusted; only mTLS deployments are authorized.
- Windows binaries are cross-built on Linux (Clang + MSYS2 sysroot) and tested only under Wine, not validated on native Windows; a Windows node's clock is never treated as synchronized without the explicit `clock.assume_synchronized` escape hatch.
- `s7spectral` (the reference out-of-tree plugin) still lacks frame/block signal types. `WindowedFFT` works (scalar dominant-frequency/amplitude outputs), but `MagnitudeSpectrum` and `PowerSpectrumDB` were removed until a `SpectralFrame`-style port type exists to carry frame-valued data. (A hardware element's native rate, e.g. MSPS-range for an SDR, is likely far faster than `NodeRuntime::step()` can reasonably be paced at, so it would need to operate on batches/blocks of samples per logical step — connected to, but not solved by, this gap; see the rate/dt entry above.)

## Review basis and limits

Based on a documentation and source-structure review (file sizes, CI/test configuration, fixture and module layout, cross-checked against AGENTS.md and the module READMEs) plus the historical findings in the three superseded documents. It is not a fresh line-by-line correctness audit, a security audit, or a performance study, The 2026-10-19 release-scope update also checked current local runner/Scope code and test registration, and incorporates the Scope regressions verified during the preceding fix. No fresh numerical audit, performance study or full test run was performed for this documentation-only update. Treat items here as a planning backlog, not a defect list with reproduction steps — consult `git log` on the removed files for the reproduction detail behind any already-resolved finding this document does not repeat.

The 2026-10-20 implementation audit additionally inspected save/lifecycle paths,
committed document protection and run-generation fixes, and ran the new GUI
suite on Xvfb/Mesa with real 2D/3D pixels. Current execution evidence and remaining
release scope are recorded in DEVELOPMENT_PLAN.md; earlier review limitations
above describe their original dates rather than superseding this new evidence.

### O1a observation separation verified

Delivered-sample ownership is extracted from viewer
windows into a window-independent, disk-backed observation source. Viewers read
bounded pages through explicit bindings; their lifetime and clear action must
not delete source data. O1b now captures before the preview mailbox. Standalone delivered-only ingestion still reports upstream capture loss explicitly.
