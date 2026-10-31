# Numerical feedback research, Phase 2: discrete structure

## Scope and review

Phase 1 (`NUMERICAL_FEEDBACK_RESEARCH_N0.md` and the accompanying report)
established a separate pure-graph trial runner, analytical oscillator and scalar
references, nonlinear comparisons, and direct S7 baselines. Its fixed correction
counts are **finite backward-Euler fixed-point iterations**, not solved backward
Euler. The saved full result predates diagnostic counters and records a baseline
commit rather than its producing source revision. It has no systematic poles,
zeros, RC/RLC matrix, or similarity/ordering comparison. The 272 KiB full JSON
is retained as historical evidence; Phase 2 checks in compact summaries and
generates full data and figures under a build directory on demand.

Plan: derive linear transitions and transfer timing; implement an independent
matrix laboratory for RC, LC, RLC and their mechanical analogues; verify it
against the existing trial runner and closed forms; measure cost and produce
plots; add headless CTest regressions and interpret the results.

## State, input and output convention

Let `x_n` be the state published for physical time `t_n=nh`. The input `u_n`
is held on `[t_n,t_{n+1})`. The measured output is `y_n=Cx_n+Du_n` **before**
the update. S7's first runtime step publishes the initial state, so its step
index is `n+1`. For `x_{n+1}=Mx_n+Gu_n`, the zero-initial-condition transfer is
`H(z)=C(zI-M)^{-1}G+D`. A measurement of the newly staged state instead has
`C z(zI-M)^{-1}G+D` under this indexing; its zeros can differ. A one-step
publication delay adds `z^{-1}` and is an actual extra memory element.

For `x'=Ax+Bu`, `y=Cx+Du`, exact zero-order-hold sampling is
`Phi=exp(Ah)`, `Gamma=integral_0^h exp(A tau) B d tau`. The block identity
`exp(h [[A,B],[0,0]])=[[Phi,Gamma],[0,I]]` avoids assuming `A` invertible.
Continuous poles are eigenvalues of `A`; sampled internal poles are
`exp(s_i h)`. Transfer poles can be a strict subset when modes cancel.

Write `Q=hA`. Forward Euler has `M=I+Q`, `G=hB`; backward Euler has
`M=(I-Q)^{-1}`, `G=(I-Q)^{-1}hB` when invertible. Implicit trapezoid has
`M=(I-Q/2)^{-1}(I+Q/2)`, `G=(I-Q/2)^{-1}hB`. Input here is held at the same
value for both trapezoid endpoint evaluations. These are matrix functions and
therefore commute with every nonsingular state-coordinate transformation.

## Finite simultaneous feedback settling

Phase 1 evaluates `f_0=Ax_n+Bu_n`, predicts
`p_0=x_n+h f_0`, then performs `k` simultaneous corrections
`p_j=x_n+h(A p_{j-1}+Bu_n)` for `j=1..k`. Thus `k=0` is forward Euler and

```
M_k = sum_{j=0}^{k+1} Q^j,
G_k = h sum_{j=0}^{k} Q^j B.
```

The implicit equation residual is
`p_k-x_n-h(Ap_k+Bu_n) = -Q^(k+1) h f_0`. The solver error relative to the
implicit root is `(I-Q)^{-1}` times this residual with sign reversed. The
iteration converges for every initial state/input when `rho(Q)<1`; at the
boundary convergence depends on the particular vector and Jordan structure.
When it converges, `M_k -> (I-Q)^{-1}` and `G_k -> (I-Q)^{-1}hB`.
Backward Euler can be numerically stable while this fixed-point algorithm
diverges: for `A=-1/tau`, `h/tau=2`, its pole is `1/3` but `rho(Q)=2`.
Increasing `k` reduces solver error only inside the convergence region; it
does not necessarily reduce error against the continuous solution.

For a sequential coordinate sweep, split `A=L+U`, with lower triangle and
diagonal in `L`. One correction solves
`(I-hL)p_j=x_n+hU p_{j-1}+hBu_n`, so the iteration matrix is
`T=(I-hL)^{-1}hU`. It converges when `rho(T)<1`. Finite sweeps generally
depend on coordinate order; their limit is the same backward-Euler root when
the solve exists and converges. This distinction is an algorithmic ordering
effect, not a property of equivalent continuous systems.

## Oscillator and structural tests

For `x'=v`, `v'=-omega0^2 x`, velocity-first Euler–Cromer gives
`M_S=[[1-h² omega0²,h],[-h omega0²,1]]`. Its determinant is one and its
trace is `2-h² omega0²`. For `0<h omega0<2`, eigenvalues lie on the unit
circle at angle `acos(1-(h omega0)²/2)`; the modified frequency is this
angle divided by `h`. Endpoints are defective and can grow linearly.
The map is symplectic for a separable Hamiltonian oscillator in canonical
position/momentum coordinates (the velocity form is conjugate under constant
mass scaling). Velocity-dependent damping and forcing are outside that claim.

For a pole `z=r exp(i theta)`, numerical damping is `-log(r)/h` and
oscillation frequency is `abs(theta)/h`. Compare to continuous
`-Re(s)` and `abs(Im(s))`; `r>1`, `r<1`, and `r≈1` distinguish growing,
decaying, and marginal modes with a tolerance stated by the experiment.
For an undamped oscillator, forward Euler has `r=sqrt(1+(h omega0)^2)`,
backward Euler `r=1/sqrt(1+(h omega0)^2)`, and trapezoid `r=1` with angle
`2 atan(h omega0/2)`. Energy scales as `r^(2N)` for the first three when
measured in normalized oscillator coordinates. Euler–Cromer conserves a
modified quadratic energy, so physical energy oscillates within a bound.

SISO zeros describe input/output cancellation, not new independent state.
For a two-state model with `D=0`, the transfer numerator is
`(C G) z + C(M-tr(M)I)G`; its denominator is `det(zI-M)` before cancellation.
Common factors are removed with a scale-aware numerical tolerance. Internal
eigenvalues are always reported separately from transfer poles. Coordinate
similarity preserves `M`, `G`, `C` up to basis change in exact arithmetic;
roundoff, sequential sweeps, mixed methods, extra delays, or distinct input
holds can break observed equivalence for identifiable reasons.
