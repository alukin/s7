# TrapezoidIntegrator numerical contract

Implemented 2026-10-19. The default `mode="implicit"` solves the trapezoidal
ODE step for coupled graph states:

```
z_next = z + dt/2 * (F(z, u) + F(z_next, u))
```

`z` is the vector of implicit TrapezoidIntegrator outputs; `u` is the vector
of other inputs held during this interval. For autonomous smooth systems this
is the second-order implicit trapezoidal method. It does not require adding
Delay elements to the model's feedback loops.

## Causality and execution

A default TrapezoidIntegrator is causal memory (`Feedthrough::None`). Its
published output is the current state, not the tentative endpoint. The usual
validator/compiler causal-cycle rules remain in force; arbitrary direct
algebraic cycles remain invalid.

After the normal causal phase, NodeRuntime:

1. Captures all implicit states and their current derivatives.
2. Forms a simultaneous forward-Euler endpoint predictor.
3. Reevaluates the stateless derivative dependencies at the candidate state.
4. Forms all trapezoidal corrections before publishing any candidate, so
   directly coupled integrators read the same iterate regardless of insertion
   order. Repeats until every state meets its convergence criterion.
5. Stages the converged endpoint, restores every trial output, and runs the
   ordinary causal-memory elements against the published current values.
6. Publishes staged outputs at the beginning of the next runtime step, using
   the same convention as EulerIntegrator and Delay.

Only the compiler-selected derivative dependency cone is reevaluated. The
compiler preserves its causal order. The validator rejects stateful direct
feedthrough in a dependency that varies with the trial state, naming the
unsafe element. A filter, nested stateful schema or I/O operation cannot be
advanced repeatedly by the solver. Other causal-memory outputs and
independent source outputs stay fixed during the solve. Plugins declaring
`has_state=false` must implement genuinely stateless operations suitable for
reevaluation; hidden side effects violate that declaration.

The optional `TrapezoidIntegration` interface in `s7/trapezoid.hpp` separates
candidate preparation from publication. The runtime uses it without changing
ordinary elements' `step()` API or introducing Studio dependencies in core.
Calling an implicit integrator's `step()` directly outside NodeRuntime is an
error: one block cannot solve an unknown coupled derivative graph by itself.

## Solver and supported envelope

The first implementation uses simultaneous fixed-point correction, at most
64 iterations per step. Each state tests the change in its native real or
complex component precision against `8*epsilon*(1+abs(candidate))`; F128 is
not reduced to double. Non-finite candidates and nonconvergence produce
explicit runtime errors. Reduce `dt` when fixed-point iteration is not
contractive. This solver is not a Newton solver and does not establish stiff
problem support: an implicit equation can have a valid solution even when
this iteration fails to converge.

Trial values and affected published outputs are restored on failure; the
runtime step counter does not advance. As with existing runtime exceptions,
ordinary stateful direct-feedthrough elements may already have advanced their
private state in the normal causal phase. This is not general transactional
rollback of arbitrary element internals or external I/O. Studio terminates a
failed run; starting a new run reconstructs its runtime.

Other sources and integration methods are held inputs for this numerical
stage. A mixed-method graph or continuously time-varying external forcing
does not automatically inherit second-order global accuracy. Use autonomous
Trapezoid states for the reference-tested coupled ODE workflow; explicitly
validate any forcing/interconnection scheme and timestep.

## Supplied-sample quadrature

`mode="sampled"` performs trapezoidal quadrature of an already supplied
sequence `x[n]`:

```
y[0] = initial_value
y[n] = y[n-1] + dt/2 * (x[n-1] + x[n])
```

Its first call records the initial input without integrating. This mode has
direct feedthrough and cannot alone break a feedback cycle. It is useful for
integrating sampled signals, including exact integration of a linear ramp;
it is not an implicit ODE solve. `dt`, `initial_value` and signal-type support
(F16–F128, C16–C128) apply to both modes. The C++ constructor's optional third
argument is `TrapezoidMode::Implicit` or `TrapezoidMode::Sampled`.

The earlier pendulum arrangement used this sample-averaging operation with
added delays and was only first order as a complete ODE method. The nine
current [comparison diagrams](../examples/double_pendulum/README.md) use
implicit Trapezoid without those delays, alongside Euler and Symplectic.

## Verification

`s7core_trapezoid_smoke` checks all real/complex floating types, scalar and
nonlinear second-order convergence, exact discrete scalar/coupled solutions,
a conservative oscillator over 200 model seconds, insertion-order independence,
held source stepping, unsafe-stateful-feedback rejection, direct-cycle
rejection, nonconvergence and published-state restoration/retry.
`s7core_type_inference_smoke` checks sampled ramp quadrature and rejection of
a sampled-only algebraic feedback loop.

The double-pendulum tests compare all three modes/three methods at
1/10/100 kHz for 20 model seconds, against continuous normal modes and closed-form
discrete solutions. For implicit trapezoid, each tenfold frequency increase
must reduce maximum angle error by 70–130 times (second-order convergence);
relative energy error must remain below `1e-8`. See the examples README for
measured results and reproduction commands.

The [Phase 3 real-graph comparison](NUMERICAL_INTEGRATION_PHASE3_REPORT.md)
executes Euler substeps and two-stage Gauss–Legendre through a research harness.
The separate [production integration PR](https://github.com/alukin/s7/pull/8)
adds opt-in runtime methods; the Phase 3 harness remains research code.
It records cases where this trapezoid implementation's fixed-point solve
fails at large `dt` even though its mathematical transition exists. Those
failures reflect the solver's contraction requirement, not instability of
the implicit trapezoidal formula. This research branch does not change this
trapezoid implementation or its publication contract.

Mathematical background: [G. Fass, numerical ODE lecture notes](https://www.math.iit.edu/~fass/472Notes)
(trapezoidal rule and fixed-point solution), and the
[SUNDIALS nonlinear-solver documentation](https://sundials.readthedocs.io/en/latest/sunnonlinsol/SUNNonlinSol_links.html)
(fixed-point versus Newton iteration). S7 does not depend on SUNDIALS.
