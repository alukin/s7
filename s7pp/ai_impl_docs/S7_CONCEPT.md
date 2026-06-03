# S7 Concept: Causal Discrete-Time System Modeling

**Status:** Active  
**Purpose:** Define core modeling semantics and execution model  
**Last Updated:** May 13, 2026

---

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

## 7. The Probe: A Passive Observer

A `Probe` is a special type of sink element designed for observation. It is a fundamental component for connecting the simulation core to the user interface for visualization and measurement.

### 7.1 Core Principles

- **Passive and Non-Invasive**: A `Probe` is a terminal node in the graph. It has input pins but no output pins. It reads data from the signals connected to it but never writes data back into the system. Its presence has no effect on the computational results of the schema.

- **Data Capture**: Its primary role is to capture a history of the signal(s) connected to it. It stores this data in an internal, thread-safe buffer.

- **GUI Connection Point**: The `Probe` acts as the bridge between the `s7core` computation engine and `s7ui` visualization tools. GUI elements, such as virtual oscilloscopes, spectrum analyzers, and meters, attach to a `Probe` to read the captured data for display.

- **Thread Safety**: Probes are designed for safe data exchange between the high-priority simulation thread (writer) and the lower-priority GUI thread (reader). They typically use lock-free or low-contention ring buffers to ensure that real-time visualization does not interfere with the simulation's determinism or performance.

### 7.2 Lifecycle and Usage

1.  A `Probe` is added to the schema like any other element.
2.  Its inputs are connected to the signals that need to be monitored.
3.  During simulation, on each `tick()`, the `Probe` reads the current value from its inputs and stores it in its internal buffer.
4.  A GUI component, running in a separate thread, can then poll the `Probe` at a regular interval (e.g., 30 times per second) to retrieve the latest data and update its display.

By formalizing the `Probe` as a core concept, we ensure a clean separation between the simulation logic and the tools used to observe it.

## 8. Minimal Mental Model

Think of S7 as:
- A causal graph scheduler,
- plus typed signal taps,
- plus explicit element state vectors.

Then every modeled system is just a composition of:
- stateless nonlinear transforms, and
- stateful recursive blocks.

This is the intended foundation for representing general nonlinear discrete-time dynamical systems in S7.

## 9. Future Extensions (compatible with this concept)

- Backward-time stepping for reversible or analyzed systems.
- Optional explicit priority constraints for deterministic ordering when multiple valid topological orders exist.
- Solver-backed blocks for intentionally implicit algebraic subsystems (separate from standard causal mode).
