# S7 Documentation Index (C++ implementation)

S7 is in pre-MVP development. Backward compatibility of APIs, file formats, and protocols is not a goal yet; documents describe the current design direction and say explicitly where the code differs.

The language-neutral documentation (vision, formal model, architecture, terminology, decisions, and the control and wire protocols) is in [`../../doc`](../../doc/README.md). This directory documents the C++ implementation and Studio.

## Start here

1. `../README.md` — what this implementation contains, how to build and run it.
2. [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) — the next release: dependable Studio simulation with the bundled runtime, acceptance criteria, and deferred backlog.
3. [OPEN_PROBLEMS.md](OPEN_PROBLEMS.md) — remaining local-simulation gaps and broader open design decisions.
4. `VISUAL_NUMERICAL_TESTS_PLAN.md` — the diagram-based, visualized numerical-validation test track (companion to `../../doc/S7_Numerical_Modeling_Test_Plan.md`).

The next-release target is local simulation quality. The development plan defines intended outcomes; the editor guide describes implemented behavior; open problems and the visual numerical plan track remaining work. Existing tests establish only their stated cases, not completion of the release criteria.

Numerical examples and measured integrator comparisons: [small-angle double pendulum](../examples/double_pendulum/README.md) (nine diagrams, continuous/discrete references and reproducible reports), and [nonlinear single pendulum](../examples/pendulum/README.md) (100-second comparisons at 1/10 kHz, energy/phase/convergence and full Scope/CSV fidelity).

Phase-view example: [two-integrator oscillator with Phase Portrait](../examples/2int_osc_phase_portrait.md), with same-step state-pair and plot verification at 1/10/100 kHz. See also the [three-integrator helix with 3D Phase Trajectory](../examples/3int_helix_phase_trajectory.md), verified at 1/10 kHz.

## Implementation architecture

| Document | Content |
|---|---|
| `S7_Transport_Architecture.md` | `s7transport` / `s7runtime` / `s7net` boundaries, security and data-plane baseline |

## Developer guides

| Document | Content |
|---|---|
| [TRAPEZOID_INTEGRATION.md](TRAPEZOID_INTEGRATION.md) | Implicit coupled trapezoidal integration, sampled quadrature mode, causality, solver limits and verification |
| [STUDIO_OBSERVATION_VIEWERS.md](STUDIO_OBSERVATION_VIEWERS.md) | Studio's Scope, Phase Portrait and 3D trajectory viewers (Qt Graphs, Qt Quick 3D): viewer registry and lifecycle, data flow, adding a viewer type, tests and manual visual test plan |
| [SCOPE_RESULTS.md](SCOPE_RESULTS.md) | Save/open Scope results, persistent comparison overlays, capture-loss metadata and file format |
| [RECORDING_FORMAT.md](RECORDING_FORMAT.md) | Recording sink/replay, compact binary byte layout, text headers, rates, descriptions and lifecycle |
| [SIGNAL_SOURCES.md](SIGNAL_SOURCES.md) | Waveform sources, chirps, PRBS excitation and recorded replay: parameters, timing and numerical limits |
| `ELEMENT_DEV_GUIDE.md` | Writing elements: element contract, metadata, registration, parameters |
| `PLUGIN_INTERFACE.md` | C ABI for element plugins, discovery paths, loading process |
| `SCHEMA_LIBRARY_WORKFLOW.md` | Schema-backed element libraries in Studio and the runtime |
| `STUDIO_DIAGRAM_EDITOR.md` | Current local simulation/Scope behavior, diagram editor internals, diagram file format, QSchematic notes |
| `STUDIO_DIAGRAM_WIRING_DESIGN.md` | Design record of the diagram wiring: QSchematic subclasses, format v2, connection rules, router (implemented) |
| `LOCAL_HEADLESS_NODES_DEMO.md` | Run two local headless runtimes and discover them in Studio |
| `WINDOWS_BUILD.md` | Cross-building Windows binaries on Linux (Clang, MSYS2 sysroot), testing under Wine, packaging |

Module READMEs: `../s7transport/README.md`, `../s7noise/README.md`, `../s7spectral/README.md`.

Conformance data for other implementations: `../s7transport/tests/fixtures/README.md` (wire format and stream descriptors) and `../s7runtime/tests/fixtures/README.md` (control protocol).

## Source entry points

- `s7core/include/s7/element.hpp` — `Element` base class and port model
- `s7core/include/s7/elements.hpp` — built-in elements
- `s7core/include/s7/element_registry.hpp`, `element_metadata.hpp` — registry and metadata
- `s7core/include/s7/schema.hpp`, `compiler.hpp`, `runtime.hpp` — schema, compiler, `NodeRuntime`
- `s7core/include/s7/schema_library.hpp` — schema library loader
- `s7runtime/src/runtime_service.cpp` — JSON schema loading, node identity, control-plane wiring
- `s7runtime/src/control_plane_mvp.cpp` — control-plane message handlers
- `s7transport/src/transport.cpp` — in-memory and WebSocket transport backends
