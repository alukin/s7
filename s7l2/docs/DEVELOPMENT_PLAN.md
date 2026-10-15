# S7 Development Plan

Date: 2026-10-14
Status: living document — update in place as phases complete or the sequence changes; do not create a new dated copy.

This is the sequencing companion to `docs/OPEN_PROBLEMS.md`: that document lists what is unsolved, this one orders the work. Phases build on each other — later phases assume the earlier ones are done — but a phase does not need to fully close before useful work on the next starts, except where noted.

## Phase 0: Establish the next deliverable

Create one short, dated project roadmap that names the target audience and release outcome (for example, a dependable two-node development MVP), lists explicit non-goals, and links to detailed design records. Update `docs/INDEX.md` so it points to the current roadmap and clearly separates normative contracts, implementation status, and open-problem tracking.

**Addresses:** Open Problems P0.
**Completion criteria:** every advertised capability is classified as implemented, experimental, or deferred; every deferred item has an owner/trigger or an explicit no-plan decision; no document presents completed work as upcoming or vice versa.

## Phase 1: Numerical-modeling validation and visualization

**Top priority.** Until this phase has passing, independently-referenced results, nothing about the system's computation can be asserted to be correct — every other phase below is hardening or extending a runtime whose numerical behavior is unverified. This phase does not depend on Phases 2–5 completing first and should start immediately; it will still benefit once Phase 2's CI hardening lands (a stable, reproducible way to run `ctest`), but does not need to wait for it.

Full design in `docs/VISUAL_NUMERICAL_TESTS_PLAN.md`, implementing `../../doc/S7_Numerical_Modeling_Test_Plan.md`'s staged methodology:

- **Stage 0 harness and a first Stage 1.1 scalar linear case — revisited against RateResolver (2026-10-15), still correct.** `s7tests/numerical_harness.hpp`/`linear_scalar_smoke.cpp` were built against the old hand-propagated `dt` convention; now that RateResolver is mandatory, a new `test_rate_resolver_confirms_one_rate()` check confirms, rather than assumes, that each schema's `EulerIntegrator` pins its connected component to exactly 1/`dt` Hz. No logic changed — the schemas already had exactly one rate-pinning element per component, so they needed no `Schema::set_rate()` seed — only the harness's stale "no formal model" comment did, and is now updated.
- **Hard prerequisite for every stage, not only Stage 2 onward — done.** Implemented `../../doc/S7_Sample_Rate_Model.md`'s inference/validation half: `s7::Rate`, `Element::declared_rate()`, `Schema::rate()`, `RateResolver::resolve()`, and (2026-10-14) wired into `Validator::validate()`/`Compiler::compile()` as a mandatory gate (`OPEN_PROBLEMS.md`, "Rate/`dt` has no formal model" — that entry has the full list of what fixing the resulting fallout touched: a `"dt"` JSON field on both schema-loading paths, deployment seeding from the diagram's own `dt`, and every affected test schema). A rate conflict, or no element/seed pinning one, is now a validation error naming the element(s), exactly like a type conflict. The headless diagram runner (below) now exercises this for real. **Studio fixed too (2026-10-15):** the mandatory gate had actually broken `s7studio_smoke` on `main` since the previous commit (any diagram with no rate-pinning element, e.g. a pure Gain/Adder/Multiply graph, failed to run in Studio) — not caught earlier because Studio needs Qt6 and wasn't built in the sandbox used to verify that change. Fixed in `local_runtime_v2.hpp::buildSchema()` (reads the same `"dt"` JSON convention as `s7runtime`/the schema-library format) and `LocalSchemaRunner::prepareRuntime()` (seeds the schema from the run's own authoritative `dt`, overriding whatever the JSON said, the same way it already overrides a stale discretization-frequency value). The design's deliberately out-of-scope multi-rate-execution half, and its `RatioOf`/per-port rate rule (needed only once a rate-changing element exists), stay with Phase 5 item 7.
- **Hard prerequisite for Stage 2 onward — done (2026-10-15).** Added a generic `Trig` element to `s7core` (`OPEN_PROBLEMS.md`, "No generic trigonometric/math element blocks nonlinear modeling"): `sin`/`cos`/`tan`/`asin`/`acos`/`atan`/`sinh`/`cosh`/`tanh` via a `function` parameter, real floating types, verified on both the native Linux build and the Windows cross-build. The pendulum's own dynamics need it, not just its energy diagnostic; Van der Pol and Lorenz need nothing beyond the existing `Multiply`. Complex-type support stays deferred (no complex-analytic trig in `s7::math` yet) until something actually needs it.
- **Headless `.s7diagram` + probe-sidecar runner — done (2026-10-15).** `s7tests/diagram_harness.hpp`, validated by re-expressing Stage 1.1 as a diagram and reproducing its result bit-for-bit; verified on both the native Linux build and the Windows cross-build. Next: work through Stages 1.2–5 (linear oscillator and coupled systems, pendulum, Van der Pol, Lorenz, integrator-dynamics investigation), each paired with a gnuplot-based visualization (phase portraits, attractor projections, energy drift, convergence plots) — the visualization pipeline itself (`VISUAL_NUMERICAL_TESTS_PLAN.md` section 3) is also not built yet.
- A further architecture follow-up surfaced by this work, not blocking but worth doing alongside it: migrating Studio's `Scope` off being a Functional Graph element (ADR-011/012 conformance, `docs/OPEN_PROBLEMS.md`).
- A genuine multi-rate/resample Functional Graph element (as opposed to observation-side decimation, which this phase handles without one) stays deferred to Phase 5 if it turns out to be needed.

**Addresses:** the numerical-validation and visual-test-tooling work requested directly, not an Open Problems entry.
**Completion criteria:** every stage in `doc/S7_Numerical_Modeling_Test_Plan.md` section 12 has a passing `ctest` case with an independent reference and, where the plan calls for one, a reviewable diagram/probe/plot artifact triple.

## Phase 2: Make validation repeatable

- Extend the existing `.github/workflows/ci.yml` job rather than treating CI as absent: add an explicit dependency-install step (Boost, OpenSSL, zlib, and Qt6 or `-DS7_BUILD_STUDIO=OFF`) and pin a toolchain that satisfies AGENTS.md's GCC 15+/Clang 22+ requirement, then confirm the job actually passes against that pinned configuration.
- Add a compiler/dependency matrix (supported Linux combinations, headless and Studio builds).
- Add a Windows cross-build plus Wine job, with a clear note that Wine is not native Windows validation.
- Either move `tools/test_two_node_deployment.sh` (registered as ctest's `s7_two_node_deployment`) under `s7tests/`, or update AGENTS.md's module map to say whole-system tests currently live beside the CLI they exercise. Extend that scenario with a standalone clean-shutdown case and a multi-cycle restart/recovery case.
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
7. The harder, deliberately out-of-scope half of `../../doc/S7_Sample_Rate_Model.md` (see Phase 1, which needs that design's inference/validation half much sooner, not this item): how a genuinely multi-rate graph actually *executes* — single global tick vs. per-rate-domain scheduling vs. a hardware element buffering internally like `NetSource`/`NetSink` — once the rate-changing element it defines actually exists and something needs more than one rate domain in one running graph.

These items should not be bundled together. Each changes either semantics, persistent state, or protocol behavior and needs a design decision, conformance fixtures, and a vertical end-to-end demonstration.

**Addresses:** Open Problems P3.
**Completion criteria:** each capability has its own design record, conformance fixtures, and a working vertical demo before the next one starts.
