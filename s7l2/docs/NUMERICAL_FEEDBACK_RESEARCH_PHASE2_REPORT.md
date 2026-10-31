# S7 numerical feedback research: Phase 2 report

Research branch: `research/numerical-feedback-0.2.0`. The analytical and
experimental source revision for the checked-in [compact summary](NUMERICAL_FEEDBACK_PHASE2_SUMMARY.json)
is `af0f6930b9939f3e5fa140274bdffd98f9057766`; its metadata records the
source SHA-256, platform, binary64 precision, Python/NumPy/SciPy versions,
UTC generation time, configuration and tolerances. The full 440-case matrix,
99 responses, and figures are generated into ignored `build/linear-research/`.
The [formal derivation](NUMERICAL_FEEDBACK_RESEARCH_PHASE2.md) fixes the
state/input/output timing convention. This work does not change production
S7 integration, validation, publication or causal-loop semantics.

The Phase 1 full suite was regenerated at `2026-10-31T05:08:32Z` from
`4daaf2e362157110e4e5acd7da9d3549c205ca5e` with runner SHA-256
`6df7eda9e481e4c9f6608683fcd5f5d09b855362dfb74aec0b319ef8e5244271`.
It completed **284/284 cases with zero failures** and recorded
`162,471,962` total derivative evaluations including diagnostics. Matching
cases and reported trajectory/energy/phase metrics are numerically identical
to the historical Phase 1 JSON; the new metadata and counters resolve its
provenance and cost ambiguity. The gate rerun completed 30/30 cases with
zero failures. The C++ production baseline was regenerated from a Release
binary built with GCC 16.2.1; its output records the binary hash and five
timing repetitions per case. Test run: `2026-10-31 05:10 UTC`, seven focused
CTest cases passed after `build-tests` completed.

## Methods and meaning of structural preservation

The primary reference is the exact zero-order-held sampled linear system,
`Phi=exp(Ah)` and `Gamma=∫exp(Aτ)B dτ`, with output `y_n=Cx_n+Du_n` before
the update. Structural preservation is assessed by internal pole mapping,
stability type, multiplicity, damping and frequency, input-output poles and
zeros, state dimension, and equivalence under a nonsingular coordinate change.
Energy and long-run phase supply additional oscillator checks. Exact sampled
does not mean exact response to a continuously varying sinusoid: its input is
held between samples.

The laboratory computes both eigenvalues of `M` and reduced SISO transfer
roots. It uses a block exponential for `Gamma`, explicit one/two-state transfer
numerators, a discriminant tolerance for double roots, and cancellation
tolerances scaled to binary64 precision and pole separation. Analytical RC,
LC and RLC formulas and Phase 1's independent pure graph runner check the
matrix calculations. A deliberately weak but nonzero second mode is retained
while a genuinely hidden mode cancels from the transfer.

## Circuit and oscillator measurements

### RC, `tau=1 s`

At `h/tau=0.5`, the exact sampled pole is `0.606531`. Forward Euler gives
`0.5`, solved backward Euler `0.666667`, and trapezoid `0.6`. Their effective
time constants `-h/log|pole|` are respectively `0.721348`, `1.233152`, and
`0.978808 s`; the exact value is `1 s`. One finite correction gives pole
`0.75` and effective time constant `1.73803 s`; two give pole `0.625` and
`1.06382 s`. More corrections approach backward Euler, not necessarily the
physical time constant monotonically. The generated charge and discharge plot
uses the same state and input-hold convention.

At `h/tau=2.5`, forward Euler's pole is `-1.5` and unstable, while solved
backward Euler's is `0.285714` and stable. Finite simultaneous corrections
give poles `-1.5, 4.75, -10.875, -69.46875` for `k=0,1,2,4` and diverge as
`k` grows. The fixed-point iteration radius is `2.5`; stability of the
implicit discretization does not imply convergence of this solver. At
`h/tau=2`, forward Euler has a marginal alternating pole `-1`, while the
backward root is `1/3` and the fixed-point iteration still fails.

### LC, `omega0=1 rad/s`

At `h omega0=0.5`, the exact sampled poles are
`0.877583 ± 0.479426i`, with magnitude one and frequency `1 rad/s`.

| Method | Pole magnitude | Numerical frequency (rad/s) |
| --- | ---: | ---: |
| Forward Euler / finite `k=0` | 1.118034 | 0.927295 |
| Finite `k=1` | 0.901388 | 1.176005 |
| Finite `k=2` | 0.838525 | 0.927295 |
| Solved backward Euler | 0.894427 | 0.927295 |
| Trapezoid | 1.000000 | 0.979915 |
| Euler–Cromer | 1.000000 | 1.010721 |

The finite poles are `p_k(±i h omega0)`, where
`p_k(w)=Σ(j=0..k+1)w^j`. For `k=1`, the exact magnitude squared is
`1-theta²+theta⁴`, `theta=h omega0`. Thus one correction can produce useful
damping for `0<theta<1`, but that damping is artificial for the lossless LC
model, and its phase at `theta=0.5` is worse than forward Euler's. Increasing
`k` converges toward the backward-Euler pole only while `theta<1`. At
`theta=2.5`, even one correction has magnitude `5.814852` while backward
Euler's solved pole has magnitude `0.371391`. Euler–Cromer leaves the unit
circle for `theta>2`; at `theta=2.5` it has a growing and a decaying real
pole. The energy plot separates exponential Euler growth, backward damping,
trapezoid conservation and bounded Euler–Cromer energy oscillation.

### RLC and mechanical analogue

With `L=1 H`, `C=0.25 F`, `R=1,4,6 Ω` are under, critical, and over damped.
The continuous poles follow
`-R/(2L) ± sqrt((R/(2L))²-1/(LC))`; the tested critical pole is double
at `s=-2`. At `h=0.2 s`, the underdamped exact sampled pair is
`0.837819 ± 0.341746i` (magnitude `0.904837`, angular frequency
`1.936492 rad/s`). Forward Euler yields magnitude `0.979796` and frequency
`2.031889`; backward Euler `0.857493` and `1.692675`; trapezoid `0.908054`
and `1.917324`. The unit-step capacitor-voltage overshoot is `0.443899 V`
for exact ZOH, `0.868216 V` for forward Euler, `0.243404 V` for backward
Euler, and `0.452196 V` for trapezoid. Maximum sampled output errors against
the closed-form step response are respectively `0`, `0.547978`, `0.214865`,
and `0.020139 V`. The critically and overdamped plots show no positive
overshoot in the exact sampled cases.

At `ω=2 rad/s`, the continuous capacitor-voltage transfer has magnitude `2`
and phase `-π/2`; exact ZOH sampling at `h=0.2` gives magnitude `1.986712`
and phase `-1.770742 rad`. The phase includes the held-input timing. Forward
Euler's sampled magnitude is `9.728138`, backward Euler's `1.121740`, and
trapezoid's `2.010484`. The frequency-response figure shows their broader
curves and phase shift; the resonant-response figure shows the corresponding
time trajectories. At `R=6`, finite `k=1` has a growing pole `1.049443`
at `h=0.2`, even though the continuous and backward-Euler systems are stable.

With `m=L`, `b=R`, and spring stiffness `1/C`, the mechanical state and input
matrices equal the RLC matrices. Comparing spring force to capacitor voltage
also makes the output matrix equal. Every tested matrix-function method then
produces equal discrete matrices and responses to binary64 tolerance. This is
a positive equivalence result, not evidence that superficial graph topology
changes dynamics.

## Zeros, realization, and timing

The pre-update RLC capacitor-voltage transfer at `h=0.2` has two observable
internal modes. Its exact ZOH zero is about `-0.93519`; forward Euler has no
finite zero, backward Euler has a zero at `0`, and trapezoid has a zero at
`-1`. The continuous transfer has no finite zero. These are different
input-output numerators arising from discrete input injection and measurement
timing. None adds a third autonomous state or pole. A separate diagonal
two-state fixture has two internal eigenvalues but one transfer pole because
its second mode is unforced and unobserved; the cancellation test checks both
the exact hidden case and a weak observable case.

Similarity-transformed RC/RLC state-space systems agree under exact sampling,
forward/backward Euler, trapezoid and finite **simultaneous** corrections,
to numerical tolerance. Two differently ordered/grouped pure derivative
graphs also agree for `k=0,1,2,4,8`. A one-sweep sequential correction does
depend on the chosen coordinates: for the tested RLC transform at `h=0.2`,
the Frobenius norm `||M_transformed - P⁻¹ M_original P||F` is about `0.133` at `k=1` and
`0.000425` at `k=8`; both converge toward the same backward-Euler root when
their iteration matrices are contractive. Sequential update order is a real
operator-splitting choice. Extra explicit delays, mixed integrator assignments,
or different input holds would likewise change the equation; no such change
is inferred from source-level grouping alone.

## Solver error, discretization error, arithmetic error, and cost

For simultaneous corrections, the exact implicit residual is
`-Q^(k+1) h f(x_n,u_n)` where `Q=hA`; the implicit-solver error is this
residual transformed by `(I-Q)⁻¹`. The compact summary reports both, while
trajectory and pole deviations use the exact sampled system. Binary64
arithmetic discrepancies are bounded separately by analytical matrix and
graph checks at roughly `1e-12` or tighter in the tested configurations;
these thresholds do not equate solver convergence with dynamical accuracy.

Phase 1's updated runner counts base, trial, and diagnostic evaluations
separately, along with pure graph-element calls, accepted steps, wall time,
and process CPU time. For its 200,000-step oscillator at `h=0.001`, finite
`k=1,2,4,8` require `600,000`, `800,000`, `1,200,000`, and `2,000,000`
total derivative evaluations including one residual evaluation per step;
Euler requires `200,000`. A tolerance-stopped backward run took about
`1,000,000` total evaluations in that case, including repeated diagnostic
checks. The equal-work matrix uses the same counted diagnostic rule: over
5 s at `h=0.1`, finite `k=1` LC final-state error is `0.22218` at 150
evaluations, versus `0.08688` for forward Euler at `h=1/30` with the same
evaluation count. For RC these errors are `0.002217` versus `0.000551`.
The comparisons are approximate work budgets: Python wall and CPU timings
also contain loop, graph and solver overhead and are recorded with each
Phase 1 run. Direct C++ `NodeRuntime` timings are a separate baseline and
must not be ranked against Python timings as algorithmic performance.

## Answers to the research questions

1. **Does iterative Euler feedback settling preserve structure better?** No
   general improvement is observed under the explicit pole, stability,
   transfer and invariant criteria above. Finite iteration can create an
   intermediate pole map, but convergence reaches dissipative backward Euler.
2. **How does `k` move poles and affect damping, phase and stability?**
   Each continuous eigenvalue `s` maps to `p_k(hs)` for simultaneous finite
   iteration. Magnitude and argument of that polynomial determine damping
   and phase. Its convergence condition `rho(hA)<1` is distinct from the
   stability region of the limiting backward-Euler method.
3. **Do equivalent realizations differ?** Similarity and pure regrouping do
   not alter these matrix-function or simultaneous-polynomial methods beyond
   rounding. Sequential coordinate sweeps do; the measured discrepancy
   vanishes toward a common implicit root when they converge.
4. **Are extra poles or zeros introduced?** The tested methods preserve state
   dimension and introduce no extra independent modes. They do alter SISO
   zeros, and a mode can cancel from a transfer while remaining internal.
   An inserted causal delay would add a persistent state, but is not part of
   any tested integrator.
5. **Which methods fit S7's architecture?** Local Euler and the existing
   partitioned Euler–Cromer block fit explicit causal memory at low cost.
   Coupled trapezoid can improve linear pole and energy behavior but needs
   a bounded pure trial region, residual checking, and a clear stage/commit
   contract. Backward settling adds those costs without a general physical
   advantage and fails for some large-step fixed-point iterations. Numerical
   merit and causal implementability are separate considerations.
6. **Is a semantic change justified?** No. The experiments support further
   model-specific method selection and explicit design work on any future
   coupled solver. They do not justify changing production integrators,
   allowing arbitrary algebraic loops, or altering publication timing.

## Reproduce and limitations

From `s7l2/`, with Python 3, NumPy, SciPy and Matplotlib available:

```bash
cmake -S . -B build-release
cmake --build build-release --target build-tests s7tests_numerical_research_baseline --parallel 6
ctest --test-dir build-release -R 's7tests_(linear_structure_research|numerical_feedback_research|linear_examples_smoke|linear_scalar_smoke|symplectic_oscillator_smoke)$' --output-on-failure
MPLCONFIGDIR=/tmp/s7-matplotlib python3 -B tools/linear_structure_research.py --output build/linear-research --compact-output docs/NUMERICAL_FEEDBACK_PHASE2_SUMMARY.json
python3 -B tools/numerical_feedback_research.py --suite gate --output build/linear-research/phase1_gate.json
python3 -B tools/numerical_feedback_research.py --suite full --output build/linear-research/phase1_full.json
python3 -B tools/collect_numerical_baseline.py --output build/linear-research/phase1_baseline.json
```

The generated pole maps, pole trajectories, feedback-count trajectory,
frequency/damping curves, LC energy, RC charge/discharge, three RLC damping
regimes, RLC step and resonant time responses, RLC frequency response,
solver-residual curves, and error-versus-work plot live in
`build/linear-research/`. Full JSON and PNG files are intentionally ignored;
the checked-in compact summary retains selected audited numerical values.
The older Phase 1 full JSON is retained as historical evidence; its metadata
named the baseline commit and did not unambiguously identify producing source.
Regenerated Phase 1 outputs include source hashes, revision, configuration,
precision and complete counters.

Limits: ideal finite-dimensional LTI systems, held inputs, one/two-state SISO
transfers, F64, and a whitelist of pure graph nodes. Pole-zero cancellation
near defective roots remains limited by conditioning. The matrix laboratory
precomputes transitions and its timing is not a production solver benchmark.
Frequency samples above Nyquist are not interpreted. The RLC diagrams already
in `examples/linear/` remain the editable production examples; the finite
feedback methods remain research-only.
