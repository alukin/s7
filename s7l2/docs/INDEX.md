# S7 Documentation Index (C++ implementation)

S7 is in pre-MVP development, approaching the 0.2.0 preview. Backward
compatibility of APIs, file formats and protocols is not a goal yet: saved
diagrams are updated with the format (`tools/update_diagrams.py`). Documents
describe the current design and say explicitly where the code differs.
Completed plans, phase reports and superseded designs are removed from the tree;
Git history keeps them.

The language-neutral documentation (vision, formal model, architecture,
terminology, decisions, control and wire protocols) is in
[`../../doc`](../../doc/README.md). This directory documents the C++
implementation and Studio.

## Start here

- [`../README.md`](../README.md): what this implementation contains, how to build, test and run it.
- [Studio user guide](STUDIO_USER_GUIDE.md) and [bundled example descriptions](STUDIO_EXAMPLES.md), also offline in Studio (**Help → S7 Studio Help**, F1). The [examples index](../examples/README.md) links every bundled diagram and guide.
- [Education curriculum](../education/README.md) and the [learning route](../education/index.md), starting with [FN01 — one sample of memory](../education/labs/FN01-unit-delay/README.md).
- [OPEN_PROBLEMS.md](OPEN_PROBLEMS.md): unsolved problems and open decisions; [SECURITY_TODO.md](SECURITY_TODO.md): deferred security findings.

## Releases and plans

| Document | Content |
|---|---|
| [RELEASE_0.2.0.md](RELEASE_0.2.0.md) | 0.2.0 goals and scope |
| [RELEASE_0.2.0_WORKPLAN.md](RELEASE_0.2.0_WORKPLAN.md) | The single 0.2.0 status and acceptance matrix |
| [STUDIO_PRINTING_0.2.0.md](STUDIO_PRINTING_0.2.0.md) | Mandatory 0.2.0 printing of diagrams and viewer outputs, PDF/printer acceptance matrix |
| [RELEASE_0.1.2_NOTES.md](RELEASE_0.1.2_NOTES.md) | Last tagged preview: scope, checks and limits (Studio's Preview limits page) |
| [RELEASE_ROADMAP.md](RELEASE_ROADMAP.md) | Hardware, native compilation, coherence, ARM deployment beta, GA |
| [RELEASE_0.3.0.md](RELEASE_0.3.0.md), [work plan](RELEASE_0.3.0_WORKPLAN.md), [GStreamer](RELEASE_0.3.0_GSTREAMER.md) | Hardware, SDR/audio/video labs, GStreamer and H8 capture/replay |
| [RELEASE_0.3.0_RUNTIME_PARALLELISM.md](RELEASE_0.3.0_RUNTIME_PARALLELISM.md) | Facts, candidate approaches and open decisions for within-node parallel execution |
| [RELEASE_0.4.0.md](RELEASE_0.4.0.md), [0.5.0](RELEASE_0.5.0.md), [0.6.0](RELEASE_0.6.0.md) | Native C++ generation, distributed coherence, ARM Linux deployment beta |
| [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) | Longer-term backlog (phases 2–5) and the 0.1.x development record |
| [SIMULATION_OBSERVATION_PERFORMANCE_PLAN.md](SIMULATION_OBSERVATION_PERFORMANCE_PLAN.md) | Probe/viewer overhead, runtime copy costs and hardware-boundary work |
| [SDR_TRANSPORT_IMPROVEMENT_PLAN.md](SDR_TRANSPORT_IMPROVEMENT_PLAN.md) | Staged high-rate sample transport work |

## Studio

| Document | Content |
|---|---|
| [STUDIO_USER_GUIDE.md](STUDIO_USER_GUIDE.md) | Using Studio: editing, running, observing, saving |
| [STUDIO_DESIGN_0_2.md](STUDIO_DESIGN_0_2.md) | Studio ownership map and 0.2 interface design |
| [STUDIO_DIAGRAM_EDITOR.md](STUDIO_DIAGRAM_EDITOR.md) | Diagram editor internals, diagram file format, QSchematic notes |
| [STUDIO_DIAGRAM_WIRING_DESIGN.md](STUDIO_DIAGRAM_WIRING_DESIGN.md) | Wiring design: QSchematic subclasses, connection rules, router |
| [STUDIO_OBSERVATION_VIEWERS.md](STUDIO_OBSERVATION_VIEWERS.md) | Scope, Phase Portrait, 3D, Waterfall and Video Player viewers: interfaces, capture, registry, tests |
| [STUDIO_OBSERVERS_DOCK.md](STUDIO_OBSERVERS_DOCK.md) | Observers dock: presentations, selection, run bindings, bounded previews |
| [LIVE_OSCILLOSCOPE_PROTOTYPE.md](LIVE_OSCILLOSCOPE_PROTOTYPE.md), [design](LIVE_OSCILLOSCOPE_DESIGN.md) | Bounded live oscilloscope workflow, retention and limits |
| [STUDIO_SPECTRUM.md](STUDIO_SPECTRUM.md) | Live real/IQ FFT spectrum, windows, amplitude/power/PSD |
| [STUDIO_PAIR_ANALYSIS.md](STUDIO_PAIR_ANALYSIS.md) | Same-step XY, persistence, density and correlation |
| [STUDIO_WINDOW_INSPECTION.md](STUDIO_WINDOW_INSPECTION.md) | A/B cursors, region statistics, frozen-window JSON/CSV |
| [STUDIO_TRIGGERING.md](STUDIO_TRIGGERING.md) | Auto/Normal/Single edge triggering, hysteresis, pre-trigger, holdoff |
| [STUDIO_INSTRUMENT_CONTROLS.md](STUDIO_INSTRUMENT_CONTROLS.md) | Instrument control semantics and the Rigol-style UI backlog |
| [STUDIO_ELEMENT_INFORMATION.md](STUDIO_ELEMENT_INFORMATION.md) | Element documentation sources and supported metadata keys |
| [STUDIO_ANALYSIS_TESTING.md](STUDIO_ANALYSIS_TESTING.md) | Surfaces, control response and time-frequency analysis workflows |
| [SCOPE_RESULTS.md](SCOPE_RESULTS.md) | Saved Scope results, comparison overlays, capture-loss metadata |
| [RECORDING_FORMAT.md](RECORDING_FORMAT.md) | Recording sink/replay and binary layout |
| [STUDIO_GUI_TESTING.md](STUDIO_GUI_TESTING.md) | Automated GUI tests: Xvfb, rendering, skips, artifacts |

## Simulation and numerics

| Document | Content |
|---|---|
| [SIMULATION_MODEL.md](SIMULATION_MODEL.md) | Analog-computer simulation semantics; independent solvers as test references |
| [INTEGRATORS.md](INTEGRATORS.md) | Euler, trapezoid, delay realizations, startup, s/z maps |
| [TRAPEZOID_INTEGRATION.md](TRAPEZOID_INTEGRATION.md) | Implicit coupled trapezoid, sampled mode, solver limits |
| [COUPLED_INTEGRATION.md](COUPLED_INTEGRATION.md), [design record](COUPLED_INTEGRATION_0.2.0_DESIGN.md) | Euler substeps and two-stage Gauss–Legendre |
| [NUMERICAL_VALIDATION_MATRIX.md](NUMERICAL_VALIDATION_MATRIX.md) | Validated configurations, references and tolerances |
| [VISUAL_NUMERICAL_TESTS_PLAN.md](VISUAL_NUMERICAL_TESTS_PLAN.md) | Diagram-based numerical validation method ([language-neutral plan](../../doc/S7_Numerical_Modeling_Test_Plan.md)) |
| [INTEGRATOR_REVIEW_2026-11-17.md](INTEGRATOR_REVIEW_2026-11-17.md) | Integrator findings, evidence and limits cited by the 0.2.0 gates |
| [INTEGRATOR_REFERENCES.md](INTEGRATOR_REFERENCES.md), [DIAGRAM_SYMBOLS.md](DIAGRAM_SYMBOLS.md) | Siebert/MIT sources and block-diagram notation |
| [CONTROL_ELEMENTS.md](CONTROL_ELEMENTS.md), [CONVOLUTION.md](CONVOLUTION.md), [SIGNAL_SOURCES.md](SIGNAL_SOURCES.md) | Control primitives, FIR/FFT convolution, waveform sources |
| [ARRAY_SIGNALS_ZERO_COPY_DESIGN.md](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md) | Vector ports and wires: array descriptors, storage, publication, accepted decisions D1–D7 |
| [RUNTIME_PERFORMANCE_ARCHITECTURE.md](RUNTIME_PERFORMANCE_ARCHITECTURE.md) | Indexed serial runtime and measured results |

## Runtime, transport and distribution

| Document | Content |
|---|---|
| `S7_Transport_Architecture.md` | `s7transport` / `s7runtime` / `s7net` boundaries and data-plane baseline |
| [SPLIT_TRANSPORT.md](SPLIT_TRANSPORT.md) | mTLS control and encrypted/trusted-plaintext data listeners |
| [FRAME_TRANSPORT.md](FRAME_TRANSPORT.md), [FRAME_ELEMENTS.md](FRAME_ELEMENTS.md) | Frame source/sink service, codecs and elements |
| [SAMPLE_TRANSPORT_BENCHMARK.md](SAMPLE_TRANSPORT_BENCHMARK.md) | Sample transport measurements and reproduction |
| [DISTRIBUTED_CAUSAL_SCHEDULE.md](DISTRIBUTED_CAUSAL_SCHEDULE.md) | Static distributed planning and real-loop test guide (ADR-027) |
| [CROSS_NODE_CAUSALITY_RESEARCH.md](CROSS_NODE_CAUSALITY_RESEARCH.md), [phase 4 evidence](CROSS_NODE_CAUSALITY_PHASE4.md) | Literature and the measurements ADR-027 relies on |
| [LOCAL_HEADLESS_NODES_DEMO.md](LOCAL_HEADLESS_NODES_DEMO.md) | Two local headless runtimes discovered in Studio |

## Hardware and video

| Document | Content |
|---|---|
| [HARDWARE_IO.md](HARDWARE_IO.md) | Sound/SDR SDK, plugins and the device-paced runner |
| [HARDWARE_IO_LIVE_TESTS.md](HARDWARE_IO_LIVE_TESTS.md) | Two-host audio, USB and radio experiments and limits |
| [VIDEO_IO_FRAME_DESIGN.md](VIDEO_IO_FRAME_DESIGN.md) | Whole-frame video direction |
| [VIDEO_IO_FULL_HD.md](VIDEO_IO_FULL_HD.md) | Full HD whole-frame baseline measurement |

## Building and extending

| Document | Content |
|---|---|
| [BUILD_DEPENDENCIES.md](BUILD_DEPENDENCIES.md) | Ubuntu and Fedora dependency installers and options |
| [NATIVE_LINUX_PACKAGES.md](NATIVE_LINUX_PACKAGES.md) | Native RPM/DEB builds and checks |
| [WINDOWS_BUILD.md](WINDOWS_BUILD.md) | Cross-building Windows binaries, Wine testing, packaging |
| [ELEMENT_DEV_GUIDE.md](ELEMENT_DEV_GUIDE.md) | Writing elements: contract, metadata, registration |
| [PLUGIN_INTERFACE.md](PLUGIN_INTERFACE.md) | Element plugin ABI, discovery and loading |
| [SCHEMA_LIBRARY_WORKFLOW.md](SCHEMA_LIBRARY_WORKFLOW.md) | Schema-backed element libraries |

Module READMEs: `../s7transport/README.md`, `../s7noise/README.md`,
`../s7spectral/README.md`. Conformance data for other implementations:
`../s7transport/tests/fixtures/README.md` and
`../s7runtime/tests/fixtures/README.md`.

## Source entry points

- `s7core/include/s7/element.hpp`: `Element` base class and port model
- `s7core/include/s7/elements.hpp`: built-in elements
- `s7core/include/s7/element_registry.hpp`, `element_metadata.hpp`: registry and metadata
- `s7core/include/s7/schema.hpp`, `compiler.hpp`, `runtime.hpp`: schema, compiler, `NodeRuntime`
- `s7core/include/s7/schema_library.hpp`: schema library loader
- `s7runtime/src/runtime_service.cpp`: JSON schema loading, node identity, control-plane wiring
- `s7runtime/src/control_plane.cpp`: control-plane message handlers
- `s7transport/src/transport.cpp`: in-memory and WebSocket transport backends
