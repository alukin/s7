# Simulation as an analog computer

S7 uses an **analog-computer approach to simulation**. A model is an explicit
network of integrators, arithmetic/function blocks, sources, switches and
other local state elements. Its dynamics emerge from their state transitions
and connections. A schema is itself an element, so a reusable controller or
plant is another graph built from these same elements.

The functional graph defines causality. Runtime validates and executes that
graph; it does not replace it with a global differential-equation problem.
New simulation features must preserve this model. Do not add graph-wide RK4,
adaptive ODE solvers, automatic event-location solvers, hidden intermediate
graph evaluations or implicit algebraic-loop solving as prerequisites for
educational examples. A need for such machinery is a separate architectural
decision, not an ordinary element implementation.

**RK4 and other independent solvers are allowed in tests.** They can produce
reference trajectories and error estimates outside the production execution
path. Closed-form solutions and independent discrete recurrences are also
useful references. Do not generate both a test result and its reference using
the same S7 execution path.

## Local state and causal memory

A normal element performs one documented transition:

```
(inputs[n], state[n]) -> (outputs[n], state[n+1])
```

Direct-feedthrough elements can use current inputs in current outputs.
Causal-memory elements publish the state staged by the preceding step, and
stage their next outputs from current inputs. Feedback cycles need causal
memory; merely having private state is insufficient. A nested schema remains
direct feedthrough under the current contract; its internal memory does not
automatically become a new causal boundary around an outer cycle.

For example, a controlled integrator has the local rectangle-rule transition:

```
if reset triggers: next = clamp(reset_value)
else if enabled:   next = clamp(state + dt * derivative)
else:              next = state
```

It publishes `next` at the following step. No global solver or scheduler
intervention implements a reset. Two integrators reset by the same signal use
the same published states and stage their changes together.

A sampled contact detector and resets can represent bouncing-ball behavior
at the chosen discretization rate. They do not claim to locate the exact
continuous-time impact between samples. Phase interpolation inside a frequency
meter is a local measurement calculation; it does not change graph execution.

## Accuracy and examples

Document each block's equations, initial conditions, input/output units,
limits and publication timing. Select a discretization rate and signal scaling
that resolve the model's dynamics; test timestep refinement and the supported
parameter range. Distinguish numerical approximation from incorrect causal
ordering, hidden delays and implementation defects.

Higher precision or a smaller timestep alone is not proof of correctness.
Use appropriate independent references and measures: equilibrium, transient
response, phase, amplitude, energy or attractor geometry as appropriate.

## Existing numerical-stage implementation

The existing implicit `TrapezoidIntegrator` includes a coupled numerical stage,
documented in [TRAPEZOID_INTEGRATION.md](TRAPEZOID_INTEGRATION.md). Its graph
reevaluation is an existing architectural exception requiring separate review
against the analog-computer direction. This document does not silently remove
or change that implementation, and it is not a precedent for expanding runtime
solver machinery. The new control primitives and bundled schema elements use
local transitions and ordinary causal-memory integrators.

Related: [element contract](ELEMENT_DEV_GUIDE.md),
[control primitives and schema components](CONTROL_ELEMENTS.md), and the
language-neutral architecture and formal model in `../../doc/`.
