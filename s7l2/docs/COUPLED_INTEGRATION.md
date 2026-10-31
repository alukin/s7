# Coupled numerical integration in S7

The production runtime supports two opt-in F64 methods for connected ideal
integrator states: simultaneous forward-Euler substeps and two-stage,
fourth-order implicit Gauss–Legendre. The ordinary `EulerIntegrator` with
`substeps=1`, implicit/sampled `TrapezoidIntegrator`, and
`SymplecticIntegrator` keep their existing behavior. This implementation
follows the [reviewed design](COUPLED_INTEGRATION_0.2.0_DESIGN.md) and the
mathematical references from research branch
`research/numerical-feedback-0.2.0` at
`ec3eb67f42e122d7bad407f5bfb1732d6083f14d`.

## Select a method

An ordinary Euler element keeps its current JSON shape. To request coupled
substeps, set `"substeps": 2`, `4`, `8`, `16`, or `32` in every participating
F64 `EulerIntegrator`'s parameters; any integer from 1 through 32 is valid.
All states in one coupled region must have the same outer `dt` and substep
count. `substeps=1` is the default and requires no new configuration. In JSON
schemas and Studio diagrams, the resolved execution-domain rate determines
the outer step. A stored `dt` is a legacy display field and is ignored by
the registry-created integrator, including the Gauss element.

For Gauss–Legendre, replace each participating F64 `EulerIntegrator` with
`GaussLegendreIntegrator` and retain its `initial_value`. For example:

```json
{"id":"x","type":"GaussLegendreIntegrator",
 "params":{"initial_value":1.0}}
```

In C++, construct `GaussLegendreIntegrator(dt, initial)` or
`EulerIntegrator(dt, initial, substeps)` in an ordinary `Schema` to declare
an explicit rate, or pass `std::nullopt` for a rate-following element. `Compiler`
then identifies the region and `NodeRuntime::step()` executes it. The public
`NodeRuntime::numerical_diagnostics()` reports accepted numerical-domain samples
(counted once per domain even when several regions are solved),
numerical failures, base/trial derivative-vector evaluations, pure-element
evaluations, stage residuals, Jacobian probes, line-search trials, Newton
iterations, the last normalized residual, and a numerical scratch **payload
estimate**. Ordinary Euler and trapezoid have no fabricated method counters
in that result. The estimate excludes allocator metadata/capacity and the
compiled graph; it is not peak process memory.

The editable [oscillator](../examples/2int_osc_gauss.s7diagram),
[RC charging](../examples/linear/rc_charge_gauss.s7diagram),
[RC discharging](../examples/linear/rc_discharge_gauss.s7diagram), and
[RLC step](../examples/linear/rlc_step_gauss.s7diagram) diagrams use this
actual production element. Existing Euler diagrams can be edited to set
`substeps`; no separate research solver or hidden delay is required. The
normal diagram discretization update sets the schema/domain rate and leaves
legacy element `dt` fields untouched; execution follows the selected rate.

## Causal and numerical contract

At outer interval `[nh,(n+1)h)`, `x_n` is the published integrator state and
the other causal outputs are sampled once. The first runtime call publishes
`x_0` and stages `x_1`; the next call publishes `x_1` before evaluating the
causal graph. Neither Euler substeps nor Gauss stages are observable through
ordinary output reads or probes. Every coupled state receives the same trial
vector before its derivative cone is replayed, independent of integrator
insertion order.

For Euler with `m` internal steps, `δ=h/m` and every component advances
simultaneously via `z_(j+1)=z_j+δ f(z_j,u_n)`. Under the same held input,
this is mathematically identical to ordinary Euler at `δ` observed every
`m` steps. Substepping separates internal resolution from the outer
observation/control rate; it does not create a higher-order integrator.
The trial derivative graph is reevaluated once per internal step, including
the initial outer evaluation.

Gauss–Legendre solves the coupled `2n` stage residual for
`K_i=f(x_n+h Σ_j a_ij K_j,u_n)`, using `a11=a22=1/4`,
`a12=1/4−√3/6`, `a21=1/4+√3/6`, and commits
`x_(n+1)=x_n+h(K_1+K_2)/2`. For linear `x'=Ax+Bu` and held `u`, put
`Q=hA` and `P=I−Q/2+Q²/12`; the transition is
`P⁻¹(I+Q/2+Q²/12)` and the input operator is `hP⁻¹B`.
The method is A-stable and conserves quadratic invariants for the tested
lossless linear systems to roundoff. It is **not L-stable**: a very large
stable step can be a poor transient approximation. A small nonlinear-solver
residual establishes solution of the discrete stage equation, not accuracy
of the physical model. Refine `dt` to assess discretization error.

The source is held across every substep or stage. If a real input varies
within the outer interval, that hold changes the forcing problem; the full
fourth-order claim applies only to smooth autonomous or correctly sampled
constant-forcing cases in the supported envelope. No implicit resampling of
external I/O is performed.

## Eligible regions and failures

The compiler forms a connected numerical region within one rate domain from selected F64 causal
memory states and derivative dependencies through built-in F64 `Gain`,
`Adder`, and `Multiply`. It retains the normal compiled causal order. A
one-way dependency between states with the same method and configuration
belongs to the same region; disconnected systems are solved separately.
Regions with different methods or configurations can supply one-way held
inputs to each other. A Resampler can supply a held input from another rate
domain; its input domain steps first under the normal domain scheduler.
Other memory outputs can also be held inputs, but a
feedback cycle crossing policies or a normal Euler/trapezoid state is a
validation error. Stateful direct feedthrough, nested execution, unknown
plugin operations, I/O, and unsupported pure elements **inside the varying
trial cone** are rejected. They are never repeatedly stepped. Ordinary
causal-cycle validation remains in force.

Gauss–Legendre currently supports at most 16 F64 state components in one
connected region. It uses damped Newton with a finite-difference Jacobian,
scaled partial pivoting, a 10-trial backtracking line search, at most 12
Newton iterations, and normalized infinity residual tolerance
`atol=rtol=1e−13`. Work arrays are allocated with the region and reused
across steps. Nonfinite values, a singular/ill-conditioned Jacobian, a
nonreducing line search, or nonconvergence raise a runtime error naming the
failure. The runtime restores published state and trial pure outputs, stages
no rejected endpoint, and preserves the pending accepted state for retry.
The general whole-step rollback guarantee remains limited to published
outputs: unrelated stateful direct-feedthrough private state cannot be
undone. The new methods are intended for small deterministic graphs, not
arbitrary large or stiff nonlinear systems.

The existing coupled trapezoid still uses its 64-iteration fixed-point
solver, its F16–F128/C16–C128 type support, and its original numerical
equation. Compiler cone discovery and runtime trial-output capture/replay are
shared, but no trapezoid tolerance or publication rule changed.

## Verification and Release cost

`s7core_coupled_integration_smoke` uses actual compiled graphs and independent
closed forms/Padé matrices for oscillator, RC and RLC. It checks Euler
fine-step equivalence for `m=1,2,4,8,16,32`, fourth-order refinement,
quadratic energy, phase, ordering, region and rate-domain boundaries, diagram/registry
configuration, mixed-method validation, nonfinite/nonconvergent solves, and
rollback/retry. Existing trapezoid and linear-example tests remain gates.

The opt-in `s7core_coupled_integration_bench` measures a median of five
Release C++ step loops. Graph construction, accuracy calculation and output
formatting are outside the timed loop. Timing uses an alternating external
F64 source evaluated **once per outer step** so the dissipative systems do
not settle to a trivial zero residual; accuracy uses a constant input and
independent exact references. Thus the timing and error columns describe
different input traces, and short timings are directional rather than a
throughput guarantee. Results on the Linux/GCC 16.2.1 host at code revision
`cde4b6b`, after the rate-domain rebase:

| Model, outer `h` | Method | Maximum state error, constant input | Median wall time/step, alternating input | Derivative vectors/step, alternating input |
|---|---|---:|---:|---:|
| RC charge, `.5 s` | Euler | .117879 | .097 µs | uninstrumented |
| RC charge, `.5 s` | Euler `m=32` | .002893 | .458 µs | 32 |
| RC charge, `.5 s` | Trapezoid | .007879 | .301 µs | uninstrumented |
| RC charge, `.5 s` | Gauss–Legendre | 3.24105e−5 | .368 µs | 15 |
| RLC `R=1`, `.2 s` | Euler | .279358 | .051 µs | uninstrumented |
| RLC `R=1`, `.2 s` | Euler `m=32` | .004788 | .540 µs | 32 |
| RLC `R=1`, `.2 s` | Trapezoid | .009451 | .336 µs | uninstrumented |
| RLC `R=1`, `.2 s` | Gauss–Legendre | 2.58046e−5 | .765 µs | 23 |

These correspond to about `.74 µs` of RC GL execution per simulated second
and `3.83 µs` for RLC GL at the stated outer timesteps. The recorded GL
work averaged two Newton iterations per outer step for the alternating
source. The numerical scratch payload estimate was 320 bytes for RC and
656 bytes for RLC; this is not peak heap use.

Connected-region splitting was measured before and after on the same
Release benchmark. Sixteen **independent** RC states, previously grouped
into one dense `32×32` Newton system, fell from `25.5 µs` to `5.63 µs`
per outer step; estimated numerical scratch payload fell from 12,800 to
5,120 bytes. A genuinely coupled `n`-state region still needs `2n`
unknowns, a dense `2n×2n` Jacobian, roughly `2n` residual probes per
Newton iteration, and cubic dense factorization work. Sparse/analytic
Jacobians or Newton–Krylov methods are possible future projects, outside
this implementation.

Reproduce the focused checks and benchmark from `s7l2`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DS7_BUILD_STUDIO=OFF
cmake --build build --target s7core_coupled_integration_smoke s7core_coupled_integration_bench --parallel
ctest --test-dir build -R '^s7core_coupled_integration_smoke$' --output-on-failure
./build/s7core/s7core_coupled_integration_bench > build/coupled_integration_bench.csv
```

The CSV belongs in the ignored build directory, not in Git. The bundled
diagrams are validated and executed by `s7tests_linear_examples_smoke`.

On the feature branch, the Release headless `build-tests` target built and
`ctest --test-dir build --output-on-failure -j4` passed **51/51** tests after
the rate-domain rebase. The
network tests require local socket access; their first sandboxed run failed
with `open: Operation not permitted`, and all passed when rerun with loopback
access. A separate Release Qt build passed the `s7studio_diagram`,
`s7studio_edit_check`, and `s7studio_bundled_demo` CTests (**3/3**).
`s7studio --edit-check` also loaded and round-tripped each of the four new
Gauss diagrams under `QT_QPA_PLATFORM=offscreen`. These automated checks do
not replace a manual Studio run-and-observe usability review.
