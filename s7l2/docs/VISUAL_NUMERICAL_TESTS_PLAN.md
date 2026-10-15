# S7 Visual Numerical Test Plan

Date: 2026-10-14
Status: living document — update in place as stages complete or the design changes.

Companion to `../../doc/S7_Numerical_Modeling_Test_Plan.md` (the language-neutral validation methodology: stages, reference-solution discipline, metrics, acceptance criteria) and to `DEVELOPMENT_PLAN.md`/`OPEN_PROBLEMS.md`. This document is the S7L2-specific (C++) implementation plan for that methodology, extended with a visual/diagram-based test track: each numerical test case is represented as an actual diagram with explicit observation points, runnable both headlessly (for pass/fail) and visually in Studio (for human inspection), with generated plots (time series, phase portraits, attractor projections, convergence) as reviewable artifacts.

Code written so far: the Stage 0 harness and first Stage 1.1 test (`s7tests/numerical_harness.hpp`, `linear_scalar_smoke.cpp`), and the headless `.s7diagram` + probe-sidecar runner itself (section 2, `s7tests/diagram_harness.hpp`), validated by re-expressing that same Stage 1.1 case as a diagram (`s7tests/diagrams/euler_scalar_decay.s7diagram`, `diagram_harness_smoke.cpp`) and confirming it reproduces the identical numbers. The visualization pipeline (section 3) and every stage from 1.2 onward are not built yet.

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
- **It sharpens why `TrapezoidIntegrator`'s `Feedthrough::Direct` is worth investigating (Stage 5), not just a quirk.** In a real analog patch, the integrator is unconditionally the sole memory element of any loop it sits in — there is nothing else in the patch that could hold state across an instant. `EulerIntegrator` (`Feedthrough::None`) preserves exactly that role: it alone can close a cycle. `TrapezoidIntegrator`, despite being "the" integrator conceptually, cannot — AGENTS.md's causal-memory rule means a bare integrator/gain/summer loop through it is rejected, and closing it needs an extra `Delay` that has no counterpart in the analog original. That mismatch between the analog metaphor (integrator = the loop's memory) and the discrete element's actual graph role (direct feedthrough, needs an externally-added memory element) is precisely the kind of "added internal dynamics from how a method is wired as a causal graph element" the test plan's Stage 5 hypothesis (section 9 of `doc/S7_Numerical_Modeling_Test_Plan.md`) asks us to characterize. Any `TrapezoidIntegrator` case in this plan's diagrams should make the added `Delay` visible on the diagram, not hide it, so the difference from the analog original is legible to a human reading the diagram.

### 1.1 Observation points are probes, not elements

The project's own architecture already settles this, and current code (Studio's `Scope`) does not follow it — see the new `OPEN_PROBLEMS.md` entry "Observation architecture: Studio's `Scope` contradicts ADR-011/012". For this plan, a probe is:

- a read-only tap on an existing element's output port (`ObservationService`'s existing `ProbePoint{element_id, port}` concept), not a graph element;
- visible on a diagram as its own thing (a tap symbol with a thin line to the wire it reads), distinct from functional elements — a Studio rendering concern, not solved by this plan (see section 5);
- zero effect on validation, type inference, compilation, causal-cycle requirements, or execution order — attaching or removing a probe never changes what the functional graph computes;
- attachable and movable to a different wire while a diagram is running, without recompiling the graph (consistent with `S7_Observation_Architecture.md`);
- read as discrete per-step values — the existing `ObservationSample`/`ObservationSnapshot` model (step, optional `model_time_ns`, typed value) is sufficient; there is no analog/continuous sampling to add;
- never multiplexed onto the inter-node data stream if the probed wire is ever on a remote node — remote observation is explicitly out of scope for this plan (see `OPEN_PROBLEMS.md` P3) and must get its own contract later, not reuse `NetSource`/`NetSink`'s stream protocol.

### 1.2 Probe list format: a sidecar file, not embedded in `.s7diagram`

Checked directly against `s7studio/src/diagram_document.cpp`: `DiagramDocument::toJson()` rebuilds the top-level JSON object from only the fields it knows (`id`, `dt`, `document_kind`, `diagram_version`, `sheet`, `schema`, `diagram`, `runtime_nodes`, `streams`). A new top-level `"probes"` key would be silently dropped the next time Studio opens and re-saves a diagram. So, until Studio is taught about probes (section 5), each test diagram's probe list lives in a separate file: `<case>.s7diagram` next to `<case>.probes.json`.

That sidecar has two top-level arrays:

- `"probes"`: `{"name": ..., "element": ..., "port": ..., "unit": ...}` objects. `name` is for labeling and need not match the element id; `unit` (optional, e.g. `"rad"`, `"m/s"`) is for plot axis labels only, nothing else reads it.
- `"views"`: which plots section 3's driver should generate for this case, referencing probes by `name` — e.g. `{"type": "phase_portrait", "x": "position", "y": "velocity"}`, `{"type": "attractor3d", "x": "x", "y": "y", "z": "z"}`. Explicit per case, not auto-generated from every probe combination — matching the project's general preference for explicit over inferred behavior (explicit network boundaries, explicit type conversion; "prefer explicit errors over silent coercion" in `AGENTS.md`'s guardrails).

### 1.3 dt/rate is governed by `S7_Sample_Rate_Model.md`, not hand propagation — a blocking prerequisite

Superseded from this plan's earlier draft, which only had the headless runner replicate Studio's ad hoc `dt`-propagation convention (`MainWindow::updateSchemaDiscretizationParams`, Qt-only). That was judged not good enough: nothing proved two integrators in one schema actually shared a rate, only hand inspection did. The real design is `../../doc/S7_Sample_Rate_Model.md` (not yet implemented) — a `RateResolver` mirroring `s7core`'s existing `TypeResolver`, giving every port a validated rate (a rational `{numerator, denominator}`, reusing the wire protocol's existing representation) and making a rate conflict a validation error naming the element, the same way a `SignalType` conflict already is.

This is now treated as a **hard prerequisite for every stage in this plan, not just Stage 2 onward the way the `Trig` element is** (`OPEN_PROBLEMS.md`, "Rate/`dt` has no formal model"; `DEVELOPMENT_PLAN.md` Phase 1, now this plan's own phase and the project's top development priority). The headless diagram runner (section 2) should run the schema through the resolver (which also absorbs the "propagate `.s7diagram`'s top-level `dt` into every element" job, as the resolver's hardware/schema-level seed, section 3 of that design) rather than reimplementing propagation by hand. Stage 1.1 (`linear_scalar_smoke.cpp`), already passing, was built before this design existed and should be revisited once the resolver does, not assumed correct by inspection.

**"One dt for the whole diagram" stays this plan's scope, not the general rule.** `dt`/rate is fundamentally a per-element property in the full design: a hardware I/O element has a rate fixed by the device, not a free choice, and in general a diagram may need an explicit rate-changing element wherever two connected elements' rates genuinely differ. "One rate everywhere" is what the resolver will *confirm* (not just assume) for every stage in this plan, since none of them has a hardware element or a rate-changing element — the harder multi-rate case is explicitly out of this plan's scope and belongs to `DEVELOPMENT_PLAN.md` Phase 5 item 7 instead.

### 1.4 Observation-side decimation, not a new functional element, for plotting at a lower rate

`S7_Observation_Architecture.md` places decimation/FFT-style processing in the Observation Graph, not the Functional Graph. So when a test wants to plot fewer points than it simulates (e.g. Lorenz run for 10^5 steps, 2000 plotted), that is a stride in the harness's sampling loop, not a new element. A genuine rate-conversion *element* is a different, legitimate need (two parts of one model computing at different native rates, feeding each other as part of the dynamics — `NetSource`/`NetSink`'s explicitness is the right template for it) but nothing in Stages 1–4 requires it; tracked as a Phase 5 item in `DEVELOPMENT_PLAN.md`, not part of this plan.

### 1.5 Derived quantities are computed by ordinary Functional Graph elements, not Observation Processing

Energy, residuals, modal measures, and similar derived quantities are *not* an Observation-layer concern here, despite `S7_Observation_Architecture.md` placing decimation/FFT-style "Observation Processing" in the Observation Graph. The difference: decimation/FFT operate on already-captured observation data for a client's own display purposes, whereas something like the pendulum's energy, `E = ½v² + ω₀²(1 − cos x)`, is an ordinary function of the model's own state — the same kind of computation any engineer might wire into a real model (to drive a controller, trigger an event, or just monitor a derived signal), not something specific to this test suite. The right way to get it is to build it in the Functional Graph from ordinary elements (`Multiply` for `v²`, a trig element for `cos x`, `Gain`/`Adder` for the rest) and then probe its output exactly like any other signal — no new Observation-layer mechanism needed.

This is a stronger and simpler answer than this plan's first draft proposed (computing derived columns ad hoc in the C++ harness): it generalizes to real models, not just test diagnostics, and it means a probe is always just `{element, port}` (section 1.2) with nothing special about a "derived" probe.

**It also surfaced a real prerequisite, not just a design preference.** Checking what each later stage's dynamics need against `s7core/include/s7/elements.hpp`'s current element set: Van der Pol's `(1−x²)` term and Lorenz's `xy`/`xz` terms need only `Multiply`, already available — but the pendulum's own dynamics, `v̇ = −ω₀²·sin(x)`, cannot be expressed as an S7 graph at all today, because no element applies a nonlinear scalar function (sin, cos, ...) to an arbitrary wired input (`AnalogOscillator` computes `sin` only on its own internal phase accumulator, not on a general input). This is recorded as its own entry in `OPEN_PROBLEMS.md` ("No generic trigonometric/math element blocks nonlinear modeling"), since it is a real `s7core` gap useful well beyond this test plan, not something to special-case here. It is **not implemented in this session** (documents only); Stage 2 is blocked on it — see section 6.

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

Time-series and phase-portrait/attractor views are deliberately separate templates, not modes of one: `S7_Terminology.md` already lists `Scope` and `Spectrum Analyzer` as distinct Observation Client types (alongside `Studio` and `Logger`), and, for exactly this kind of split, Simulink's Sinks library has `Scope` (time-domain) and `XY Graph` (phase-plane) as two separate blocks rather than two modes of one block. This plan's gnuplot templates follow that precedent; if Studio's `Scope` is ever migrated to a real Observation Client (section 5), it should stay a time-series-only client, with phase-portrait/attractor viewing as its own, separately named client type, not a mode added to Scope.

## 4. Layout

- `s7tests/diagrams/<case>.s7diagram`, `s7tests/diagrams/<case>.probes.json` — one pair per test case, Studio-openable. `euler_scalar_decay` exists (the runner's own validation case, section 2).
- `s7tests/diagram_harness.hpp` — the loader/runner from section 2. Done.
- `s7tests/visualize/*.gp` — gnuplot templates from section 3, plus one small driver script. Not done.
- `s7tests/<stage>_<case>_smoke.cpp` — one `ctest`-registered executable per case, following the existing `s7tests_linear_scalar_smoke` naming and registration pattern in `s7tests/CMakeLists.txt`. `diagram_harness_smoke.cpp` exists (the runner's own validation, not yet a numbered stage).

## 5. Explicitly deferred, not part of this plan

- Migrating Studio's `Scope` off `ScopeElement` onto a real Observation Client / fixing the ADR-011/012 violation, and drawing probes as a distinct visual symbol in the diagram editor. Tracked in `OPEN_PROBLEMS.md`. This plan's diagrams work today without that fix (sidecar probe files, headless-only runner); doing the Studio-side fix later would let Studio open the same `.s7diagram` + `.probes.json` pair and show live probe readings natively, and would let a human place/move probes in the editor instead of hand-editing the sidecar JSON.
- How a genuinely multi-rate graph would *execute* (as opposed to the rate resolver's inference/validation, which is a blocking prerequisite below, not deferred) — a Functional Graph resample element, and the scheduling question behind it, for models that actually need more than one rate domain (`../../doc/S7_Sample_Rate_Model.md` section 6). Tracked as a `DEVELOPMENT_PLAN.md` Phase 5 item.
- Remote observation (section 1.1's last point). Already tracked in `OPEN_PROBLEMS.md` P3.

## 6. Execution order

Follows `doc/S7_Numerical_Modeling_Test_Plan.md` section 12, with the diagram+probes+visualization track layered on from Stage 1.2 onward (Stage 1.1 needs no diagram or phase portrait — one scalar trajectory against a closed form needs no visual story):

1. **Blocking prerequisite for every stage below, including re-confirming Stage 1.1 — done.** `../../doc/S7_Sample_Rate_Model.md`'s `RateResolver` is implemented and mandatory in `Validator::validate()`/`Compiler::compile()` (`OPEN_PROBLEMS.md`, "Rate/`dt` has no formal model").
2. Stage 0 harness — done (`numerical_harness.hpp`).
3. **Stage 1.1 scalar linear — re-confirmed against the resolver (2026-10-15), not just re-passing by inspection.** `linear_scalar_smoke.cpp` (`EulerIntegrator` vs. analytical decay, convergence order, forced-response steady state) now has a `test_rate_resolver_confirms_one_rate()` check that calls `RateResolver::resolve()` directly and asserts it resolves exactly 1/`dt` Hz for every element in the integrator loop — the schema needed no `Schema::set_rate()` seed, since its one `EulerIntegrator` already pins its one connected component.
4. **Headless diagram runner (section 2) — done (2026-10-15).** Runs every schema through the resolver from step 1 (not hand-propagating `dt`); validated against a re-expression of 1.1 as a diagram (`diagrams/euler_scalar_decay.s7diagram`, `diagram_harness_smoke.cpp`) reproducing its result bit-for-bit.
5. Stage 1.2 linear second-order oscillator, as a diagram with position/velocity probes — first phase-portrait case. Build next.
6. Stage 1.3 coupled linear systems of increasing size, against the matrix exponential.
7. Stage 1.6 larger-graph/performance checks (kept numeric-only, no visualization story).
8. **Blocking prerequisite for Stage 2 onward — done.** A generic `Trig` element exists in `s7core` (section 1.5, `OPEN_PROBLEMS.md`), real floating types, `sin`/`cos`/`tan`/`asin`/`acos`/`atan`/`sinh`/`cosh`/`tanh`. Van der Pol and Lorenz need nothing beyond the existing `Multiply`; the pendulum's own dynamics needed this.
9. Stage 2 pendulum — dynamics and energy both built from ordinary elements (section 1.5); energy-drift plot, phase portrait, multiple amplitudes.
10. Stage 3 Van der Pol — limit-cycle phase portrait.
11. Stage 4 Lorenz — 3D attractor projections, statistical/geometric checks.
12. Stage 5 — reuse every case above across integrators/dt values to investigate integrator dynamics, per the test plan's hypothesis in its section 9.
