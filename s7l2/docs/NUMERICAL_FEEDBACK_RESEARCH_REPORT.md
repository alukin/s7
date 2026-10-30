# S7 0.2.0 numerical feedback research report

Research branch: `research/numerical-feedback-0.2.0`; baseline:
`0b75330d7334f36ec79a9b67a547633466dbff0c`. This report evaluates a
separate F64 pure-graph prototype and direct production `NodeRuntime` baselines.
No production integrator, validator, compiler, scheduler or publication behavior
was changed. The [N0 contract](NUMERICAL_FEEDBACK_RESEARCH_N0.md) defines the
equations, safe trial region, sample convention and model matrix.

## Method and reproducibility

The prototype represents each model as scalar integrator outputs feeding an
ordered graph of pure functions. A trial temporarily replaces selected
integrator outputs, reevaluates only the state-dependent derivative cone, and
stages the endpoint once. Fixed trial counts `1,2,4,8` apply simultaneous
backward-Euler fixed-point corrections after a forward-Euler predictor. Zero
trials is intentional forward Euler. The converged candidate takes at most 64
corrections and requires normalized equation residual at most one, with
`atol=rtol=1e-12`. The trapezoid prototype uses the same 64/normalized-residual
stop for its different endpoint equation. **It is not the production
TrapezoidIntegrator tolerance implementation.** Production Trapezoid and
Euler–Cromer were measured through `NodeRuntime` independently.

References are analytical for the harmonic oscillator, scalar linear loop and
P plant, and SciPy DOP853 with strict tolerances and a second capped-step run
for nonlinear models. Lorenz pointwise comparison is limited to the first 2 s;
the rest of its 40 s run is characterized by boundedness and sampled moments.
Errors are Euclidean state errors at up to 1001 checkpoints. Trial counts,
algorithm derivative evaluations and pure-element evaluations exclude
diagnostic residual reevaluations; the wall-time metric includes diagnostics
and sampling. Python prototype timing must only be compared to other prototype
runs on the same host. Direct C++ `NodeRuntime` timings use a Release build and
five repetitions per case; short runs are sensitive to host load and frequency.

Reproduce with GCC 16/C++26 and Python 3 with NumPy/SciPy:

```bash
cmake -S . -B build-release -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target s7tests_numerical_research_baseline
python3 tools/collect_numerical_baseline.py --output docs/NUMERICAL_FEEDBACK_BASELINE.json
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools -p test_numerical_feedback_research.py
python3 tools/numerical_feedback_research.py --suite gate --output /tmp/s7-numerical-gate.json
python3 tools/numerical_feedback_research.py --suite full --output /tmp/s7-numerical-full.json
python3 tools/numerical_feedback_research.py --suite period --output /tmp/s7-numerical-periods.json
python3 tools/check_lorenz_reference.py --output /tmp/s7-lorenz-reference.json
```

Raw summaries are in [the prototype results](NUMERICAL_FEEDBACK_RESULTS.json),
[the period checks](NUMERICAL_FEEDBACK_PERIODS.json) and
[the direct S7 baseline](NUMERICAL_FEEDBACK_BASELINE.json). The full result
was collected before diagnostic counters and period fields were added to the
same runner; its endpoint equations and numerical results are unchanged. The
full runner is intentionally a research executable, not a replacement for
`NodeRuntime`.

## Initial gate: oscillator and scalar feedback

At `h=0.001`, 200 s, the oscillator `x'=v, v'=-x` from `(1,0)` gives:

| Method | Max state error | Final amplitude | Final phase error (rad) | Algorithm RHS evaluations/step |
| --- | ---: | ---: | ---: | ---: |
| Forward Euler, zero trials | 0.105171 | 1.105171 | -0.0000667 | 1 |
| Backward candidate, 1 trial | 0.095163 | 0.904837 | +0.0001333 | 2 |
| Backward candidate, 2 trials | 0.095163 | 0.904837 | -0.0000667 | 3 |
| Backward candidate, 4 or 8 trials | 0.095163 | 0.904837 | -0.0000667 | 5 or 9 |
| Trapezoid research solve | 0.0000167 | 0.999999975 | -0.0000167 | about 3 |
| Euler–Cromer | 0.000508 | about 1.000213 | +0.000390 | 1 |

The forward-Euler amplitude matches `(1+h²)^(N/2)`; its energy grows by
about 0.110701 by 200 s. The implicit backward root has amplification
`1/sqrt(1+h²)` and therefore damps the same oscillator. Extra fixed-point
trials converge to that dissipative method rather than recover the continuous
invariant. At `h=0.0005`, forward Euler's maximum error falls to 0.051271,
the backward candidate's to about 0.048771, trapezoid's to 0.00000417, and
Euler–Cromer's to 0.000252. Direct S7 `NodeRuntime` forward Euler has the
same final error as the prototype at both timesteps to displayed precision.
Production `TrapezoidIntegrator` has final error 0.0000166667 at `h=0.001`
and 0.00000416667 at `h=0.0005`; its tighter stopping rule avoids the
prototype's small accumulated amplitude loss.

For `x'=1-2x`, `x(0)=0`, 12 s at `h=0.001`, the maximum error is 0.00018409
for forward Euler, 0.00018452 for one backward trial, about 0.00018378 for
converged backward Euler, and `6.15e-8` for the research trapezoid solve.
At half the timestep, forward Euler's maximum error is 0.00009201. The final
state is close to equilibrium for all methods, so maximum transient error is
the meaningful measure. Direct S7 forward Euler and trapezoid match their
closed-form terminal values.

## Equal derivative-evaluation budget

For the oscillator, compare the backward candidate at `h=0.001` with plain
Euler at `h=0.001/(k+1)`; both use the same number of algorithm RHS evaluations
over 200 s. These Euler values use its exact discrete recurrence, independently
of the research runner. The scalar linear loop uses its exact recurrence in
the same way.

| Backward trials `k` | Euler `h` at same budget | Oscillator max error: backward / Euler | Scalar max error: backward / Euler |
| ---: | ---: | ---: | ---: |
| 1 | 0.0005 | 0.095163 / 0.051271 | 0.00018452 / 0.00009201 |
| 2 | 0.000333333 | 0.095163 / 0.033895 | 0.00018378 / 0.00006133 |
| 4 | 0.0002 | 0.095163 / 0.020201 | 0.00018378 / 0.00003679 |
| 8 | 0.000111111 | 0.095163 / 0.011173 | 0.00018378 / 0.00002044 |

The budget comparison is unfavorable to backward trials in both gate models.
It also shows why a small same-timestep improvement is insufficient evidence
for a new production method.

## Extended matrix

The full run contains 284 cases and no unexpected failures at the frozen
timesteps. The maximum discrepancy between the two nonchaotic DOP853 reference
runs is `5.76e-11` in Euclidean state norm. See the raw result file for every
parameter/initial-state/timestep combination.

The undamped pendulum at `(theta,omega)=(1.5,0)`, 100 s, `h=0.001` gives:

| Method | Max trajectory error | Max absolute energy drift | Last-half period (s) |
| --- | ---: | ---: | ---: |
| Forward Euler | 9.728 | 6.713 | 2.69457 |
| Converged backward candidate | 7.914 | 4.900 | 2.16380 |
| Euler–Cromer | 0.002146 | 0.012658 | 2.330987 |
| Research trapezoid | 0.000562 | 0.00000727 | 2.330988 |
| Independent reference | 0 | 0 | about 2.330988 |

The period estimates use upward zero crossings in the final half of the run;
the reference crossing interpolation uses the summary sample grid, so the
last digits do not establish a tight period bound. At `h=0.0001`, maximum
trajectory error falls to 6.511 for forward Euler, 6.130 for backward Euler,
0.000214 for Euler–Cromer and `5.56e-6` for research trapezoid. The large
100 s Euler trajectory errors reflect accumulated energy and phase changes;
they do not imply an incorrect graph or failed reference integration.

For Van der Pol, the 40/60 s, three-initial-state matrix gives the following
range of maximum trajectory error at `h=0.001`:

| `mu` | Forward Euler | Research trapezoid | Last-half period for `(0.1,0)` (Euler / backward / trapezoid / reference) |
| ---: | ---: | ---: | --- |
| 1 | 0.117–0.212 | `2.04e-5`–`3.77e-5` | 6.66817 / 6.65842 / 6.66329 / 6.66330 s |
| 3 | 0.960–2.710 | `3.09e-4`–`9.10e-4` | 8.88339 / 8.83498 / 8.85910 / 8.85906 s |

Backward iteration converges at these small timesteps, but its trajectory
error remains close to forward Euler's and sometimes exceeds it. The higher
`mu` case is more demanding; these tests do not qualify stiff behavior. The
last-quarter `x` ranges in the saved period results provide a bounded
limit-cycle amplitude check for the two displayed starts.

For Lorenz, at `h=0.00025` from `(1,1,1)`, maximum pointwise state error over
the **first 2 s** is 0.2040 for forward Euler, 0.2029 for converged backward
Euler and 0.0001202 for research trapezoid. Halving `h` gives 0.10186, 0.10158
and `3.01e-5` respectively. The second initial state `(1.000001,1,1)` has the
same early error to the shown precision. An independent reference rerun with
`max_step=0.005` differs from the original DOP853 reference by at most
`4.46e-11` over 2 s for either initial state; the detailed check is saved in
[the Lorenz reference record](NUMERICAL_FEEDBACK_LORENZ_REFERENCE.json).
All tested trajectories remain finite with maximum sampled norm around 52.
The last-half means differ substantially among methods and even between the
two close initial states over this 40 s window. That is a short, sensitive
chaotic sample, not evidence for a ranked invariant distribution. The full
40 s maximum pointwise errors of roughly 40–53 are reported in the raw file
as divergence diagnostics, not acceptance scores.

The feedback-control plant `y'=-y+u` with P, PI and saturated PI variants
gives these maximum state errors over 12 s at `h=0.001`:

| Controller | Forward Euler | Converged backward candidate | Research trapezoid |
| --- | ---: | ---: | ---: |
| P | 0.00036833 | 0.00036741 | `1.84e-7` |
| PI | 0.00038626 | 0.00038546 | `1.64e-7` |
| Saturated PI | 0.00026024 | 0.00026002 | `4.34e-8` |

At `h=0.0005`, the Euler and backward errors roughly halve, while the
trapezoid errors fall by about fourfold. The control source is held constant
for each step. The saturation is a pure local function, not a direct
algebraic-loop solver or anti-windup controller. No explicit user-modeled Delay
was removed by trialing.

## Computational cost

For the oscillator at `h=0.001`, the research prototype uses 1, 2, 3, 5 and
9 algorithm derivative evaluations per accepted step for zero, 1, 2, 4 and
8 refinements. The converged backward candidate averages 3 at this timestep;
the research trapezoid also averages about 3. At the pendulum timestep the
converged backward and trapezoid candidates average about 4 and 3.72,
respectively. One extra diagnostic derivative evaluation per fixed-count
implicit step, or one per convergence check, is included in the prototype
wall-time figure but excluded from those algorithm counts. Pure-element
evaluation counts and per-case nanoseconds are in the raw result file.

On the AMD Ryzen 7 4700U Release build, direct `NodeRuntime` oscillator
forward Euler runs in roughly 40–64 ns/step across five short repetitions;
production Trapezoid in roughly 86–151 ns/step. The scalar loop is similarly
host-load sensitive, and its 12,000-step run is particularly short. The
repeat-level measurements are retained in the baseline JSON. These timings
support only a coarse cost comparison, not a stable performance ranking of
small methods. The Python prototype takes microseconds per step and cannot be
compared directly with those C++ nanoseconds. The evaluation counts and
equal-budget error table are the more reliable cost evidence here.

## Failure and causal semantics

The research unit checks verify the direct-cycle/purity gate, held constant
source, deterministic repeated evaluation with unchanged published state,
forward-Euler one-update behavior, the analytical backward and trapezoid
linear roots, and failure without a published state change. At `h=1`, scalar
`x'=1-2x` has a valid backward-Euler endpoint but fixed-point slope `-2`, so
the bounded solver rejects it after 64 corrections. For `z=z²`, the implicit
equation has two roots (0 and 1); the zero Euler predictor selects 0. For
`z=1+z²`, no real endpoint exists and the iteration exposes a non-finite
derivative. These are properties of the equation and solver, not permission to
accept direct-feedthrough algebraic loops in S7.

The direct production `s7core_trapezoid_smoke` and
`s7tests_symplectic_oscillator_smoke` tests pass on this branch. They include
the coupled Trapezoid validation and publication checks already in the repo.

## Recommendation and limits

Do not merge the backward-Euler trial prototype into production on this
evidence. It changes the integration method, damps conservative systems, and
loses to simple timestep reduction at equal derivative-evaluation budget in
the initial gate. Keep forward Euler as an intentional, inspectable method.
Coupled Trapezoid and Euler–Cromer remain useful where their existing contracts
apply. For the scalar `x'=1-2x` block with constant input, a bilinear/TPT
one-pole realization has the same multiplier `(1-h)/(1+h)` and source increment
`h/(1+h)` as trapezoid, so it is not an independent general-network result.
No generic TPT/Zavalishin or direct algebraic-loop method is proposed:
a local realization, unique-solution envelope, and causal port contract are
still needed. A production feedback-refinement feature would require separate
numerical and causal-semantic review plus an explicit maintainer decision.

The prototype covers only F64 pure scalar graphs and constant external input.
It does not qualify mixed precision, plugins, nested schemas, stateful direct
feedthrough, hardware I/O, arbitrary algebraic loops, stiff equations, or
rollback of production element internals. Its timing includes Python overhead
and diagnostic reevaluations. Lorenz long-run pointwise error cannot rank
methods because trajectories diverge chaotically. These limits prevent a
general-method recommendation from the tested models.
