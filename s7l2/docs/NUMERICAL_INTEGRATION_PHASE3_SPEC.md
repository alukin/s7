# Numerical integration research: Phase 3 contract and plan

Branch: `research/numerical-feedback-0.2.0`. This continues the [Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md)
and [Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md) work. The primary
systems are only the two-integrator harmonic oscillator, one-integrator RC,
and two-integrator RLC (`R=0,1,4,6`). All elements are ideal computational
elements; no amplifier or device nonideality is modeled.

## Short implementation plan

1. Freeze the update, sampling, publication, cost and failure contract below.
2. Build a headless research policy over validated and compiled **real S7
   elements**. Accept F64 Euler integrators, constants and a small whitelist
   of stateless arithmetic only. Compile a safe trial derivative cone from
   schema connections, as the coupled trapezoid implementation does.
3. Implement simultaneous Euler substeps and a coupled two-stage Gauss–Legendre
   solve. Keep all stage state temporary and restore published outputs before
   completing an outer step. Test rollback and insertion order.
4. Compare with ordinary fine-step `NodeRuntime`, production trapezoid and
   Euler–Cromer where applicable, and independent exact sampled formulas.
   Record operation counts, time, energy, phase, poles and step responses.
5. Generate compact checked-in evidence and ignored full data/figures, then
   report numerical findings and the architectural blocker to production use.

The experimental policy is explicitly selected by a research executable;
ordinary `NodeRuntime::step()` and integrator elements keep their current
meaning. No arbitrary algebraic loop or plugin trial is authorized.

## State and timing

Physical interval `n` is `[nh,(n+1)h)`. `x_n` is the persistent state and
`u_n` is sampled once at the interval start and held through every substep or
stage. The first `step()` publishes `x_0` and stages `x_1`; the next `step()`
publishes `x_1`, matching S7 causal-memory publication. No internal state or
stage becomes an ordinary output or observation. A failed solve restores all
published outputs and leaves the pending stage and accepted step count intact.

The research graph contains only EulerIntegrator F64 states and eligible
stateless derivative elements. No unrelated stateful element, nested schema,
I/O, plugin, sampled trapezoid, or mixed integration method is accepted.
An independently held constant is evaluated once per physical step. The
trial cone contains stateless nodes that are both downstream of a selected
integrator output and upstream of a selected derivative input. All state
components receive the same trial vector before that cone runs. The real
compiled topological order determines evaluation order inside the pure cone,
not element insertion order.

## Euler substepping

For `delta=h/m`, start `z_0=x_n` and compute `z_{j+1}=z_j+delta f(z_j,u_n)`
for `j=0..m-1`, updating **all** coupled components together. This is not
fixed-point iteration. For a linear system `x'=Ax+Bu`,

```
M_E,m = (I+delta A)^m,
G_E,m = delta sum_{j=0}^{m-1}(I+delta A)^j B.
```

Ordinary Euler at `delta` with every `m`th state observed has the same
recurrence under identical held forcing. The engineering distinction is
the outer publication/control rate, not a new numerical method. Primary
counts `m=1,2,4,8,16,32` use `m` derivative-vector evaluations per outer
step. Initial examples hold inputs constant; a varying source sampled at a
different rate invalidates the equivalence unless its hold convention agrees.

## Two-stage fourth-order Gauss–Legendre

Use `a11=a22=1/4`, `a12=1/4-sqrt(3)/6`,
`a21=1/4+sqrt(3)/6`, `b1=b2=1/2`, and
`c1=1/2-sqrt(3)/6`, `c2=1/2+sqrt(3)/6`.
The coupled unknown is `(K1,K2)` with
`F_i(K)=K_i-f(x_n+h sum_j a_ij K_j,u_n)=0`.
Accepted `x_{n+1}=x_n+h(K1+K2)/2`. A bounded damped Newton solve uses a
finite-difference Jacobian of this **full coupled stage residual**, residual
norm `||F||∞/(atol+rtol max(1,||K||∞))`, and explicit nonfinite/singular/
iteration-limit failures. The tolerance and maximum iterations are recorded
with each run. A residual evaluation costs two graph derivative evaluations;
Jacobian probes and line-search trials are included in the count. The
stage vectors are temporary, not persistent S7 modes.

For linear `A`, write `Q=hA` and `P=I-Q/2+Q²/12`. The exact method transition
and constant-input operator are

```
M_GL = P^{-1}(I+Q/2+Q²/12),
G_GL = h P^{-1} B.
```

The input formula follows from `R(Q)-I=P^{-1}Q`; it does not require `A^{-1}`.
`R(z)=(1+z/2+z²/12)/(1-z/2+z²/12)` is A-stable, not L-stable (`R(z)→1`
for very negative real `z`). On the imaginary axis its magnitude is one;
the oscillator phase error starts at order `h^5` per step. The continuous
closed-form oscillator and RC/RLC responses and exact zero-order-held
transition are independent references. Fourth-order global convergence
applies to smooth autonomous dynamics or forcing sampled correctly at stage
times. Holding a varying input over a full interval approximates a different
physical forcing and cannot support an unqualified fourth-order claim.

## Acceptance and architecture boundary

The C++ regression must validate and compile every graph, check Euler
substep/fine-Euler equivalence, the independent GL matrix/input formula,
fourth-order refinement, lossless energy, damped responses, exact publication,
determinism, order independence, eligibility rejection and failure rollback.
The full-duration matrix and plots remain opt-in. Tests use double precision
with explicit tolerances and do not write under `$HOME`.

The current compiler precomputes a safe RHS trial cone specifically for
implicit trapezoid elements. It has no public generalized trial-stage
contract for Euler elements, and ordinary `NodeRuntime` owns its own staging
and rollback machinery. A production Gauss–Legendre policy would require an
architectural decision about per-graph policy selection, eligible elements,
trial state ownership, failure semantics, and rate validation. The research
policy must not silently extend the production contract.
