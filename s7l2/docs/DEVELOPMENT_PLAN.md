# S7 Development Plan

Date: 2026-10-19
Status: living document — update in place as phases complete or the sequence changes; do not create a new dated copy.

This is the sequencing companion to `docs/OPEN_PROBLEMS.md`: that document lists what is unsolved, this one orders the work. The next-release section defines the current commitment; the later phases preserve the longer-term backlog.

## Next release: dependable local simulation in Studio

**Decision: 2026-10-19. One release goal: good simulation quality in `s7studio` with its bundled runtime.** The target user builds or opens a causal model, runs it locally, and can trust the computed values, model time, plots, and recorded results. No release number or delivery date is assigned here.

“Bundled runtime” means Studio's existing in-process `LocalSchemaRunner`, using `s7core` validation, compilation and `NodeRuntime` on a worker thread. Local simulation must work without starting a separate `s7runtime` service, discovering nodes, provisioning certificates, or deploying a diagram. Core/runtime logic stays headless and independently testable; Studio configures execution and observes it. This release does not introduce a second numerical engine in the UI.

### Release boundary

In scope: single-node, single-rate simulation; correct rate/`dt` handling; causal state publication; the accuracy and documented limitations of existing integrators; deterministic run controls; reliable Scope and phase views; observation delivery, recording/export, bounded resource use and useful errors. Fix editor, loader, plugin or packaging problems when they prevent that local workflow.

Deferred: distributed scheduling/deployment expansion, remote observation, hardware acquisition, multi-rate execution/resampling, snapshots/restore, production credential/operations work, broad architectural refactoring, and new integrators or a systematic nonlinear integrator comparison. Existing network features remain available, but expanding them is not a next-release objective. Revisit deferred work after this release's acceptance criteria pass; a demonstrated local simulation blocker can bring a specific item forward.

### Current evidence and remaining work

| Area | Implemented evidence | Still required for release |
|---|---|---|
| Numerical execution | Mandatory rate validation; scalar analytical/convergence harness; Euler/symplectic oscillator checks; nine coupled-pendulum diagrams with continuous/discrete references at three rates; implicit trapezoidal stage; reference-backed nonlinear pendulum over 100 s at 1/10 kHz (Symplectic/Trapezoid pass; Euler fails accuracy as expected) | Reference-backed acceptance for the supported example/configuration matrix; model-time/state alignment and timestep refinement checks |
| Studio delivery and views | Worker-thread execution and bounded sample mailbox; `s7studio_scope_view`, `s7studio_scope_run` and `s7studio_scope_lifecycle` regressions (two-channel pause/resume, stop while paused, reset/rerun, mailbox overflow and temporary-directory failure) | Broaden phase-view controls, storage-write failures, long-run UI responsiveness and shutdown coverage |
| Scope lifecycle | Uniform preview stride; recording starts before opening a view and survives hiding/reopening; Auto Y preserves the time range; all 6.6 million nonlinear-pendulum CSV rows preserve exact double values; saved `.s7scope` comparisons, snapshot/append fidelity and disk-backed imported zoom checks | Actual disk exhaustion, large-import/export responsiveness, per-comparison controls and wider platform coverage |
| Reproducibility | Headless diagram harness, Studio oscillator test, and coupled-pendulum bundled/headless sample comparisons | Compare the same saved model/configuration across both paths; document supported platforms and resource envelope |

These checks establish specific behavior, not general numerical correctness. In particular, the Scope run regression's ten-second sine comparison at 1/10/100 kHz is a rendering/delivery regression with a tolerance; it does not prove long-term energy conservation or validate all integrators.

### Work sequence

1. **Establish the local simulation baseline.** Reproduce failures from saved diagrams through the complete open → configure → validate → run → observe → export workflow. Start with `examples/2int_osc.s7diagram` at 1, 10 and 100 kHz over the same model duration. Record effective parameters, method, initial conditions, rate, step count, first/last timestamps, probe mapping and any dropped samples. Distinguish numerical-method limits from scheduling, timestamp and display defects before changing semantics.
2. **Validate values and model time together.** Extend the existing headless harness and Studio tests with independent references and per-case tolerances. Cover scalar dynamics, the two-integrator oscillator, an energy-bounded oscillator realization, and representative coupled/nonlinear models selected from the bundled examples. Verify the first published state, its step/time label, the final sample and convergence as `dt` decreases. Characterize forward Euler's known oscillator drift rather than requiring impossible exact conservation. Preserve causal-memory publication order; any runtime change needs a core smoke regression.
3. **Make observation trustworthy throughout a run.** Test full-run and zoomed views, multiple channels, open-before/open-after-run, close/reopen, pause/resume/stop/reset/rerun, clear and overlay behavior, CSV export and disk failures. Preview decimation must retain the model-time extent and useful curve detail. Observation overload must be visible and bounded; a disk recording of delivered samples cannot recover samples dropped before delivery. Select and test a delivery/recording policy for the supported rate/duration/channel envelope before claiming full-resolution capture.
4. **Make the result reproducible and releasable.** Exercise Studio-enabled builds and the local workflow in CI, with explicit dependencies and a supported compiler; keep headless numerical gates too. Check worker/UI lifetime and shutdown under load. Record reference metrics and reviewable traces/plots for the release matrix, then smoke-test the packaged Studio with its required local runtime dependencies/plugins. State which platforms were actually tested; Wine does not establish native Windows support.

### Acceptance criteria

The next release is ready when all of the following have evidence:

- A fresh Studio installation can open, configure and run the supported examples locally without an external service. Every bundled example is classified as **validated within a stated envelope** or **experimental with stated limits**; no illustrative nonlinear/chaotic diagram is advertised as reference-validated merely because it draws a plausible curve.
- Each validated numerical case records equations, parameters, initial conditions, method, effective `dt`, duration, independent reference, tolerances and results. State error, convergence and applicable amplitude/phase/energy behavior pass within that envelope. The suite distinguishes known method instability from implementation defects.
- The oscillator's 1/10/100 kHz runs preserve the same requested model duration, correct step/time alignment and full plotted time extent. Display sampling does not change model values or timestamps. Observation enabled/disabled and different window states produce the same runtime trace at matching steps.
- Run controls and view lifecycle tests pass without stale or mixed run data. Scope fit/zoom/pan and Auto Y preserve the selected time semantics. Exported values/timestamps match the recorded trace, and all capture loss or storage failures are reported rather than presented as complete data.
- A documented rate/duration/channel/graph-size envelope has measured throughput, bounded preview/queue memory and disk-use behavior. UI responsiveness, pause/stop latency and clean shutdown have measured bounds on the stated test machine. No universal throughput or real-time guarantee is inferred from discretization frequency.
- CI runs the named numerical and Studio integration gates; the release build passes them and a manual packaged-workflow check. Remaining local-simulation defects have explicit dispositions against these criteria.

Detailed test design: [`VISUAL_NUMERICAL_TESTS_PLAN.md`](VISUAL_NUMERICAL_TESTS_PLAN.md). Current gaps: [`OPEN_PROBLEMS.md`](OPEN_PROBLEMS.md). Current UI/runtime behavior: [`STUDIO_DIAGRAM_EDITOR.md`](STUDIO_DIAGRAM_EDITOR.md).

## Later work — retained backlog, not next-release gates

The phases below retain the broader project direction. Their numbering is a backlog reference, not a requirement to finish them for the Studio simulation release. Only the local validation, CI and lifecycle work selected above is part of this release; the complete cross-language, distributed, security and operations programmes remain deferred.

## Phase 2: Make validation repeatable

- Extend the existing `.github/workflows/ci.yml` job rather than treating CI as absent: add an explicit dependency-install step (Boost, OpenSSL, zlib, and Qt6 or `-DS7_BUILD_STUDIO=OFF`) and pin a toolchain that satisfies AGENTS.md's GCC 15+/Clang 22+ requirement, then confirm the job actually passes against that pinned configuration.
- Add a compiler/dependency matrix (supported Linux combinations, headless and Studio builds).
- Add a Windows cross-build plus Wine job, with a clear note that Wine is not native Windows validation.
- Decide whether to move `tools/test_two_node_deployment.sh` (registered as ctest's `s7_two_node_deployment`) under `s7tests/`, and keep module maps consistent with that decision. Extend that scenario with a standalone clean-shutdown case and a multi-cycle restart/recovery case.
- Promote fixture currency, plugin SDK smoke, and Python decoder checks to required (non-skippable) CI checks, and make cross-language fixture conformance a release gate.
- Add sanitizer jobs selectively: ThreadSanitizer for transport/runtime concurrency, AddressSanitizer/UBSan for core, parsing, and lifecycle paths. These have been run ad hoc (see resolved findings in git history) but are not a standing gate.
- Perform the concurrency/lifecycle audit called for in Open Problems P1 (runtime graph replacement, stream ownership, endpoint shutdown, Studio worker/UI handoff) before adding scheduler or recovery features in Phase 5.

**Addresses:** Open Problems P1 (validation/CI/whole-system-testing items).
**Completion criteria:** a clean checkout has reproducible build/test instructions and CI reports the same named gates; the end-to-end behavior is covered without using a developer's home directory or shared state; the concurrency audit's findings are either fixed or tracked as new Open Problems entries.

## Phase 3: Reduce change risk in high-churn code

Refactor by behavior boundaries, preserving public APIs and adding tests before moving code:

- Split `transport.cpp` into endpoint lifecycle, connection/session handling, TLS/identity, and stream routing units.
- Split `control_plane_mvp.cpp` into request validation/dispatch, deployment transaction/persistence, stream management, and response serialization units.
- Split `runtime_service.cpp` into JSON loading, graph/deployment lifecycle, scheduling/serve loop, and clock/state handling units.
- Break `main_window.cpp` and `diagram_scene.cpp` into UI actions/controllers and scene/model operations, leaving thin coordination entry points.
- Decide whether schema-backed element libraries should move from plain JSON files to an archive/zip format (open preference from the earlier status plan, never acted on); only build it if still wanted.

Do not split solely to reduce line counts. Each extraction should establish a testable boundary, make ownership explicit, and reduce the number of reasons a file changes.

**Addresses:** Open Problems P2.
**Completion criteria:** each module has a clear ownership map; high-risk behavior can be tested without launching all of Studio or a real network; no refactor changes externally visible protocol semantics.

## Phase 4: Close the gap between an MVP demo and an operated system

- Finalize a production credential story: enrollment authority, certificate profiles and role OIDs (replacing the current development placeholder OID), rotation/revocation, key protection, bootstrap trust, and recovery after loss of the control Studio. Keep this work separate from test fixture generation.
- Specify resource limits and operator behavior: maximum graph/schema and library sizes, plugin trust policy, stream buffer/log sizing, persistent state backup/restore, disk-full behavior, service supervision, and upgrade/restart behavior.
- Decide whether crash recovery is in scope. If it is, define durable payload retention and acknowledgement semantics before implementation. If it is not, surface the expected data loss and required operator action consistently in CLI, Studio, and docs.
- Add operational traces/metrics or structured logs with stable identifiers for deployment revision, node identity, stream ID, and fault; ensure secrets and private material are excluded.
- Perform an independent security review of TLS setup, certificate parsing and authorization, plugin loading, file/path handling, canonical JSON and digest validation, and deployment persistence before production use.
- Publish the operational profile that turns "lossless"/"real time"/"distributed" into supported graph size/rate, latency and clock assumptions, expected hardware, restart behavior, and resource ceilings.

**Addresses:** Open Problems P1 (production-readiness items).
**Completion criteria:** a documented threat/operations model and runbook; a credential lifecycle that can be exercised from provisioning through rotation; explicit behavior for all restart and storage failure paths.

## Phase 5: Evolve the language-neutral model deliberately

After the MVP boundary is stable, choose one major model capability at a time:

1. Add an acquisition-time source and validate `SOURCE_TIME` from a real or deterministic test source.
2. Decide if relay semantics are required; specify identity, buffering, latency, and failure propagation before adding relays.
3. Define distributed scheduling and coherence domains with formal invariants and executable examples before introducing `NetRuntime` or synchronized multi-node execution changes.
4. Add snapshots/restore only after element state serialization has a versioning and validation contract.
5. Add remote observation through explicit network/data contracts that preserve local observation's non-invasive behavior.
6. Give `s7spectral` frame/block signal types so `MagnitudeSpectrum`/`PowerSpectrumDB` can come back.
7. The harder, deliberately out-of-scope half of `../../doc/S7_Sample_Rate_Model.md` (the inference/validation half is already implemented and required by the local simulation baseline): how a genuinely multi-rate graph actually *executes* — single global tick vs. per-rate-domain scheduling vs. a hardware element buffering internally like `NetSource`/`NetSink` — once the rate-changing element it defines actually exists and something needs more than one rate domain in one running graph.

These items should not be bundled together. Each changes either semantics, persistent state, or protocol behavior and needs a design decision, conformance fixtures, and a vertical end-to-end demonstration.

**Addresses:** Open Problems P3.
**Completion criteria:** each capability has its own design record, conformance fixtures, and a working vertical demo before the next one starts.
