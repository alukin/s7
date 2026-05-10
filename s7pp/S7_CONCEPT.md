# S7 Concept: Causal Discrete-Time System Modeling

S7 means **Simulation 7** ("level 7"), and this document defines the modeling semantics we want the engine to follow.

## 1. Goal

S7 represents a dynamical system in a generalized Z-domain view using a directed graph of connected elements.

At each discrete step `k`:
- Each element reads inputs from connected upstream outputs.
- Each element updates its internal state (if it has one).
- Each element produces output values for downstream consumers.

The key principle is:
- **Dynamics live in element state vectors.**
- **Nonlinearity is stateless mapping unless memory is explicitly modeled in state.**

## 2. Core Modeling Assumptions

1. **Causality**
   - Signals propagate from source outputs to sink inputs through directed edges.
   - Input pins are taps to connected output values.

2. **State Ownership**
   - An element owns its internal state vector `x`.
   - No external object mutates another element's state.

3. **Output Definition**
   - Output is computed from current state and current inputs:
     - `y[k] = g(x[k], u[k])`

4. **State Evolution**
   - State evolves according to the element equation:
     - `x[k+1] = f(x[k], u[k])`

5. **No Hidden Delays**
   - The framework must not introduce accidental extra `z^-1` blocks.
   - Any delay/memory must come from explicit element state dynamics.

## 3. Element Types

## 3.1 Stateless elements

Stateless elements have no internal memory and implement only algebraic mappings.
Examples:
- `Sum<T>`
- `Vca`
- `ToComplex`
- `ToReals`

Form:
- `y[k] = h(u[k])`

## 3.2 Stateful elements

Stateful elements implement explicit recursion.
Examples:
- `Integrator<T>`
- `Vco`
- `HarmonicOscillator` (as sinusoidal source with internal phase/sample state)

Form:
- `x[k+1] = f(x[k], u[k])`
- `y[k] = g(x[k], u[k])`

## 4. Graph and Scheduling Semantics

S7 schema is a directed graph `G = (V, E)`:
- `V` are elements.
- `E` are typed pin connections (`Output -> Input`).

Execution order is defined by causal dependencies:
1. Build dependency graph from connections.
2. Find source-to-sink order (topological order on current-sample dependencies).
3. Execute `tick()` in that deterministic order.

### 4.1 Algebraic loop rule

A loop composed only of direct-feedthrough/stateless dependencies is invalid.
- Such a loop has no causal solution in one sample without solving simultaneous equations.
- In S7 this is a schema error.

### 4.2 Feedback rule (IIR behavior)

Feedback is valid when recursion is represented by explicit stateful dynamics.
- This gives IIR behavior without adding framework-imposed delays.
- The resulting behavior is defined by element equations and execution order.

## 5. Pin Semantics

Pins are typed and strict:
- Real output -> Real input only.
- Complex output -> Complex input only.

Input pins do not store independent signal history.
- They read the connected output's current value.

This keeps signal flow explicit and easy to reason about.

## 6. Practical Engine Policy

For clarity and performance of `tick()`:
- Keep `tick()` logic simple and local to element equations.
- Precompute static coefficients in `calculateParameters()` before run.
- Avoid framework-side hidden buffering of signal values.
- Use assertions for violated internal assumptions in hot paths where appropriate.

## 6.1 Lifecycle contract

The execution lifecycle is:
1. Build execution order from the schema graph (`prepareExecution`).
2. Call `calculateParameters()` for all elements in execution order.
3. Run hot-loop `tick()` calls.

`tick()` should avoid expensive parameter derivations and only use:
- current inputs,
- current state,
- precomputed parameters.

## 7. Minimal Mental Model

Think of S7 as:
- A causal graph scheduler,
- plus typed signal taps,
- plus explicit element state vectors.

Then every modeled system is just a composition of:
- stateless nonlinear transforms, and
- stateful recursive blocks.

This is the intended foundation for representing general nonlinear discrete-time dynamical systems in S7.

## 8. Future Extensions (compatible with this concept)

- Backward-time stepping for reversible or analyzed systems.
- Optional explicit priority constraints for deterministic ordering when multiple valid topological orders exist.
- Solver-backed blocks for intentionally implicit algebraic subsystems (separate from standard causal mode).

