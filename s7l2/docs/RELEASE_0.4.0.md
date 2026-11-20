# S7 0.4.0 — preliminary native compiler notes

**Status: planning (2026-11-20); not implemented or release-qualified.**
See the [roadmap](RELEASE_ROADMAP.md) and
[0.3.0 H8 capture/replay prerequisite](RELEASE_0.3.0_WORKPLAN.md#h8-bridge-qualification).

## Main goal

The **diagram is the source code**. S7 is a design-and-verification tool:
take a supported **local** S7 diagram, validate its causal/numerical
meaning, produce efficient C++ and build a native executable that
computes what the diagram specifies, with minimal resources.
The current dynamic `NodeRuntime` is the reference implementation
used to verify the generated implementation.

## Candidate architecture

```text
.s7diagram -> Schema -> shared validated / flattened / typed / rate-aware
                              causal intermediate representation (IR)
               +-> existing ExecutableGraph / NodeRuntime (reference)
               +-> specialized C++ source/header + CMake recipe
                                    -> native executable or model library
```

Current `Compiler::compile()` builds an `ExecutableGraph`, **not C++**.
Plan the shared IR without duplicating type/rate inference, flattening
or scheduling. Preserve original diagram IDs and source references
for diagnostics and generated-code inspection.

## Proposed qualification gates

| ID | Goal | Evidence |
| --- | --- | --- |
| **N0** | Shared valid IR | One authoritative type/rate/causal/state representation; invalid/unsupported graphs rejected before emission |
| **N1** | Readable specialized C++ | Static data connections and state; no per-step registry lookup, graph maps or virtual element scheduling for supported lowered primitives |
| **N2** | Native products | Reproducible CMake build, a standalone executable and an embeddable processing component; no Qt/Studio dependency on deployed model |
| **N3** | Differential equivalence | Sample-by-sample and state/reset/failure tests against `NodeRuntime`, plus independent numeric oracles; controlled floating-point policy |
| **N4** | Measurable performance | Code size, RSS, per-step cost and throughput versus the same dynamic model on named hardware |
| **N5** | Usable workflow | Proposed `s7c` CLI and Studio export/build entrypoint, clear unsupported-element errors and reproducible build manifest |

Initial compilable subset: F64/C64 arithmetic and constants, gain, delay
and feedback, simple integrators, and flattened nested schemas.
Expand to common FIR/DSP and more complex coupled methods only with
shared verified numerical kernels. Distinguish bitwise-equivalent tests
from tests with justified tolerances; disable uncontrolled fast-math
transformations. Cover initial conditions, simultaneous memory
publication, graph faults and outputs at committed steps.

The generated processing component should expose typed input/output,
initialization/reset and bounded step semantics; optimize out graph
dispatch and per-step allocations wherever possible. It is **not**
a hardware-driver generator. The execution host connects the model
to PortAudio/PipeWire/SoapySDR/GStreamer or recorded input and retains
clock, lifecycle and physical-effect ownership.

**Out of scope for the prototype:** arbitrary plugins without codegen
contracts, distributed graphs, FPGA/HDL, universal ARM/embedded
deployment and production hard-real-time guarantees. Actual
compiled-model ARM sensor deployment is the
[0.6.0 beta goal](RELEASE_0.6.0.md).
