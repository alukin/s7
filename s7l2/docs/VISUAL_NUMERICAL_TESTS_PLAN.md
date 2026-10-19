# S7 Visual Numerical Test Plan

Date: 2026-10-19
Status: living document — update in place as stages complete or the design changes.

Companion to `../../doc/S7_Numerical_Modeling_Test_Plan.md` (the language-neutral validation methodology: stages, reference-solution discipline, metrics, acceptance criteria) and to `DEVELOPMENT_PLAN.md`/`OPEN_PROBLEMS.md`. This document is the S7L2-specific (C++) implementation plan for that methodology, extended with a visual/diagram-based test track: each numerical test case is represented as an actual diagram with explicit observation points, runnable both headlessly (for pass/fail) and visually in Studio (for human inspection), with generated plots (time series, phase portraits, attractor projections, convergence) as reviewable artifacts.

## Next-release role and current evidence

The next release has one goal: dependable local simulation in Studio with its bundled runtime. [`DEVELOPMENT_PLAN.md`](DEVELOPMENT_PLAN.md) defines its acceptance criteria. This document remains the longer-term numerical methodology; completing every research stage below is not a condition of that release. Select a supported example/configuration matrix, validate it independently, and label examples outside the validated envelope experimental. Broad nonlinear integrator comparison, new methods, large-scale studies and the full Lorenz/statistical programme remain later research.

Implemented evidence as of 2026-10-19:

- Stage 0 numerical harness and Stage 1.1 scalar analytical/convergence checks (`s7tests/numerical_harness.hpp`, `linear_scalar_smoke.cpp`).
- Mandatory rate inference/validation in compilation, including schema-rate seeding on the Studio path.
- Headless diagram/probe-sidecar runner (`s7tests/diagram_harness.hpp`), checked against the scalar case (`diagram_harness_smoke.cpp`).
- Focused plain-Euler amplification and symplectic-oscillator tests (`symplectic_oscillator_smoke.cpp`). These do not complete the damped/forced oscillator or coupled-system stages.
- Nine small-angle double-pendulum diagrams (slow/fast/mixed modes × Euler/Symplectic/implicit Trapezoid), with continuous normal-mode and closed-form discrete references: [`examples/double_pendulum`](../examples/double_pendulum/README.md). The headless suite covers 27 configurations, 20 seconds each at 1/10/100 kHz, reporting state error, energy and convergence (first order for Euler/Symplectic, second order for implicit Trapezoid). A Studio scene-loader/bundled-runner comparison exercises the same models. This is conservative coupled-linear coverage, not completion of the damped/forced or general nonlinear stages.
- Three nonlinear single-pendulum diagrams with a long-double elliptic reference, independent RK4/AGM cross-checks and predeclared quality budgets: [`examples/pendulum`](../examples/pendulum/README.md). Six 100-second runs at 1/10 kHz verify trajectory, period, energy, refinement and implicit endpoint residuals. Symplectic and Trapezoid pass; Euler fails accuracy as expected. The bundled runner and real Scope capture all 6.6 million values without drops, match headless values exactly and export exact double round trips. Reproducible gnuplot scripts produce trajectory/error figures for this benchmark.
- Studio Scope preview regression (`s7studio_scope_view`) and actual-example delivery/window-lifecycle regression (`s7studio_scope_run`, `s7studio --scope-run-check`): ten model seconds of `examples/2int_osc.s7diagram` at 1/10/100 kHz, including opening after execution and closing/reopening. The unit view test also checks that Auto Y preserves a zoomed time range. This is focused integration evidence, not a complete convergence/long-run numerical study.

- `s7studio_scope_lifecycle`: reference-matched two-channel pendulum pause/resume/stop/reset/rerun and recording snapshots, disk-backed saved-result comparisons, import rollback, mailbox overflow accounting and injected temporary-directory failure. [Scope results](SCOPE_RESULTS.md) documents persistence and comparison semantics. This does not qualify native dialogs, disk exhaustion or a general throughput envelope.

- `s7studio_phase_portrait`: [two-integrator oscillator with a PhasePortrait observer](../examples/2int_osc_phase_portrait.md), ten seconds at 1/10/100 kHz. Every plotted X/Y pair must match same-step headless states exactly and the closed-form Euler recurrence within `1e-10`; tests cover axis mapping/ranges, late opening, reopening, clear and step pairing. Broader live phase rendering and overload qualification remain open.

- `s7studio_phase_trajectory_3d`: [three-integrator helix](../examples/3int_helix_phase_trajectory.md), ten seconds at 1/10 kHz. Every point in XY/XZ/YZ must match same-step headless states exactly and the discrete analytical solution within `1e-10`; checks include wiring, axes/ranges, opening before/after execution, reopening, clearing and coordinate pairing.

Not completed: a general gnuplot artifact pipeline (section 3), the full staged model/reference suite, headless/Studio trace equivalence for the supported release matrix, capture/export completeness and overload qualification, and broader run-control/phase-view lifecycle coverage. The single-pendulum benchmark validates its stated envelope; other illustrative nonlinear/chaotic diagrams remain unvalidated.

### Local release validation matrix to build

| Track | Required comparison | Coverage to add |
|---|---|---|
| Effective configuration and time | Saved diagram, effective element rates, requested duration, steps and model timestamps | First/final published samples; frequency changes including 1/10/100 kHz; save/reload and nested-schema consistency |
| Numerical values | Independent analytical/reference result with per-case tolerances | Scalar dynamics, plain-Euler oscillator, bounded oscillator realization, selected coupled/nonlinear cases; refinement, phase/amplitude and energy metrics where applicable |
| Runtime/client equivalence | Same effective model and probes through headless and bundled Studio paths | Match values/timestamps at matching steps; repeat runs and observation/window-state changes; account explicitly for causal-memory publication |
| View lifecycle | Captured trace versus displayed time extent and curve | Open before/after run, hide/reopen, multi-channel views, zoom/pan, fit/Auto Y, phase views; pause/resume/stop/reset/rerun and clear/overlay |
| Recording and resource use | Delivered samples versus CSV; produced versus delivered sample counts | Overload/loss reporting, temporary-file/disk failures, long runs, queue/preview bounds, disk growth and measured UI/stop responsiveness |

Every release case must name its reference, parameters, method, `dt`, duration, expected sample/time alignment and tolerances before it can count as validated. Keep numerical/reference checks headless, and add Studio integration checks for the complete user path. A Scope screenshot or a synthetic sine-buffer test cannot substitute for actual model execution and recording checks.

## 1. Design decided so far

### 1.0 The analog-computer correspondence

`doc/S7_Numerical_Modeling_Test_Plan.md`'s "core approach" already states this: model the ODE as the causal graph an analog computer would have been patched into, and replace only the analog integrator with a discrete one. This plan makes that correspondence an explicit, named convention for every diagram and test case built under it, not just background motivation:

| Analog computer (1940s–1970s: EAI, Philbrick, Comdyna) | S7 element / concept |
|---|---|
| Integrator (op-amp + feedback capacitor; the one place state/energy is actually stored in the patch) | `EulerIntegrator`, `TrapezoidIntegrator`, or whatever integrator-like causal-memory element is under test (Stage 5) — this is the *only* thing this plan swaps relative to the analog original |
| Summing amplifier | `Adder` (weighted sum) |
| Inverter | `Gain(-1)` |
| Coefficient potentiometer | `Gain` (general `S7` gain is an unrestricted scale factor, not limited to the pot's 0–1 attenuation range — closer to a pot plus an amplifier stage) |
| Quarter-square / servo multiplier (for genuine nonlinear products, e.g. Van der Pol's `x²ẋ`, Lorenz's `xy`/`xz`) | `Multiply` |
| Patch panel and patch cords | the diagram's `connections` (wires) |
| IC (initial-condition) mode | an integrator element's own `initial_value` parameter |
| Operate mode | the normal step loop (`runtime.step()` repeated) |
| Hold mode | pausing the step loop (no S7 runtime state models this explicitly today; conceptually, just stop calling `step()`) |
| Oscilloscope/X-Y-plotter probe clipped onto a summing-junction or integrator output — non-loading, external, doesn't affect the circuit | a `ProbePoint` read through `ObservationService` (section 1.1) |
| X-Y scope mode (two probed voltages plotted against each other — the classical way phase portraits, limit cycles, and Lissajous figures were displayed) | a phase-portrait plot: two probes plotted against each other (section 3) |
| Strip-chart/pen recorder | a time-series plot: one probe vs. model time (section 3) |

Two consequences worth stating explicitly:

- **Element naming in test diagrams should use this vocabulary** where it helps readability: e.g. `integrator_x`, `summer_1`, `coeff_a`, `probe_x`, `probe_v`, rather than generic `e1`/`e2` ids. This is a documentation/authoring convention for this plan's diagrams, not a code or schema change.
- **Trapezoidal feedback integration is now implemented (2026-10-19).** Default `TrapezoidIntegrator` is causal memory. NodeRuntime solves its coupled endpoint states through stateless derivative dependencies, stages the result and preserves normal next-step publication. Artificial delays are no longer needed in the pendulum examples. The separate `mode="sampled"` retains direct-feedthrough quadrature of supplied samples. See [the numerical contract](TRAPEZOID_INTEGRATION.md) for held-input semantics, fixed-point convergence limits, unsafe-stateful-dependency rejection and tests. This is an implemented integration stage, not permission for arbitrary algebraic cycles.

### 1.1 Observation points are probes, not elements

The project's own architecture already settled this; Studio's `Scope` used not to follow it, and now does — see `OPEN_PROBLEMS.md`'s "Observation architecture: Studio's `Scope` contradicted ADR-011/012 — fixed". For this plan, a probe is:

- a read-only tap on an existing element's output port (`ObservationService`'s existing `ProbePoint{element_id, port}` concept), not a graph element;
- visible on a diagram as its own thing, distinct from functional elements — implemented in Studio (2026-10-15) as a small marker drawn partway along the tapped wire, the same decoration mechanism `SignalWire` already used for its type label, placed via a wire's right-click menu ("Add Probe Here");
- zero effect on validation, type inference, compilation, causal-cycle requirements, or execution order — attaching or removing a probe never changes what the functional graph computes;
- read as discrete per-step values — the existing `ObservationSample`/`ObservationSnapshot` model (step, optional `model_time_ns`, typed value) is sufficient; there is no analog/continuous sampling to add;
- never multiplexed onto the inter-node data stream if the probed wire is ever on a remote node — remote observation is explicitly out of scope for this plan (see `OPEN_PROBLEMS.md` P3) and must get its own contract later, not reuse `NetSource`/`NetSink`'s stream protocol.

Not implemented: moving a placed probe to a different wire (today it is remove-and-re-add); this plan's own headless runner has no notion of "while a diagram is running" at all, so this only matters for Studio's interactive case, and is a small follow-up, not a design gap.

### 1.2 Probe list format: a sidecar file for this plan; Studio now has its own native format

Checked directly against `s7studio/src/diagram_document.cpp`: `DiagramDocument::toJson()` rebuilds the top-level JSON object from only the fields it knows (`id`, `dt`, `document_kind`, `diagram_version`, `sheet`, `schema`, `diagram`, `runtime_nodes`, `streams`, and — since 2026-10-15 — `probes`). A new unknown top-level key would have been silently dropped the next time Studio opened and re-saved a diagram, which was the original reason this plan's own test diagrams use a separate sidecar file instead: `<case>.s7diagram` next to `<case>.probes.json`, read directly by `s7tests/diagram_harness.hpp`, independent of Studio.

Studio itself is now taught about probes natively (`OPEN_PROBLEMS.md`, "Observation architecture: Studio's Scope contradicted ADR-011/012 — fixed"): `DiagramDocument::probes`, a `std::vector<DiagramProbe>`, serialized as the diagram's own top-level `"probes"` array — the same per-probe shape as this plan's sidecar (`element`, `port`, `name`, `unit`), just embedded rather than alongside. **The two are not yet unified**: a diagram authored in Studio carries its probes inline; a diagram authored for this plan's headless runner still uses the sidecar. Converting between them is a one-line transform (lift the array out, or embed it in), but nothing automates it yet — a real follow-up, not done in this pass.

The sidecar (and Studio's embedded array) each have the probe entries themselves:

- `"probes"`: `{"name": ..., "element": ..., "port": ..., "unit": ...}` objects. `name` is for labeling and need not match the element id; `unit` (optional, e.g. `"rad"`, `"m/s"`) is for plot axis labels only, nothing else reads it.
- `"views"` (sidecar only; Studio has no equivalent concept yet, since it opens live windows interactively instead of generating plots from a file): which plots section 3's driver should generate for this case, referencing probes by `name` — e.g. `{"type": "phase_portrait", "x": "position", "y": "velocity"}`, `{"type": "attractor3d", "x": "x", "y": "y", "z": "z"}`. Explicit per case, not auto-generated from every probe combination — matching the project's general preference for explicit over inferred behavior (explicit network boundaries, explicit type conversion; "prefer explicit errors over silent coercion" in `AGENTS.md`'s guardrails).

### 1.3 dt/rate inference and validation — implemented; configuration parity still needs tests

**Implemented:** `s7::Rate`, element-declared rates, schema-rate seeds and mandatory `RateResolver` checks in `Validator::validate()`/`Compiler::compile()`. Stage 1.1 now explicitly checks the resolved rate. Studio's loader reads schema `dt`, and `LocalSchemaRunner` seeds the run from its authoritative `dt`. The headless diagram runner seeds the schema from the diagram's top-level `dt`.

Rate resolution validates agreement; it does not rewrite every element's parameters. Studio's run configuration separately updates rate-related parameters before building the run. The headless diagram runner uses the stored parameters. Release comparisons must use the same effective configuration, report it, and test stale/conflicting parameters and nested schemas explicitly rather than assuming these paths are equivalent.

**One rate per connected model is this release's scope.** Hardware-native rates, explicit rate-changing elements and scheduling genuinely multi-rate graphs remain deferred. The implemented inference/validation is the prerequisite already met; multi-rate execution is a separate capability. See `../../doc/S7_Sample_Rate_Model.md` for the design and `OPEN_PROBLEMS.md` for implementation history.

### 1.4 Observation-side decimation, not a new functional element, for plotting at a lower rate

`S7_Observation_Architecture.md` places decimation/FFT-style processing in the Observation Graph, not the Functional Graph. So when a test wants to plot fewer points than it simulates (e.g. Lorenz run for 10^5 steps, 2000 plotted), that is a stride in the harness's sampling loop, not a new element. A genuine rate-conversion *element* is a different, legitimate need (two parts of one model computing at different native rates, feeding each other as part of the dynamics — `NetSource`/`NetSink`'s explicitness is the right template for it) but nothing in Stages 1–4 requires it; tracked as a Phase 5 item in `DEVELOPMENT_PLAN.md`, not part of this plan.

### 1.5 Derived quantities are computed by ordinary Functional Graph elements, not Observation Processing

Energy, residuals, modal measures, and similar derived quantities are *not* an Observation-layer concern here, despite `S7_Observation_Architecture.md` placing decimation/FFT-style "Observation Processing" in the Observation Graph. The difference: decimation/FFT operate on already-captured observation data for a client's own display purposes, whereas something like the pendulum's energy, `E = ½v² + ω₀²(1 − cos x)`, is an ordinary function of the model's own state — the same kind of computation any engineer might wire into a real model (to drive a controller, trigger an event, or just monitor a derived signal), not something specific to this test suite. The right way to get it is to build it in the Functional Graph from ordinary elements (`Multiply` for `v²`, a trig element for `cos x`, `Gain`/`Adder` for the rest) and then probe its output exactly like any other signal — no new Observation-layer mechanism needed.

This is a stronger and simpler answer than this plan's first draft proposed (computing derived columns ad hoc in the C++ harness): it generalizes to real models, not just test diagnostics, and it means a probe is always just `{element, port}` (section 1.2) with nothing special about a "derived" probe.

**The pendulum exposed two core math requirements.** `Trig` now applies nonlinear functions such as `sin` and `cos` to arbitrary inputs, and `Divider` provides real floating-point division for the full equations' state-dependent denominators. The complete nonlinear double-pendulum graph is in `examples/double_pendulum_full.s7diagram`; the small-angle linear model is in `examples/double_pendulum_small_angle.s7diagram`.

### 1.6 Complex-valued probes

Not needed by any stage currently planned (Stages 1–5's state signals are all real `F64`, even where Stage 1.3's system matrix has complex-conjugate eigenvalues — the eigenvalues are complex, the state trajectories stay real), but worth settling now rather than discovering it mid-stage, since `ObservationSample::to_display_double()` deliberately throws on any complex type (`s7observe/src/observe.cpp`: *"A complex value has no single display value"*) — the export step in section 2 cannot call it unconditionally.

- A probe on a complex-typed port exports as two columns, `<name>.re`/`<name>.im` (from `as_complex()`), never a single display-reduced number — lossless, and every other view derives from these.
- Magnitude/phase for a plot only (never fed back into the model) is derived in the gnuplot script from `.re`/`.im` (`sqrt($2**2+$3**2)`, `atan2($3,$2)`) — a display transform, same spirit as `to_display_double()` itself being display-only.
- Magnitude/phase needed *as a modeled signal* (e.g. a demodulator) follows section 1.5's rule instead: build it from the existing `ComplexToMagnitudePhase` element and probe its real-valued outputs — don't special-case this in the visualization layer.
- An I/Q constellation or complex-plane trajectory view is the existing phase-portrait template (section 3) applied to one probe's `.re`/`.im` columns instead of two separate probes — no new plotting mechanism, just this reuse.
- A spectrum/frequency-domain view is out of scope until `s7spectral` gets frame/block signal types (`OPEN_PROBLEMS.md` P3); `WindowedFFT` currently outputs only a scalar dominant frequency/amplitude, so there is no spectrum to plot yet.

## 2. Headless diagram runner — implemented (2026-10-15)

A small, Qt-free addition to `s7tests/` (`diagram_harness.hpp`), extending the existing `numerical_harness.hpp`/`linear_scalar_smoke.cpp` pattern rather than replacing it:

1. Parse a `.s7diagram` file (`boost::json`, already a dependency); take its `schema` object and top-level `dt`.
2. Build the `s7::Schema` from `schema.elements`/`connections` the same way `s7core/tests/type_inference_smoke.cpp` already does for JSON-parameterized elements (`ElementRegistry::find(type)` + `make_element_spec`), not by hand-constructing C++ objects — this is what lets the same diagram run unmodified whether it came from Studio or was hand-written.
3. Seed `Schema::set_rate()` from the diagram's top-level `dt` and call `NodeRuntime::set_model_time_step_ns()` with it, per 1.3 — no per-element propagation: RateResolver (run inside step 4 below) is what actually proves every element's own rate (if it has one) agrees, rather than this step rewriting it by hand.
4. `Validator::validate` / `Compiler::compile` / `NodeRuntime`, exactly as today.
5. Parse the matching `.probes.json` sidecar into `ProbePoint`s; construct one `ObservationService`.
6. Step loop: step the runtime, `snapshot()` the probes (optionally strided, per 1.4), append to an in-memory trace.
7. Two outputs from the same run:
   - pass/fail checks against an independently-computed reference (closed form, matrix exponential, conserved quantity, limit-cycle measure, or statistics, per the stage — see `doc/S7_Numerical_Modeling_Test_Plan.md` section 3.2/3.3), asserted the same hand-rolled way as `linear_scalar_smoke.cpp` (`assert`/`throw`, no framework);
   - optionally (`S7TESTS_DUMP_TRACE` env var, off by default so `ctest` stays fast), a full trace dump to a plain text table (`step model_time_s <probe name> <probe name> ...`), one row per sampled step, for visualization. A complex-typed probe contributes two columns (`<name>.re`, `<name>.im`), per section 1.6, not one.

Validated against a re-expression of Stage 1.1 as a diagram, per section 6 step 4: `s7tests/diagrams/euler_scalar_decay.s7diagram` + `.probes.json`, run through `diagram_harness_smoke.cpp`, reproduces `linear_scalar_smoke.cpp`'s own result bit-for-bit (`max abs error=0.00995017` in both). Verified on both the native Linux build and the Windows cross-build (`ctest` under Wine).

This is additive to what exists: `linear_scalar_smoke.cpp`'s hand-built-in-C++ schema stays as-is (it is a fine, lighter-weight pattern for a single small case); the diagram runner is for cases worth keeping as an actual, Studio-openable artifact (any case with a probe-point/visual-inspection story — in practice, Stage 1.2 onward).

## 3. Visualization pipeline (separate from ctest pass/fail)

gnuplot (already installed; matplotlib is not, and is not worth adding as a dependency for this). Not run by `ctest` and not asserted on automatically — plots are for human review, per `doc/S7_Numerical_Modeling_Test_Plan.md` section 10 ("a visually plausible plot alone is insufficient" as a *pass* criterion, but still required reporting). Generated on demand by a small driver script (not a build target run on every `cmake --build`) reading `s7tests/diagrams/<case>.s7diagram` + `.probes.json`'s `views` array (section 1.2) plus a trace dump, writing PNGs next to the trace in the build tree (regenerable, not committed). Template scripts themselves live under `s7tests/visualize/` and are committed. (Gnuplot invocations below are illustrative of the mechanism, not verified syntax — no code runs in this session; confirm exact flags when the templates are actually written.)

### 3.1 Time series

One probe (or, for a complex probe, its magnitude/phase derived from `.re`/`.im` per section 1.6) vs. model time. The straightforward template: `plot 'trace.dat' using 2:N with lines` (column 2 = model time, column N = the probe). Covers the invariant/energy-drift view too — the probed output of the diagram's own energy-computing elements (section 1.5) vs. time; the script does no physics, it only plots a named column.

### 3.2 Phase portraits (2D)

**For every stage here, the phase-space coordinates are integrator outputs, with nothing in between.** Building the pendulum's first-order form (`ẋ = v`, `v̇ = −ω₀²sin(x)`) as a causal graph already needs two integrators — one producing `x` from `v`, one producing `v` from `−ω₀²sin(x)` — so a phase-portrait probe is just `{element: "x_integrator", port: 0}` / `{element: "v_integrator", port: 0}`, no `Trig`/`Multiply`/`Gain`/`Adder` involved. Same pattern for Van der Pol (two integrators) and Lorenz (three, giving the full attractor coordinates directly). This is the same analog-computer practice named in section 1.0 — scope channels clipped straight onto the two integrator outputs, X-Y mode — and it draws a clean line worth keeping in mind through the rest of this plan: raw state-space trajectories/attractors are "free" probes on integrators the model already has for its own dynamics; only *derived* diagnostics (energy, period/amplitude measures, ...) need the extra element work from section 1.5.

Two probes against each other — position vs. velocity for the pendulum/oscillator/Van der Pol cases, or a complex probe's own `.re`/`.im` columns for an I/Q constellation/complex-plane view (section 1.6, no separate mechanism). Two distinct recipes, both needed, for different questions:

- **Single trajectory, time-colored** — shows a transient settling into a limit cycle within one run: `plot 'trace.dat' using X:Y:1 with lines lc palette` (color = step index via the active palette). This is how a transient spiral into the Van der Pol limit cycle, or a pendulum trajectory's shape, becomes visible in one static image without needing arrows.
- **Multiple initial conditions overlaid** — one line per run, one color per run (not per time), answering a different question: do different starting points converge to the same cycle/behavior (Stage 2's multiple amplitudes, Stage 3's "convergence from different initial conditions")? Each IC's trace is a separate file/line in the same `plot` command.

**Reference overlay:** the C++ test already computes an independent reference trajectory for its own pass/fail check (closed form, matrix exponential, or high-accuracy numerical solve) — dump it to a trace file in the same column format and add it as a second line (thin/dashed vs. the S7 trace's thick/solid) in the same plot, rather than inventing a separate comparison mechanism. For Lorenz specifically (section 3.3), once the trajectories diverge — expected, per the test plan's chaos-handling guidance — the overlay stops meaning "do these match pointwise" and starts meaning "do both trace the same attractor shape"; caption the plot accordingly rather than leaving that to be guessed.

### 3.3 Attractors (3D)

Three probes (Lorenz x, y, z): `splot 'trace.dat' using X:Y:Z:1 with lines lc palette` (true 3D, time-colored the same way as 3.2) with a fixed `set view <elevation>,<azimuth>` recorded once a legible angle is found by actually rendering it — not guessable in advance, and per the test plan's reproducibility rule (section 3.1), whatever angle is chosen gets written down, not left to gnuplot's default. The "standard 2D projections" (classically (x,z) for the Lorenz butterfly, plus (x,y) and (y,z)) are not a separate mechanism — they are section 3.2's 2D phase-portrait template, run three times on the same trace file's column pairs.

### 3.4 Convergence

Error vs. h on log-log axes (`set logscale xy`), with the expected order's slope drawn as a reference line for comparison — a meta-plot across several separate runs' summary errors (already computed by the harness's `observed_order` helper), not a single run's trace at all.

### 3.5 Client-type precedent

Time-series and phase-portrait/attractor views are deliberately separate templates, not modes of one: `S7_Terminology.md` already lists `Scope` and `Spectrum Analyzer` as distinct Observation Client types (alongside `Studio` and `Logger`), and, for exactly this kind of split, Simulink's Sinks library has `Scope` (time-domain) and `XY Graph` (phase-plane) as two separate blocks rather than two modes of one block. This plan's gnuplot templates follow that precedent; Studio's `Scope` is now an Observation Client (section 5) and remains a time-series client, with phase-portrait/attractor viewing as its own, separately named client type, not a mode added to Scope.

## 4. Layout

- `s7tests/diagrams/<case>.s7diagram`, `s7tests/diagrams/<case>.probes.json` — one pair per test case, Studio-openable. `euler_scalar_decay` exists (the runner's own validation case, section 2).
- `s7tests/diagram_harness.hpp` — the loader/runner from section 2. Done.
- `s7tests/visualize/*.gp` — gnuplot templates from section 3, plus one small driver script. Not done.
- `s7tests/<stage>_<case>_smoke.cpp` — one `ctest`-registered executable per case, following the existing `s7tests_linear_scalar_smoke` naming and registration pattern in `s7tests/CMakeLists.txt`. `diagram_harness_smoke.cpp` exists (the runner's own validation, not yet a numbered stage).

## 5. Explicitly deferred, not part of this plan

- **Done, no longer deferred:** migrating Studio's `Scope` off `ScopeElement` onto a real Observation Client / fixing the ADR-011/012 violation, and drawing probes as a distinct visual symbol in the diagram editor (2026-10-15, `OPEN_PROBLEMS.md`). Unifying Studio's native embedded `"probes"` array with this plan's own sidecar-file convention (section 1.2) is a small follow-up, not done.
- How a genuinely multi-rate graph would *execute* (as opposed to the rate resolver's inference/validation, which is already implemented, not deferred) — a Functional Graph resample element, and the scheduling question behind it, for models that actually need more than one rate domain (`../../doc/S7_Sample_Rate_Model.md` section 6). Tracked as a `DEVELOPMENT_PLAN.md` Phase 5 item.
- Remote observation (section 1.1's last point). Already tracked in `OPEN_PROBLEMS.md` P3.

## 6. Execution order

Follows `doc/S7_Numerical_Modeling_Test_Plan.md` section 12, with the diagram+probes+visualization track layered on from Stage 1.2 onward (Stage 1.1 needs no diagram or phase portrait — one scalar trajectory against a closed form needs no visual story):

1. **Blocking prerequisite for every stage below, including re-confirming Stage 1.1 — done.** `../../doc/S7_Sample_Rate_Model.md`'s `RateResolver` is implemented and mandatory in `Validator::validate()`/`Compiler::compile()` (`OPEN_PROBLEMS.md`, "Rate/`dt` inference and validation").
2. Stage 0 harness — done (`numerical_harness.hpp`).
3. **Stage 1.1 scalar linear — re-confirmed against the resolver (2026-10-15), not just re-passing by inspection.** `linear_scalar_smoke.cpp` (`EulerIntegrator` vs. analytical decay, convergence order, forced-response steady state) now has a `test_rate_resolver_confirms_one_rate()` check that calls `RateResolver::resolve()` directly and asserts it resolves exactly 1/`dt` Hz for every element in the integrator loop — the schema needed no `Schema::set_rate()` seed, since its one `EulerIntegrator` already pins its one connected component.
4. **Headless diagram runner (section 2) — done (2026-10-15).** Runs every schema through the resolver from step 1 (not hand-propagating `dt`); validated against a re-expression of 1.1 as a diagram (`diagrams/euler_scalar_decay.s7diagram`, `diagram_harness_smoke.cpp`) reproducing its result bit-for-bit.
5. Stage 1.2 linear second-order oscillator, as a diagram with position/velocity probes. Focused Euler/symplectic tests, the Studio example regression and the nine coupled-pendulum examples exist; damped/forced cases and generated phase-portrait artifacts remain. The coupled suite supplies continuous/discrete reference and convergence reports for its stated configurations.
6. Stage 1.3 coupled linear systems: the four-state small-angle double-pendulum suite covers known normal modes. Larger/general matrices and scaling studies remain.
7. Stage 1.6 larger-graph/performance checks (kept numeric-only, no visualization story).
8. **Blocking math prerequisites for Stage 2 onward — implemented.** `Trig` supplies real floating-point trigonometric functions and `Divider` supplies real floating-point division. Van der Pol and Lorenz also use the existing `Multiply`; the full pendulum additionally uses `Divider` for its nonlinear denominators.
9. Stage 2 pendulum — dynamics and energy both built from ordinary elements (section 1.5); energy-drift plot, phase portrait, multiple amplitudes.
10. Stage 3 Van der Pol — limit-cycle phase portrait.
11. Stage 4 Lorenz — 3D attractor projections, statistical/geometric checks.
12. Stage 5 — reuse every case above across integrators/dt values to investigate integrator dynamics, per the test plan's hypothesis in its section 9.
