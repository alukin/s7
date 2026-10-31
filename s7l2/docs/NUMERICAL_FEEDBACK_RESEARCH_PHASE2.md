# Numerical feedback research, Phase 2: discrete structure

The [Phase 2 report](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md) records the
measurements and answers to the six research questions. Its compact data is
in [the audited summary](NUMERICAL_FEEDBACK_PHASE2_SUMMARY.json).

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
implicit root is `(I-Q)^{-1}` times this signed residual. The
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
angle divided by `h`. At `h omega0=2` the repeated `-1` pole is defective
and can grow linearly; the zero-step limit is the identity.
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
The implemented cancellation threshold scales with machine epsilon and the
inverse separation of the internal poles, with a square-root-epsilon floor
for a repeated root. A deliberately weak but nonzero mode remains visible in
the tests. Near a multiple zero, algebraic identifiability is still limited
by binary64 conditioning; printed roots are not exact symbolic factors.

## Circuit formulas and independent references

For RC, `tau=RC`, `A=-1/tau`, `B=1/tau`. The exact sampled pole is
`exp(-h/tau)`, and exact input gain is `1-exp(-h/tau)`. With constant input
`u`, `x(t)=u+(x(0)-u)exp(-t/tau)`. Forward Euler's pole is `1-h/tau`, stable
in magnitude for `0<h/tau<2` but alternating for `1<h/tau<2`. Backward
Euler's pole is `1/(1+h/tau)`; trapezoid's is
`(1-h/(2tau))/(1+h/(2tau))`. The fixed-point correction for backward Euler
requires `h/tau<1` for arbitrary starts, a narrower condition than either
the solvability or stability of the implicit method.

For LC with state `(x,v)`, `A=[[0,1],[-omega0²,0]]`. Since `A²=-omega0² I`,
`Phi=I cos(omega0 h)+(A/omega0)sin(omega0 h)`. For a force applied to the
second derivative, `B=[0,b]ᵀ`, exact `Gamma=[b(1-cos(omega0 h))/omega0²,
b sin(omega0 h)/omega0]ᵀ`. The autonomous reference is
`x=x0 cos(omega0 t)+v0 sin(omega0 t)/omega0` and
`v=v0 cos(omega0 t)-omega0 x0 sin(omega0 t)`.
Finite simultaneous correction has poles
`p_k(±i h omega0)` with `p_k(w)=sum_{j=0}^{k+1}w^j`. At `k=1`,
`|p_1(i theta)|²=1-theta²+theta⁴`, so it is damped for `0<theta<1` even
though forward Euler grows for every positive `theta`. Its phase is the
argument of `1+i theta-theta²`, not the physical angle `theta`.

For series RLC, `A=[[0,1],[-1/(LC),-R/L]]`, `B=[0,1/L]ᵀ`, and capacitor
voltage output `C=[1/C,0]`. The continuous poles are
`-R/(2L) ± sqrt((R/(2L))²-1/(LC))`, and the continuous transfer is
`1/(LC s²+RC s+1)`. With `L=1`, `C=0.25`, the cases `R=1,4,6` are
under, critical and over damped. For a unit step from zero state, write
`alpha=R/(2L)`, `omega0²=1/(LC)` and `V_C=1-g(t)`:

* Under damped: `g=e^(-alpha t)[cos(omega_d t)+(alpha/omega_d)sin(omega_d t)]`,
  `omega_d=sqrt(omega0²-alpha²)`.
* Critical: `g=(1+alpha t)e^(-alpha t)`.
* Over damped: with `lambda±=-alpha±sqrt(alpha²-omega0²)`,
  `g=(lambda+ exp(lambda- t)-lambda- exp(lambda+ t))/(lambda+-lambda-)`.

The augmented exponential remains the implementation reference for arbitrary
linear systems, including singular `A`; these closed forms independently
check its RC, LC and RLC results. The mass-spring-damper correspondence
`q↔x`, `i↔v`, `L↔m`, `R↔b`, `1/C↔spring stiffness`, and `u↔force` gives
identical state and input matrices when parameters are mapped. Output scaling
must also be mapped: capacitor voltage `q/C` corresponds to spring force
`stiffness*x`, not displacement alone.

For the stated RLC pre-update output at `h=0.2`, the forward-Euler transfer
has a constant numerator and no finite zero; backward Euler has a zero at
`z=0`; trapezoid has a zero at `z=-1`. Exact sampling has a zero near
`z=-0.93519` for `R=L=1,C=0.25`. All retain two internal states. These
zeros change how a sampled input reaches capacitor voltage; they are not
extra autonomous modes. A deliberately hidden diagonal mode in the tests
has two internal eigenvalues but only one transfer pole after cancellation.
