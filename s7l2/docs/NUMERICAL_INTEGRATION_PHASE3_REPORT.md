# Numerical integration research: Phase 3 results

Branch: `research/numerical-feedback-0.2.0`. This report continues the
[Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md) and
[Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md) investigations. The
[Phase 3 specification](NUMERICAL_INTEGRATION_PHASE3_SPEC.md) defines the
mathematics and execution contract; the
[compact summary](NUMERICAL_INTEGRATION_PHASE3_SUMMARY.json) records numerical
values and exact producing source revision. These are experiments on ideal
integrator networks, with no physical amplifier model.

## What ran

The C++ research runner validates and compiles actual S7 schemas containing
one or two `EulerIntegrator` elements and ordinary S7 arithmetic elements.
Its explicit research policy reads and temporarily substitutes their output
states, reevaluates the compiled pure derivative cone, and restores the
published outputs. It applies either simultaneous Euler substeps or a full
coupled two-stage Gauss–Legendre (GL) solve. It never calls the ordinary
Euler integrators' `step()` during a trial; the integrator elements provide
the actual graph state and ports. The comparator runs production
`NodeRuntime` with coupled implicit trapezoid, or its Euler–Cromer element
for the oscillator. The ordinary fine-Euler equivalence regression also
runs `NodeRuntime` at `h/m`.

The three primary systems were the oscillator (`ω₀=1`, 100 s), RC charging
and discharging (`RC=1`, about 20 s), and RLC (`L=1`, `C=.25`, `R=0,1,4,6`,
20 s). Constant sources were held over every interval. The 246 measured
cases cover outer steps `h=.05,.1,.2` for the oscillator,
`h=.1,.5,2,3` for RC, and `h=.05,.1,.2,.5` for RLC. Each includes Euler
`m=1,2,4,8,16,32`, GL, Euler with approximately the same number of
derivative-vector evaluations as GL, and trapezoid; the oscillator also
includes Euler–Cromer. All numerical states and references use IEEE-754
binary64. `max trajectory error` below is the maximum Euclidean norm of
the state error at outer sample times. Energy drift is the maximum absolute
relative error from initial energy, so it is used only for lossless models.

For the research policy, the first `step()` publishes `x₀` and stages `x₁`.
Later calls publish the pending accepted state; intermediate substeps and GL
stages are never published. The production runtime requires an additional
call to publish its final pending state. This timing is accounted for in
the comparisons and recorded as `timed_steps` in raw results. A failed GL
solve restores the prior published state and pure outputs and leaves the
pending state unchanged. The research trial cone rejects all stateful or
unrecognized direct-feedthrough elements, plugins, I/O and mixed integrator
types. These restrictions are part of the result, not general runtime
support.

## Mathematical interpretation

For a linear system `x'=Ax+Bu` with a constant held input, Euler substeps of
size `δ=h/m` give `M=(I+δA)^m` and
`G=δ Σ(j=0..m−1)(I+δA)^j B`. Thus the result is precisely ordinary
Euler at the smaller step observed every `m` updates. The S7 graph
regression confirms this for the oscillator, RC and RLC through `m=32`.
For any oscillator frequency `ω`, its Euler pole magnitude per outer step is
`(1+(hω/m)²)^(m/2)>1`; substepping reduces but never removes energy
growth at fixed finite `m`. Its numerical angular frequency is
`(m/h) atan(hω/m)`, which approaches `ω` as `m` increases. For RC, Euler
substepping is stable when `0<h/(m RC)<2`, with pole
`(1−h/(m RC))^m`.

GL has the two coupled stage equations and coefficients in the specification.
For linear `A`, let `Q=hA`, `P=I−Q/2+Q²/12`; then
`M=P⁻¹(I+Q/2+Q²/12)` and `G=hP⁻¹B`. The C++ tests compare the **graph
execution** against independently constructed matrices and constant-input
closed forms, including critically damped RLC. The two stages are temporary
solver variables, so GL adds no persistent modes. Its scalar stability
function has unit modulus on the imaginary axis and is A-stable. Its limit
on the negative real axis is one, so it is **not L-stable**. For the
oscillator, the pole phase per step is
`2 atan[(hω/2)/(1−(hω)²/12)]` on the continuous branch near zero; this
has a fifth-order per-step phase defect, versus the third-order defect of
trapezoid. At large `h`, pole phase must be unwrapped rather than inferred
from one principal argument. The numerical test verifies fourth-order
global refinement for smooth autonomous motion.

## Measured dynamics

The following values come from the compact summary. Errors use the state
units of each model; different models' error magnitudes are not directly
interchangeable.

| System and outer step | Euler `m=32` maximum error | Trapezoid maximum error | GL maximum error | GL derivative vectors per outer step |
|---|---:|---:|---:|---:|
| Oscillator, `h=.1`, 100 s | .169118 | .083185 | 1.3881e−5 | 23 |
| RC charge, `h=.5`, 20 s | .002893 | .007879 | 3.2410e−5 | 11.55 |
| RLC lossless, `h=.2`, 20 s | .138476 | .244643 | 6.5961e−4 | 23 |
| RLC `R=1`, `h=.2`, 20 s | .004812 | .009627 | 2.5990e−5 | 21.8 |
| RLC `R=4`, `h=.2`, 20 s | .001379 | .005658 | 2.9131e−5 | 13.7 |
| RLC `R=6`, `h=.2`, 20 s | .001303 | .008635 | 1.4892e−4 | 17 |

At `h=.1`, the oscillator's GL energy drift was `1.33e−15` over 100 s and
its final phase error was `−1.3881e−5` rad. Trapezoid also held energy to
roundoff (`1.11e−15`), but accumulated `−.083209` rad of phase error.
Euler `m=32` raised energy by 36.68%; ordinary Euler `m=1` raised it by
more than 20,000 times its initial value. Euler–Cromer stayed bounded but
had .0526 maximum relative energy drift and .0913 maximum trajectory error.
For lossless RLC at `h=.2`, GL's maximum energy drift was `8.88e−16`;
Euler `m=32` reached .6487. The GL oscillator and lossless RLC poles lay
on the unit circle to numerical precision, as the rational transition
predicts.

For RC charge at `h=.5`, the exact pole is `.60653066`, GL gives
`.60655738`, trapezoid `.6`, and ordinary Euler `.5`. At `h=3`, ordinary
Euler's pole is `−2` and its discharge maximum state error was `128`; two
Euler substeps stabilize the recurrence but remain inaccurate. GL gives
pole `.0769231` against exact `.0497871`, with maximum discharge error
`.027136`. Euler `m=8` gives `.026504` and `m=32` gives `.006938` there:
**stable GL is not necessarily the most accurate large-step transient**.
The GL pole for `h=30` is still inside the unit disk but approaches one
in the stiff limit, demonstrating the lack of L-stability. Production
trapezoid could not complete RC at `h=2` or `3` because its fixed-point
solver was not contractive; this is solver failure, not a claim that the
trapezoid formula lacks an implicit solution.

For underdamped RLC (`R=1`, `h=.2`), the exact pole pair is approximately
`.83781867 ± .34174645i`, magnitude `.90483742`, frequency
`1.93649167 rad/s`. GL gives `.83783139 ± .34174741i`, magnitude
`.90484956`, frequency `1.93647002 rad/s`. Trapezoid's frequency is
`1.91732365 rad/s`. Critical `R=4` and overdamped `R=6` retain the
expected real-pole structure in the conditioned analytical comparison.
Production trapezoid's fixed-point iteration also failed for `R=6,h=.5`;
GL completed that case. The generated step-response and pole figures show
the full sampled curves and timestep dependence.

## How many substeps, and at what cost?

For the representative steps above, the first tested `m` whose **maximum
trajectory error** was no greater than trapezoid's was: RC charge `16`,
lossless RLC `32`, RLC `R=1` `32`, `R=4` `16`, `R=6` `8`. None of
`m≤32` matched trapezoid for the 100 s oscillator at `h=.1`. None matched
GL in those cases. The exception is RC discharge at the deliberately coarse
`h=3`, where `m=8` already beat GL's transient error. There is no single
substep count that matches another method on every system or step size.

At approximately equal derivative-vector work: oscillator `h=.1` used
23,000 vectors for both GL and Euler `m=23`, with maximum errors
`1.3881e−5` and `.24283`; RC charge `h=.5` used 462 GL versus 480 Euler
`m=12` vectors, with errors `3.2410e−5` and `.007800`; RLC `R=1,h=.2`
used 2,180 GL versus 2,200 Euler `m=22` vectors, with errors
`2.5990e−5` and `.007045`. The coarse RC discharge `h=3` reversed this
ordering: 81 GL vectors gave `.027136`, while 84 Euler `m=12` vectors
gave `.018111`. Pure-element evaluations, base/trial split, stage residuals,
Jacobian probes, line-search trials, solver iterations and accepted/failed
steps are recorded for each research-policy case. Production comparator
derivative counts are `null` because the production runtime does not
instrument those operations; they must not be read as zero.

All timing comparisons are **within Release C++ S7 graph execution**. The
five-run median for the oscillator at `h=.1` was about `15.3 µs` GL,
`7.1 µs` Euler `m=32`, and `1.68 µs` production trapezoid per simulated
second. RLC `R=1,h=.2` was about `8.05 µs` GL, `3.69 µs` Euler `m=32`,
and `1.40 µs` trapezoid per simulated second. The GL solver's finite
difference Jacobian, repeated graph evaluations, vector allocations and
line search make a derivative-vector count an incomplete CPU-cost measure.
These are tiny graphs and short wall durations, so timings are directional,
not a throughput guarantee. Setup, CSV and reference calculations were
excluded; the production benchmark includes its extra final publication
step. The reported GL scratch payload estimates are 208 bytes for RC and
456–464 bytes for the two-state systems; they exclude vector capacity,
allocator metadata and the compiled graph, and are **not peak memory**.

## Architectural recommendation and limits

Euler substepping has no new mathematical advantage over increasing global
Euler frequency when all coupled states and held inputs advance identically.
Its practical role is to keep the external observation/control rate lower
than the internal integration rate. That separation needs an explicit
input-sampling and output-publication contract, particularly if external
sources change during a physical step.

GL reproduced the ideal oscillator's invariant and phase behavior and gave
small errors on RC and all RLC damping regimes in the tested envelope. It
can be implemented as a **coupled graph policy** without adding persistent
states or changing causal graph meaning, provided trial evaluation is
restricted and reversible. The current research policy demonstrates this
for the stated F64 arithmetic graphs. A general first-class production
policy is **not yet justified as an API change**: the compiler currently
exposes a safe trial cone specifically for coupled trapezoid, while the
runtime owns staging, rate and failure semantics. Production adoption would
need a design decision on graph-wide policy selection, supported elements,
external input holds, trial ownership and rollback. A per-integrator GL
block using stale peers would be a different, incorrect method. The
experiments therefore recommend exploring graph-level integration policies
through an explicit architectural proposal, with ordinary `NodeRuntime`
unchanged until that contract is agreed.

The GL solver is damped finite-difference Newton with at most 12
iterations, normalized residual tolerance `atol=rtol=1e−13`, and explicit
nonfinite, singular and nonconvergence failures. The included failure and
retry tests cover safe rollback for eligible graphs; they do not qualify
arbitrary nonlinear, stiff, discontinuous or side-effecting networks.
No varying external forcing was tested. Fourth-order claims apply to the
smooth autonomous/constant-forcing systems here; a zero-order hold of a
genuinely varying input changes the forcing problem. The RLC initial
comparison covers charge/current states, not every possible observed
transfer function. Numerical precision and finite-difference Newton
conditioning also limit extrapolation to other state scales or types.

## Reproduce and inspect

From `s7l2`, with the documented C++26 toolchain:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DS7_BUILD_STUDIO=OFF
cmake --build build-release --target s7tests_numerical_integration_research_smoke s7tests_numerical_integration_research --parallel
ctest --test-dir build-release -R '^s7tests_numerical_integration_research_smoke$' --output-on-failure
./build-release/s7tests/s7tests_numerical_integration_research build-release/s7tests/phase3
MPLCONFIGDIR=/tmp/s7-mpl python3 -B tools/plot_numerical_integration_phase3.py --input build-release/s7tests/phase3 --compact docs/NUMERICAL_INTEGRATION_PHASE3_SUMMARY.json
```

The full runner writes `results.json` and per-run CSV under the ignored
`build-release/s7tests/phase3/` directory. The postprocessor writes 14
figures there, including oscillator trajectory/energy/phase, RC response
and timestep error, RLC step/energy, complex poles and trajectories, and
error versus derivative work and measured execution cost. The compact JSON
records the branch, producing Git SHA, source and binary SHA-256, UTC
timestamp, compiler, Python/NumPy/Matplotlib/platform versions,
configuration, solver tolerances, failures and reproduction command.
It is the checked-in numerical evidence; the full datasets and plots are
generated on demand.

The final relevant Release regression run passed 8/8 CTest cases:
`s7core_smoke`, `s7core_indexed_execution_smoke`,
`s7core_trapezoid_smoke`, `s7tests_linear_examples_smoke`,
`s7tests_symplectic_oscillator_smoke`,
`s7tests_numerical_integration_research_smoke`,
`s7tests_linear_structure_research`, and
`s7tests_numerical_feedback_research`. The Phase 3 smoke test includes
independent matrix and closed-form references, Euler/fine-Euler equivalence,
publication, deterministic insertion-order checks, fourth-order refinement,
nonfinite and nonconvergent solver paths, rollback and mixed-element
eligibility rejection. The long 246-case matrix is opt-in and completed;
its five recorded trapezoid failures are expected diagnostic results.
