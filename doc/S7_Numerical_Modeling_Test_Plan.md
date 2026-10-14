# S7 Numerical Modeling and Integrator Validation Plan

## 1. Purpose

This document defines a staged validation plan for **S7**, a causal,
graph-based streaming computation system. The goal is to establish that
S7 can model differential equations accurately and reproducibly,
beginning with larger linear systems and then progressing to nonlinear
systems with well-understood reference behavior.

The plan is intended for an AI coding assistant working on the S7
repository. Treat it as a test strategy, not as permission to redesign
the architecture. First inspect the existing code, testing conventions,
graph semantics, and integrator implementation. Propose changes before
making broad architectural modifications.

## 2. Core approach

Model differential equations through their **causal computational
graphs**:

\[ `\dot{\mathbf{x}}`{=tex} = f(`\mathbf{x}`{=tex}, `\mathbf{u}`{=tex},
t) \]

The continuous model is represented by operations such as integrators,
gains, sums, products, and other local functions. A discrete
implementation replaces continuous-time state evolution with discrete
causal elements while preserving the graph's local operations.

The central approach is:

> Discretize the computational graph of the differential equation rather
> than trying to transform the complete nonlinear equation into the
> Z-domain.

For example, an analog-computer model of an ODE can be represented by
integrators connected to summers, multipliers, and gain blocks.
Replacing an analog integrator with a discrete integrator/accumulator
gives a direct path to a causal discrete-time model.

Keep the following concerns separate:

-   correctness of the equation and its graph;
-   correctness of each numerical integration method;
-   numerical stability and long-term qualitative behavior;
-   S7 runtime scheduling, state updates, and causality;
-   performance and resource use for larger graphs.

Do not interpret a successful run alone as proof of correctness.

## 3. General testing principles

### 3.1 Reproducibility

Every test case should define and record:

-   equation or state-space model;
-   parameters and units;
-   initial conditions;
-   integration method and all method-specific settings;
-   timestep (h);
-   simulated duration and number of steps;
-   reference solution and its provenance;
-   comparison metrics and tolerances;
-   S7 version/commit and relevant build configuration.

Tests should be deterministic where the model and runtime permit it. Any
randomized tests must record their seed.

### 3.2 Independent reference solutions

Use the strongest practical reference for each model:

1.  **Analytical solution**, where available.
2.  **Matrix exponential or other well-established closed-form
    calculation** for linear systems.
3.  **High-precision or independently implemented numerical solver**
    when a closed form is impractical.

Do not use the same S7 integration path to generate both the result and
its reference. Avoid treating a very small timestep as an exact answer
without checking convergence.

### 3.3 Multiple views of correctness

Evaluate more than pointwise sample error. Depending on the system,
compare:

-   absolute and relative error;
-   residuals in the differential equation or state update;
-   phase portraits;
-   eigenvalue-related behavior and modal decay/growth;
-   conserved quantities and invariants;
-   oscillation period and amplitude;
-   limit-cycle characteristics;
-   long-run drift;
-   statistical and geometric properties of chaotic attractors;
-   execution time and memory/allocation behavior for larger graphs.

Use metrics appropriate to the model. For chaotic systems, pointwise
trajectory agreement after divergence becomes expected is not a useful
long-term pass criterion by itself.

### 3.4 Long-duration tests

Include short tests for fast fault localization and long tests that
expose accumulated numerical effects. Long tests should be large enough
to make slow drift, instability, or unintended state behavior visible,
but bounded so they remain practical for continuous integration.

### 3.5 Do not prejudge integration methods

A historical hypothesis to investigate is that some seemingly more
accurate integrators may behave worse as causal stateful graph elements
because their implementation introduces additional internal dynamics.

This is a **hypothesis to test**, not an established conclusion.
Distinguish carefully between:

-   poles and zeros of the input/output transfer function of a
    linearized or linear integrator;
-   extra state variables or internal stages in a numerical method;
-   parasitic modes of multistep methods;
-   numerical instability caused by timestep and system dynamics;
-   implementation defects in S7.

For example, the trapezoidal integrator for (`\dot{x}`{=tex}=u) has
transfer function

\[ `\frac{X(z)}{U(z)}`{=tex} =
`\frac{h}{2}`{=tex}`\frac{1+z^{-1}}{1-z^{-1}}`{=tex}, \]

which has one pole and one zero. Do not describe it as having an extra
pole without a precise, justified definition. Multi-stage and multistep
methods need to be analyzed according to their actual equations and
realization.

## 4. Stage 0 --- Establish a trustworthy test harness

Before comparing integration methods:

1.  Inspect the current S7 graph model, stateful element lifecycle,
    tick/evaluate/step semantics, and existing tests.
2.  Identify how initial state, timestep, and simulation length are
    represented.
3.  Ensure state updates are causal and occur exactly when intended;
    detect accidental repeated updates during graph evaluation.
4.  Add a small deterministic harness that can run a graph for (N) steps
    and export timestamped state samples.
5.  Make reference data and test parameters explicit rather than hidden
    in GUI state.
6.  Verify that the same model produces the same results across repeated
    runs.
7.  Add diagnostic output for failed comparisons: first failing sample,
    maximum error, RMS error, final state, and relevant model
    configuration.

The harness should be usable headlessly. GUI plotting can help inspect
results but must not be required to run the tests.

## 5. Stage 1 --- Linear systems, from simple checks to large coupled models

Start with linear systems before nonlinear equations. These tests
isolate runtime and integration issues from nonlinear model
construction.

### 5.1 Scalar first-order systems

Test

\[ `\dot{x}`{=tex}=ax+bu \]

with constant and time-varying inputs, and with positive, zero, and
negative (a). Include zero input and nonzero initial state.

Check the analytical solution, expected growth/decay, steady-state
behavior where applicable, and convergence as (h) is reduced.

### 5.2 Linear second-order oscillators

Test standard linear systems such as

\[
`\ddot{x}`{=tex}+2`\zeta`{=tex}`\omega`{=tex}\_0`\dot{x}`{=tex}+`\omega`{=tex}\_0\^2x=u(t).
\]

Cover underdamped, critically damped, and overdamped cases. Include free
response and forced response. Compare displacement and velocity with an
analytical solution or a trusted reference.

This stage tests state-space decomposition and feedback connections
before increasing graph size.

### 5.3 Coupled linear systems of increasing size

Use the state-space form

\[ `\dot{\mathbf{x}}`{=tex}=A`\mathbf{x}`{=tex}+B`\mathbf{u}`{=tex},
`\qquad`{=tex}
`\mathbf{y}`{=tex}=C`\mathbf{x}`{=tex}+D`\mathbf{u}`{=tex}. \]

Build families of models with increasing state dimension, for example
small, medium, and large cases chosen to remain practical for the
current runtime. Include:

-   diagonal systems with known independent modes;
-   coupled systems with real eigenvalues;
-   oscillatory systems with complex-conjugate eigenvalue pairs;
-   systems with widely separated time constants;
-   stable systems and carefully bounded unstable cases;
-   sparse matrices and graphs with many connections.

Choose matrices with known eigenvalues or compute a reliable reference
from the matrix exponential:

\[ `\mathbf{x}`{=tex}(t)=e\^{At}`\mathbf{x}`{=tex}(0)
+`\int`{=tex}\_0\^t
e\^{A(t-`\tau`{=tex})}B`\mathbf{u}`{=tex}(`\tau`{=tex}),d`\tau`{=tex}.
\]

Start with cases for which the reference can be evaluated reliably. Add
larger cases after correctness is established.

### 5.4 Linear test metrics

For each case, record:

-   state-vector error against the reference at selected samples and at
    the final time;
-   maximum absolute error and RMS error;
-   observed convergence order as the timestep is reduced;
-   qualitative stability versus the eigenvalue spectrum and chosen
    timestep;
-   modal frequency, damping, and phase error for oscillatory cases;
-   long-run drift where a meaningful invariant exists;
-   steps per second, memory usage, and graph-size scaling.

Do not expect every method to remain stable for every timestep. A
stability failure can be a valid method limitation rather than an S7
defect, provided the result matches the method's known stability
properties.

### 5.5 Integration methods to compare

Compare only methods that are implemented or deliberately added, and
document their exact update equations. Candidate methods include:

-   forward Euler / left-rectangle accumulation;
-   backward Euler, if supported;
-   trapezoidal (implicit or an explicitly documented applicable
    realization);
-   Runge--Kutta methods such as RK4, if supported;
-   multistep methods, if supported.

For each method, separate mathematical properties from implementation
properties. Confirm the actual number of state variables, how
intermediate stages are evaluated, and whether a method requires solving
an implicit equation.

### 5.6 Larger-graph and runtime stress tests

Once the smaller reference tests pass, increase state count and graph
connectivity. Measure:

-   throughput as the number of states and operations grows;
-   memory and allocation behavior over long runs;
-   whether evaluation order changes results unexpectedly;
-   whether stateful nodes update exactly once per logical timestep;
-   whether vector/batch processing preserves causal semantics;
-   whether logging and observers materially alter execution results.

Performance tests should not replace numerical-correctness tests. Keep
the two categories separately reportable.

## 6. Stage 2 --- Nonlinear mathematical pendulum

The first nonlinear benchmark should be the undamped mathematical
pendulum:

\[ `\ddot{x}`{=tex}+`\omega`{=tex}\_0\^2`\sin `{=tex}x=0. \]

Here (x) is angular displacement in radians. In first-order form:

\[ `\dot{x}`{=tex}=v,`\qquad`{=tex}
`\dot{v}`{=tex}=-`\omega`{=tex}\_0\^2`\sin `{=tex}x. \]

The continuous system conserves energy:

\[
E=`\frac{1}{2}`{=tex}v\^2+`\omega`{=tex}\_0\^2(1-`\cos `{=tex}x)=`\text{constant}`{=tex}.
\]

This is a strong long-run test because it is nonlinear, has no damping,
and provides a clear invariant. Its periodic solutions are well studied
and can be expressed using elliptic functions.

Test multiple initial amplitudes, including small oscillations and
larger librations that clearly expose nonlinearity. Avoid the separatrix
initially because it introduces a particularly demanding timescale and
deserves a separate test.

Measure:

-   energy error (E_n-E_0) over time;
-   maximum and RMS state error against an analytical or high-accuracy
    reference;
-   period and amplitude error;
-   phase error after many periods;
-   phase portrait and long-term qualitative behavior;
-   dependence on timestep and integration method.

Do not require every integrator to conserve the original energy exactly.
Instead, characterize bounded versus secular energy drift, convergence,
and qualitative behavior. Include enough periods to reveal effects that
are invisible in a short run.

## 7. Stage 3 --- Van der Pol oscillator

Use the van der Pol equation to test nonlinear self-excited oscillation
and a stable limit cycle:

\[ `\ddot{x}`{=tex}-`\mu`{=tex}(1-x\^2)`\dot{x}`{=tex}+x=0. \]

In first-order form:

\[ `\dot{x}`{=tex}=v,`\qquad`{=tex}
`\dot{v}`{=tex}=`\mu`{=tex}(1-x\^2)v-x. \]

Start with a moderate positive (`\mu`{=tex}), then vary it to explore
how the dynamics and numerical difficulty change. Large (`\mu`{=tex})
can create a stiff problem; treat that as a deliberate later stress
case, not the first configuration.

Compare:

-   convergence toward the limit cycle from different initial
    conditions;
-   limit-cycle shape in the phase plane;
-   period and amplitude;
-   transient duration and behavior;
-   sensitivity to timestep and integration method;
-   stability and runtime cost as (`\mu`{=tex}) increases.

Use a high-accuracy independent reference. Do not assume a single
tabulated trajectory is a universal reference: initial conditions,
parameter values, timestep, and transient duration must match.

## 8. Stage 4 --- Lorenz system

Use the Lorenz system as a later test of nonlinear coupling and chaotic
dynamics:

\[ `\dot{x}`{=tex}=`\sigma`{=tex}(y-x), \] \[
`\dot{y}`{=tex}=x(`\rho`{=tex}-z)-y, \] \[
`\dot{z}`{=tex}=xy-`\beta `{=tex}z. \]

Use a documented, conventional parameter set and initial condition, and
record them in the test. Because chaotic trajectories diverge from tiny
perturbations, do not use long-horizon pointwise equality as the primary
criterion.

Compare instead:

-   short-time trajectory error before divergence dominates;
-   boundedness and numerical blow-up behavior;
-   phase-space geometry and attractor projections;
-   statistical distributions and time averages;
-   sensitivity to timestep and precision;
-   consistency across repeated runs of the same configuration.

A reference implementation should use a suitably accurate method and
timestep. Statistical comparisons require sufficiently long samples and
should account for transient removal.

## 9. Stage 5 --- Investigate the dynamics of integrator implementations

After the basic benchmark suite is reliable, explicitly investigate
whether integration methods introduce unwanted numerical dynamics in S7.

For each integrator:

1.  Write down its mathematical update equations.
2.  Identify external state variables, internal stages, and any
    auxiliary state.
3.  For linear input/output cases, derive or numerically verify the
    discrete transfer function where meaningful.
4.  Inspect poles, zeros, stability regions, and any parasitic modes
    relevant to the method.
5.  Run the same models over matched simulation durations and timesteps.
6.  Compare accuracy, stability, phase error, invariant drift, and
    computational cost.
7.  Repeat at multiple timesteps to distinguish a method's structural
    behavior from a poor timestep choice.
8.  Document whether any observed effect is a known property, an S7
    implementation defect, or still unexplained.

Do not equate a method's internal stages with additional poles
automatically. Do not select a method solely by local truncation order.
Long-term behavior and the intended use of S7 as a causal computational
graph are also important.

## 10. Suggested acceptance criteria

Define numeric tolerances per test rather than using one global
tolerance. Acceptance criteria should account for scale, timestep,
reference accuracy, and the known stability limits of each method.

At minimum, a test should establish:

-   the model graph implements the intended equations;
-   state updates follow the intended causal schedule;
-   output agrees with the reference within a documented tolerance in
    the applicable regime;
-   timestep refinement produces the expected convergence behavior;
-   known qualitative properties are reproduced where the method and
    timestep should preserve them;
-   failures provide enough diagnostics to reproduce and investigate
    them.

For nonlinear long-run tests, report both quantitative error and
qualitative behavior. A visually plausible plot alone is insufficient.

## 11. Reporting format

For each benchmark, produce a concise machine-readable or structured
report containing:

-   test identifier and model description;
-   parameters, initial conditions, timestep, duration, and method;
-   S7 commit/build information;
-   reference method and accuracy assumptions;
-   pass/fail status for each metric;
-   maximum and RMS error;
-   convergence results across timesteps;
-   invariant, period, limit-cycle, or statistical metrics as
    appropriate;
-   performance measurements;
-   links to generated data and plots;
-   notes about anomalies and follow-up work.

Keep raw samples separate from summaries so that plots and metrics can
be regenerated.

## 12. Recommended execution order

1.  Establish the headless deterministic test harness.
2.  Verify scalar first-order linear equations.
3.  Verify linear second-order oscillators.
4.  Validate coupled state-space systems of increasing dimension against
    independent references.
5.  Add long-duration and graph-size/performance tests.
6.  Run the nonlinear mathematical pendulum and measure energy drift
    over many periods.
7.  Run van der Pol and validate limit-cycle behavior.
8.  Run Lorenz and compare short-term and statistical/geometric
    properties.
9.  Investigate the numerical dynamics of the integrator implementations
    using evidence from the complete suite.

## 13. Guiding principle

The objective is not to prove in advance that one integration method is
best. It is to build a reproducible experimental basis for understanding
how S7's causal graph, state semantics, and numerical methods behave on
increasingly demanding models.

Prefer small, independently verifiable steps. Inspect the existing
implementation first, reuse established project conventions, and avoid
broad changes until a failing test identifies a concrete problem.
