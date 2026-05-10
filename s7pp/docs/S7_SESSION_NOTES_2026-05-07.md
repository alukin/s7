# S7 Session Notes (2026-05-07)

Purpose: short continuity notes so future sessions can resume quickly.

## 1) Core Architectural Decisions

- S7 models causal discrete-time systems on a directed graph.
- Input pins are taps to upstream output values.
- Dynamics are element-local state vectors (`z^-1` interpretation).
- Stateless direct-feedthrough loops are invalid and must raise schema errors.
- Scheduler now uses a single causal execution order (no split stateless/stateful phases).
- `commit()` is no longer used by scheduler in normal run flow.

## 2) Lifecycle Contract

- Added `Element::calculateParameters()` hook (default no-op).
- Added `Schema::calculateParameters()` and `Schema::prepareExecution()`.
- Run flow:
  1. build/validate execution plan,
  2. precompute element parameters,
  3. execute hot-loop `tick()`.
- `tick()` should avoid heavy parameter recalculation.

## 3) Error Handling / Planning

- Added `SchemaExecutionError` (custom exception).
- `Schema::rebuildExecutionPlan()` throws `SchemaExecutionError` with detailed loop diagnostics:
  - cycle nodes,
  - cycle edges with ports (`from:out -> to:in`).
- Netlist load (`SchemaBuilder::build`) validates plan via `prepareExecution()`.

## 4) Elements Added / Refactored

- Added `FIRFilter` and `IIRFilter`.
- Added `DigitalOscillator` (implemented via `IIRFilter` core, coefficients computed from params).
- Added `EnergyDifference` element:
  - accumulates energetic signal difference
  - `E[k] = E[k-1] + (u1[k]-u2[k])^2 * dt`.
- Added initial state support:
  - `FIRFilter::InitState { x }`
  - `IIRFilter::InitState { x, y }`
- Registry/netlist keys for initial states:
  - FIR: `x0, x1, ...`
  - IIR: `x0..` and `y0..`

## 5) Performance-Oriented Refactor

Moved expensive per-tick calculations into `calculateParameters()` for:
- `Vco`
- `HarmonicOscillator`
- `DigitalOscillator`

`tick()` now focuses on state/input -> output updates.

## 6) Documentation Added

- `S7_CONCEPT.md` (core semantics)
- `S7_ELEMENTS.md` (element library reference)
- `README.md` (docs index)
- `docs/S7_TESTS.md` (test philosophy)

## 7) Test Infrastructure Added

- Added `s7tests` target and CTest integration.
- Current theory-based tests:
  - FIR impulse response
  - First-order IIR step response
  - Harmonic vs digital oscillator agreement (100 periods, 200 samples/period, RMS threshold)

## 8) Observed Numerical Check

Oscillator agreement test produced very small mismatch (expected floating-point effects):
- accumulated energy ~ `2.79e-23`
- RMS error ~ `1.11e-11`

## 9) Near-Term Next Steps

1. Add more DSP/control theory canonical examples (not implementation-only regressions).
2. Add dedicated control examples (closed-loop plant/controller checks).
3. Expand docs with netlist-focused cookbook and mathematical validation cases.
4. Optionally add CI execution of `ctest` for automatic verification.

## 10) Architecture Direction Updated (2026-05-10)

- Added `docs/S7_RUNTIME_ARCHITECTURE.md` to capture distributed-runtime direction.
- Added `docs/S7_CONTROL_PLANE.md` as draft control-plane contract for GUI/node orchestration.
- Added `docs/S7_BAKED_ENGINE.md` as draft data-oriented baked execution engine specification.
- Added `docs/S7_PLUGIN_API.md` as draft shared-library plugin ABI and descriptor contract.
- `v1` scope now targets:
  - separate GUI and runtime,
  - local worker processes,
  - remote computation nodes.
- One authoritative centralized GUI orchestrates active multi-node control.
- Execution direction is hybrid: scalar/sample-step plus future block/frame processing.
- Same-host inter-process data path should prefer shared memory.
- Plugins should load from shared libraries, but ABI is experimental/internal first.
- Placement is manual in the initial implementation.
- Failure policy default should combine `degraded_warning` with `transient_recoverable` retry/reconnect when safe.
- Deterministic sources (for example file-backed replay) should preserve strict reproducibility; live sources are not assumed fully deterministic.
- Graph/session, deployment, and GUI layout should remain separate artifacts.
- Trust model direction is X.509-based authentication.
- Element type IDs should use lower-case dot-separated naming.
- Every computation node should expose a control-plane server endpoint so GUI clients can connect directly, inspect capabilities, and control that node.

