# Simulation as an analog computer

S7 uses an **analog-computer approach to simulation**. A model is an explicit
network of integrators, arithmetic/function blocks, sources, switches and
other local state elements. Its dynamics emerge from their state transitions
and connections. A schema is itself an element, so a reusable controller or
plant is another graph built from these same elements.

The maintainer reaffirmed this direction on 2026-10-24 and identified William
McC. Siebert's *Circuits, Signals, and Systems* as the book that inspired the
first S7 implementation. This is the maintainer's design-history statement,
not a reconstruction of its unknown numerical algorithm.

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

## Local model time and finite runs

The configured `dt` defines a reduced rational sample rate. Local model time is
computed from the completed step index and that rate, without accumulating a
rounded duration. `ObservationSample`/`ObservationSnapshot` expose seconds as
binary64 and nanoseconds rounded from each absolute timestamp. Nanosecond
rounding is bounded to half a nanosecond per timestamp; it does not accumulate
with the number of steps. Seconds retain fractional-nanosecond intervals within
binary64 precision. Conversion from configured binary64 `dt` to a rational
period first normalizes the binary64 reciprocal round trip so `dt` and
`1/dt` configurations agree, then uses continued fractions within twice binary64
machine epsilon of that normalized period (within four epsilon of configured
`dt`); it does not change the element's numerical integration rule or `dt`.
The supported configured interval is 1 ns through 9e9 seconds. Signed 64-bit
nanosecond timestamp overflow is an explicit error before the next graph step.

Samples taken after `step()` identify successful steps 1 through N, at times
`dt` through `N*dt`. There is no automatic step-zero or extra terminal sample.
Causal-memory elements publish their initial output on step 1 and the update
staged during step 1 on step 2. Thus an Euler decay sampled on step N contains
N-1 published updates. A timestamp identifies the graph step; it does not
silently shift this state-publication contract.

Studio finite runs execute `floor(duration * rate)` whole steps. A duration
that is not a multiple of `dt` stops before the next step would exceed it;
zero duration or `dt` greater than the requested duration executes zero steps.
Only -1 selects continuous execution. Pause consumes no steps or model time;
resume continues the same sequence, and a new run restarts at step 1 with its
own run identity and initial state. Wall-clock waits and capture backpressure
do not advance model time.

These local timing rules do not change deployment wire periods or UTC pacing.
Deployment still uses its explicit integer-nanosecond period contract; extending
that contract to arbitrary rational periods is separate distributed work.

## Accuracy and examples

Document each block's equations, initial conditions, input/output units,
limits and publication timing. Select a discretization rate and signal scaling
that resolve the model's dynamics; test timestep refinement and the supported
parameter range. Distinguish numerical approximation from incorrect causal
ordering, hidden delays and implementation defects.

Higher precision or a smaller timestep alone is not proof of correctness.
Use appropriate independent references and measures: equilibrium, transient
response, phase, amplitude, energy or attractor geometry as appropriate.

## Siebert reference and reading status — 2026-10-24

Reference: William McC. Siebert, *Circuits, Signals, and Systems*, MIT Press;
[publisher record](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/)
and [Google Books contents](https://books.google.com/books/about/Circuits_Signals_and_Systems.html?id=zBTUiIrb2WIC).

The publisher/catalogue and accessible contents have been reviewed. Full chapter
text was not accessible through the checked publisher/preview routes, and the
[Internet Archive edition](https://archive.org/details/circuitssignalss0000sieb_z6i3)
is access-restricted. **The book has not yet been read in full.** A readable copy
is needed before attributing specific numerical or realization techniques to it.

Proposed first reading, based on the table of contents rather than chapter
text already studied:

- Chapter 1, circuit dynamic equations and their solutions (starts p. 1).
- Chapter 5, interconnected systems and feedback (p. 139).
- Chapter 6, feedback dynamics (p. 163).
- Chapter 7, discrete-time signals and difference equations (p. 207).

Use that reading to study explicit circuit/block realizations and separate
continuous-system behavior from the discrete transitions used to approximate
it. The analog-computer and causal-memory contracts above remain the design
boundary; neither locating the book nor the maintainer's tentative recollections
authorize hidden global solver passes. Euler feedback/settling research and the
uncertainty about the historical method are recorded in
[OPEN_PROBLEMS.md](OPEN_PROBLEMS.md).

The maintainer authorized adapting examples from this book and referencing it.
For each adaptation, record the edition, chapter, printed page and example or
problem number after reading the source; distinguish any S7 parameter or
model changes. Express the model as an explicit S7 integrator/function/state
network, document equations, units, initial conditions and the valid numerical
envelope, and check it against an independent reference. Reference the source
in the example's README/diagram notes and the examples catalogue. These are
original S7 realizations of the model; no unread passage or existing example
is to be retroactively attributed to a specific book example by inference.

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
