# S7 Baked Engine Specification (Draft v0)

**Status:** Active (Draft)  
**Purpose:** High-performance baked execution engine design notes  
**Last Updated:** May 13, 2026

---

This document defines the planned high-performance baked execution engine for S7.

Status: draft for later implementation.

## 1. Purpose

S7 currently has an object-oriented runtime that is functionally correct and convenient for modeling.

The baked engine adds a compile-like stage that transforms a validated schema/netlist into a flat, data-oriented execution plan intended for:

- high-throughput simulation on desktop/laptop CPUs,
- low-latency, low-jitter deterministic stepping on embedded targets,
- predictable memory behavior with no hot-loop allocation.

## 2. Non-goals for first implementation

- replacing the OO runtime immediately,
- adding all element types on day one,
- changing existing mathematical semantics,
- forcing distributed runtime/control-plane integration in the first baked prototype.

The first baked executor is a local execution backend with parity checks against the existing runtime.

## 3. Baseline requirements

- identical tick semantics to current S7 runtime,
- no dynamic allocation inside `step()`,
- prevalidated execution plan from bake stage,
- explicit support for `double` and `std::complex<double>`,
- precision-preserving behavior (no `-ffast-math` assumptions).

## 4. Execution semantics contract

The baked engine must preserve the causal semantics already defined for S7.

Per time step `k`:

1. each operation reads inputs from prepared value slots,
2. each stateful operation updates internal state according to element equation,
3. outputs become available in deterministic order defined by the baked plan.

A baked plan is valid only if it is derived from a schema that passes the same cycle/feedthrough validation as the OO scheduler.

## 5. Architecture overview

## 5.1 Two-stage model

- Authoring/runtime model (existing):
  - object graph with typed ports and elements.
- Baked model (new):
  - compact instruction stream plus flat buffers.

## 5.2 Main components

- `Baker`:
  - transforms validated schema into baked intermediate representation (IR).
- `BakedPlan`:
  - immutable instruction/buffer metadata.
- `BakedEngine`:
  - owns mutable value/state buffers and executes `step()`.

## 6. Baked IR

The IR should remain simple, POD-friendly, and portable.

## 6.1 Core records

- `OpCode`:
  - enum of primitive operation kinds (`sum_real`, `iir_real`, `integrator_real`, etc.).
- `Instruction`:
  - operation code,
  - input slot indices,
  - output slot indices,
  - coefficient offsets,
  - state offsets,
  - optional flags.
- `Layer`:
  - contiguous instruction range that is safe to execute with layer-level parallelism.

## 6.2 Indexing

Pointers/references from OO graph are lowered to integer indices (`uint32_t`) into flat buffers.

- Value slots index signal values.
- State slots index element internal states.
- Coefficient tables index immutable parameters.

## 6.3 Determinism fields

`BakedPlan` should store:

- bake hash or revision id,
- schema version,
- stable instruction ordering metadata.

This supports reproducibility checks for deterministic sources.

## 7. Memory model

Use structure-of-arrays style storage.

## 7.1 Value buffers

- contiguous `double` buffer for real slots,
- contiguous `std::complex<double>` buffer for complex slots,
- optional adapter slots for explicit real<->complex conversion ops.

## 7.2 State buffers

- contiguous buffers grouped by operation/state shape,
- indexed by instruction metadata,
- initialized once at engine creation/reset.

## 7.3 Coefficients

- immutable contiguous arrays,
- grouped by operation family,
- loaded during bake/engine construction.

## 7.4 Allocation rule

All required buffers are allocated and sized before stepping begins.

No heap allocations are allowed inside `step()`.

## 8. Bake pipeline

## 8.1 Inputs

- validated schema/netlist,
- element parameter snapshots,
- optional target profile (`desktop_throughput`, `embedded_low_jitter`).

## 8.2 Steps

1. validate graph and execution order preconditions,
2. lower elements to primitive op templates,
3. assign value/state/coefficient slots,
4. generate instruction stream,
5. generate execution layers,
6. emit immutable `BakedPlan`.

## 8.3 Validation outputs

`Baker` should emit diagnostics with actionable detail for unsupported elements or lowering failures.

## 9. Execution modes

## 9.1 Deterministic single-thread mode

Primary first mode for correctness and embedded low-jitter behavior:

- fixed instruction order,
- no task scheduling overhead,
- stable timing profile.

## 9.2 Parallel desktop mode

Optional mode for high throughput:

- execute layer-safe instruction ranges in parallel,
- avoid cross-thread writes to same output/state slots,
- preserve deterministic behavior within allowed floating-point reduction constraints.

## 9.3 Target profiles

The same `BakedPlan` abstraction may support target-specific execution strategy selection at runtime.

## 10. Dispatch strategy

Hot-loop dispatch should avoid virtual calls.

Acceptable choices:

- compact `switch` on `OpCode`,
- static function table indexed by `OpCode`.

Decision can be profile-driven, but interface contract should not depend on one mechanism.

## 11. Precision policy

- arithmetic uses `double` and `std::complex<double>`,
- no implicit precision downgrades,
- explicit conversion operations for type transitions,
- parity comparisons use strict tolerances aligned with existing tests.

## 12. Compatibility with existing S7 runtime

The baked backend is an additional execution path.

- The OO runtime remains source of truth for authoring and early validation.
- The baked path must pass parity tests against the OO path on the same schemas and inputs.

## 13. Suggested first element subset

Start with a minimal but representative set:

- `Constant<Real>`,
- `Sum<Real>`,
- `Integrator<Real>`,
- `FIRFilter`,
- `IIRFilter`.

Then expand incrementally to oscillator/control/media-adjacent elements.

## 14. Testing strategy

## 14.1 Parity tests

For deterministic sources, compare OO vs baked outputs over fixed sample counts.

- absolute/relative tolerance checks,
- RMS error checks,
- state snapshot comparisons where practical.

## 14.2 Allocation tests

Verify no allocations occur during `step()` in test mode (instrumented allocator or counters).

## 14.3 Performance tests

Track at least:

- samples/second,
- step latency distribution,
- jitter (embedded profile),
- scaling by thread count (desktop profile).

## 14.4 Regression corpus

Keep a corpus of representative netlists and expected metrics to prevent performance and semantic regressions.

## 15. API sketch (conceptual)

```cpp
// Existing pathway
SystemModel model = LoadFromJSON("netlist.json");

// New bake stage
BakedPlan plan = Bake(model, BakeOptions{/* target profile */});
BakedEngine engine(plan);

// Hot loop
while (running) {
    engine.step();
}
```

## 16. Integration with broader architecture

The baked engine is execution-core focused and complements, not replaces:

- runtime control plane,
- plugin/catalog discovery,
- distributed node orchestration,
- data-plane transport selection.

In future, plugin-provided elements may optionally expose bake-lowering hooks once plugin API is defined.

## 17. Open design items (deferred)

- final IR binary format stability,
- parallel scheduling backend selection,
- SIMD-specific specialization strategy,
- mixed-precision experiments (currently out of scope),
- plugin-lowered custom operations.

## 18. Immediate next step when implementation starts

Implement a thin vertical slice:

1. bake minimal subset,
2. execute deterministic single-thread mode,
3. prove parity on existing tests,
4. add instrumentation,
5. then introduce parallel mode.

