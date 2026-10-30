# Numerical feedback research: N0 contract

Branch baseline: `0b75330d7334f36ec79a9b67a547633466dbff0c` on `main`.
This contract freezes the experiments before interpreting their results. The
research runner is separate from `NodeRuntime`; it does not change production
methods, validation, `Element::step()`, or the coupled Trapezoid stage.

## Existing transitions and publication

Let `z_n` be integrator outputs published for model step `n`, `h > 0`, and
`f(z,u)` the output of a stateless, direct-feedthrough derivative graph. During
one step, external inputs `u_n`, every unrelated causal-memory output, and
all internal integrator base states `z_n` remain fixed. A trial may temporarily
replace *only* the selected integrator outputs seen by `f`; it must not call a
stateful element's `step()`. All state components use the same trial iterate.
After an accepted trial, each integrator stages its endpoint exactly once. The
published `z_n` is restored before observation or other memory transitions.
The staged value appears at the next `NodeRuntime::step()` publication, exactly
as in the existing runtime. Step 1 therefore observes the initial condition.

| Method | Exact transition / applicable scope |
| --- | --- |
| Forward Euler | `z_{n+1}=z_n+h f(z_n,u_n)`; one derivative evaluation. The existing `EulerIntegrator` remains this method. |
| Backward Euler candidate | `z_{n+1}=z_n+h f(z_{n+1},u_n)`; an implicit endpoint equation, solved experimentally by simultaneous fixed-point iteration. This is **not** repeated forward Euler. |
| Coupled Trapezoid | `z_{n+1}=z_n+h/2 [f(z_n,u_n)+f(z_{n+1},u_n)]`; existing implementation uses a forward-Euler predictor and up to 64 simultaneous fixed-point corrections. Its current tolerance, rollback limits and behavior remain authoritative. The separate research runner uses the equation with its own normalized residual stop, so its finite-tolerance result must be labeled separately from the production implementation. |
| Symplectic Euler / Euler–Cromer | For `x'=v, v'=a(x)` only: `v_{n+1}=v_n+h a(x_n)`, `x_{n+1}=x_n+h v_{n+1}`. The existing block is one causal-memory element. Velocity-dependent acceleration loses the symplectic guarantee. |
| Sampled Trapezoid | `y_0=initial`, `y_n=y_{n-1}+h/2(u_{n-1}+u_n)` after the first sample; direct feedthrough, not an implicit ODE method. |
| Controlled integrator | Locally chooses reset, enabled, clamped rectangle update or hold; reset/enable/saturation decisions are not trialed by this experiment. |

No generic TPT/Zavalishin candidate is specified: it needs a **local filter or
integrator realization and zero-delay port contract** before a comparison can
be meaningful. The existing coupled Trapezoid is a reference for the
trapezoidal formula, not evidence that arbitrary TPT networks are supported.

## Trial region and deterministic execution

The experimental graph accepts scalar real integrators, fixed sources and a
whitelist of pure functions (gain, sum, product, sine, saturation). The trial
region is the intersection of (a) pure nodes reachable downstream from a
trial integrator output and (b) pure nodes upstream of a selected integrator
derivative. It is evaluated in a stable topological order, once per trial.
Derivative inputs outside that region retain their base-step values. Other
causal-memory nodes terminate traversal and hold their published outputs.
Any stateful direct-feedthrough element, I/O, plugin with unknown purity,
unbound input, type mismatch, or direct-feedthrough algebraic cycle is
ineligible. The experimental harness rejects direct cycles. This is a narrow
research envelope, not a production authorization for new loop semantics.

For `k=0` refinement, use forward Euler's one derivative evaluation and
commit. For `k=1,2,4,8`, seed the endpoint with forward Euler and take exactly
`k` further simultaneous fixed-point corrections. A converged run uses the
same predictor and at most 64 corrections. Fixed-count results are reported
with their final **equation residual**, whether or not converged; they are not
silently relabeled as a solved implicit method. Trial counts do not include
the base derivative evaluation. A full derivative reevaluation used solely to
measure the residual is counted in cost, but does not change the endpoint.

The residual is `max_i |z_i - z_{n,i} - h f_i(z,u_n)| /
(atol+rtol*max(|z_i|,|z_{n,i}|))`, with `atol=rtol=1e-12` in F64 research
experiments. A result is converged if this dimensionless residual is at most
1. Non-finite trial values and failed 64-correction convergence reject the
model step, retain the previous published/staged state, and report model,
method, `h`, step index, iteration count and residual. The runner stops on the
first failure. It does not model rollback of arbitrary production element
internals, which the existing runtime also does not guarantee.

Reevaluation with `z_n` unchanged yields the same `f(z_n,u_n)` and cannot
refine any method. An explicit Delay output remains held throughout trialing;
this mechanism cannot erase a user-modeled one-step delay. Direct algebraic
loops have a different fixed-point equation and remain invalid.

## Frozen model matrix and independent references

All state samples are indexed by accepted transition count `m` and compared at
physical time `m h`; the research runner also exposes the corresponding S7
publication as step `m+1`. Sources are constant in the initial matrix, so no
within-step forcing interpolation is assumed. The automated runner includes:

| Model | Parameters / initial state | `h`, duration | Independent reference |
| --- | --- | --- | --- |
| Harmonic oscillator | `x'=v, v'=-x`, `(1,0)` | `0.001,0.0005`; 200 s | `(cos t,-sin t)` and discrete amplification checks |
| Linear feedback | `x'=1-2x`, `x=0` | `0.001,0.0005`; 12 s | `(1-exp(-2t))/2` |
| Pendulum | `theta'=omega, omega'=-9.81 sin(theta)`, `(1.5,0)` | `0.001,0.0001`; 100 s | independent high-accuracy DOP853 and refinement check |
| Van der Pol | `x'=v, v'=mu(1-x²)v-x`, `mu=1,3`, starts `(0.1,0),(2,0),(4,0)` | `0.001,0.0005`; 40/60 s | independent DOP853 |
| Lorenz | `sigma=10,rho=28,beta=8/3`, starts `(1,1,1),(1.000001,1,1)` | `0.00025,0.000125`; 40 s | independent DOP853, short horizon only for pointwise error |
| P/PI/saturated PI plant | `y'=-y+u`, `r=1`, `Kp=2`, `Ki=1`, `u=Kp(r-y)+Ki q`, `q'=r-y`; saturation `u in [-1,1]` when enabled, `(y,q)=(0,0)` | `0.001,0.0005`; 12 s | analytical P; independent DOP853 for PI variants |

The oscillator and linear feedback are the initial gate; the nonlinear cases
are interpreted only after their graph and basic contract checks pass.
DOP853 is used **only outside** the experimental graph as an independent
continuous-time reference. Its tolerances are `rtol=2e-13, atol=2e-14`,
cross-checked against a second run capped at a 0.025 s maximum step for
nonchaotic models. For Lorenz, an additional 0.005 s maximum-step run is
compared over `t <= 2 s`; the 0.025 s cap alone is inactive at these strict
tolerances and gives an uninformative zero discrepancy.
Lorenz's long-run pointwise error is reported as divergence, not a pass/fail
test; short horizon is `t <= 2 s`.

## Measurements and decision rules

For each case, record final/max/RMS Euclidean trajectory error at up to 1001
uniformly spaced checkpoints plus the terminal point; oscillator
phase and amplitude/energy error; pendulum energy drift; final normalized
solver residual; derivative and pure-element evaluations; convergence failures;
and wall time per accepted step. Compare at the common timesteps and at
roughly equal derivative-evaluation budgets by reducing `h` for zero-trial
Euler. CPU time is only comparable within the same runner/process and host;
the production indexed serial runtime is measured separately. For nonlinear
oscillators report bounded period/amplitude observations; for Lorenz report
short-horizon error and long-run boundedness/statistics. No universal error
tolerance or method-ranking threshold is assumed. A fixed trial count is
useful only if its accuracy and failure behavior justify the extra work at an
equal budget, with the method named by its actual equation.
