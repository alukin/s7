# S7 Documentation Index (C++ implementation)

S7 is in pre-MVP development. Backward compatibility of APIs, file formats, and protocols is not a goal yet; documents describe the current design direction and say explicitly where the code differs.

The language-neutral documentation (vision, formal model, architecture, terminology, decisions, and the control and wire protocols) is in [`../../doc`](../../doc/README.md). This directory documents the C++ implementation and Studio.

## Start here

[Studio user guide](STUDIO_USER_GUIDE.md) and
[bundled example descriptions](STUDIO_EXAMPLES.md) are also available offline
inside Studio through **Help → S7 Studio Help** (F1).

The [0.1.1 preview notes](RELEASE_0.1.1_NOTES.md) summarize the current
release scope, verified checks and known limits. The
[0.1.0 checklist](RELEASE_0.1.0.md) records the previous release.

[Studio manual-test findings for a possible 0.1.1](STUDIO_0.1.1_TRIAGE.md)
record the maintenance backlog.
The [0.1.1 interface design and implementation plan](RELEASE_0.1.1.md)
proposes concrete layouts, menu organization, persistence, configuration
ownership, implementation order and acceptance checks for review. Networking
work, including Trusted Networks and node configuration/control/restart, is
assigned to 0.2.0; 0.1.1 focuses on local Studio workflows.
The [main menu review](STUDIO_MENU_REVIEW.md) retains the original inventory.
The [element-information coverage inventory](STUDIO_ELEMENT_INFORMATION.md)
records the documentation sources, supported JSON keys and information access.
The [instrument controls design](STUDIO_INSTRUMENT_CONTROLS.md) records real
hardware/software references, channel selection, precise value semantics,
inspection sliders and progress presentation.
Its [Rigol UI backlog and resolved time/preview defects](STUDIO_INSTRUMENT_CONTROLS.md#next-session-rigol-inspired-live-oscilloscope-ui)
records the deferred control grouping, units/division, knob behavior, graph
markers and overview proposals, with implementation constraints and acceptance.
It records the implemented fixes for `dt`-dependent display width, coarse traces
on magnification, preview navigation, exact time entry and the Signals empty state;
the broader Rigol control redesign remains planned.
The [Observers dock guide](STUDIO_OBSERVERS_DOCK.md) describes reusable
presentations, selection, run bindings, saved metadata and bounded preview work.
Implemented menu, shortcut, toolbar, font, palette, element-information, library
drag-placement, instrument-control and Observers dock increments and their
verification are recorded in the 0.1.1 plan. The simulation guide records
Siebert's book as the maintainer's stated design inspiration, with reading
status and attribution rules for planned example adaptations.

The [0.2.0 goals](RELEASE_0.2.0.md) include indexed serial runtime execution,
P1 numerical integration and feedback-settling research with a published report,
qualified multi-node operation on local and remote hosts, remote node
control/configuration/restart and Trusted Networks settings, certificate viewing
and node certificate creation, Studio printing of diagrams and observation
graphs, and a software signal-generator element inspired by the UNI-T UTG932E.
The [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) records ownership, distributed
architecture and implementation steps, numerical research milestones N0–N4,
PR review gates and the concurrent Studio workstream. Experimental results do
not imply a production integrator or settling algorithm for 0.2.0.
Within-node parallel scheduling and its unresolved design choices are recorded
for [0.3.0 planning](RELEASE_0.3.0_RUNTIME_PARALLELISM.md).

1. `../README.md` — what this implementation contains, how to build and run it.
2. [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) — the 0.1.x local-simulation development record, acceptance criteria and deferred backlog.
3. [OPEN_PROBLEMS.md](OPEN_PROBLEMS.md) — remaining local-simulation gaps and broader open design decisions.
4. [VISUAL_NUMERICAL_TESTS_PLAN.md](VISUAL_NUMERICAL_TESTS_PLAN.md) — the diagram-based numerical-validation methodology (companion to the [language-neutral test plan](../../doc/S7_Numerical_Modeling_Test_Plan.md)). The [0.2.0 research matrix](RELEASE_0.2.0.md#numerical-integration-and-feedback-settling-research--p1-planned) assigns the next experiments.

Local simulation quality was the 0.1.x target. The 0.2.0 goals and work plan now
define the next release direction; open problems and the visual numerical plan
track remaining work. Existing tests establish only their stated cases, not
completion of the 0.2.0 release criteria.

Numerical examples and measured integrator comparisons: [small-angle double pendulum](../examples/double_pendulum/README.md) (nine diagrams, continuous/discrete references and reproducible reports), and [nonlinear single pendulum](../examples/pendulum/README.md) (100-second comparisons at 1/10 kHz, energy/phase/convergence and full Scope/CSV fidelity).

The [linear-response laboratory](../examples/linear/README.md) contains editable
RC, RLC and mechanical analogues. [Phase 2 discrete-structure mathematics](NUMERICAL_FEEDBACK_RESEARCH_PHASE2.md),
the [pole-zero research report](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md)
and its [compact numerical summary](NUMERICAL_FEEDBACK_PHASE2_SUMMARY.json)
extend the [Phase 1 feedback report](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md)
without changing production causal semantics.

Phase-view example: [two-integrator oscillator with Phase Portrait](../examples/2int_osc_phase_portrait.md), with same-step state-pair and plot verification at 1/10/100 kHz. See also the [three-integrator helix with 3D Phase Trajectory](../examples/3int_helix_phase_trajectory.md), verified at 1/10 kHz.

[Local simulation acceptance](LOCAL_SIMULATION_ACCEPTANCE.md) records Release regression, installed-package and measured operational evidence with remaining release gates.

## Implementation architecture

| Document | Content |
|---|---|
| [RUNTIME_PERFORMANCE_ARCHITECTURE.md](RUNTIME_PERFORMANCE_ARCHITECTURE.md) | Indexed serial runtime design and measured results; corrected FFT/FIR experiments and deferred scheduler ideas |
| [RELEASE_0.3.0_RUNTIME_PARALLELISM.md](RELEASE_0.3.0_RUNTIME_PARALLELISM.md) | Established facts, candidate approaches and open decisions for within-node parallel execution |
| `S7_Transport_Architecture.md` | `s7transport` / `s7runtime` / `s7net` boundaries, security and data-plane baseline |
| [SPLIT_TRANSPORT.md](SPLIT_TRANSPORT.md) | Separate mTLS control and configurable encrypted/trusted-plaintext data listeners, configuration and cross-language contract |

## Developer guides

| Document | Content |
|---|---|
| [SIMULATION_MODEL.md](SIMULATION_MODEL.md) | Analog-computer simulation semantics; independent solvers allowed as test references |
| [CONVOLUTION.md](CONVOLUTION.md) | FIR and FFT complex frame convolution processors, independent/continuous modes and graph-port limit |
| [CONTROL_ELEMENTS.md](CONTROL_ELEMENTS.md) | Local control/measurement primitives and the separate reusable schema resource |
| [TRAPEZOID_INTEGRATION.md](TRAPEZOID_INTEGRATION.md) | Implicit coupled trapezoidal integration, sampled quadrature mode, causality, solver limits and verification |
| [STUDIO_OBSERVATION_VIEWERS.md](STUDIO_OBSERVATION_VIEWERS.md) | Studio's Scope, Phase Portrait and 3D trajectory viewers (Qt Graphs, Qt Quick 3D): read-only viewer interfaces, independent run data/authoritative capture, registry and lifecycle, adding a viewer type, tests and manual visual test plan |
| [LIVE_OSCILLOSCOPE_PROTOTYPE.md](LIVE_OSCILLOSCOPE_PROTOTYPE.md) | Bounded Time/Spectrum/XY/Correlation workflow, retention, activity, freeze and remaining limits |
| [STUDIO_SPECTRUM.md](STUDIO_SPECTRUM.md) | Live real/IQ FFT spectrum, windows, amplitude/power/PSD, referenced dB, bounded analysis and numerical verification |
| [STUDIO_PAIR_ANALYSIS.md](STUDIO_PAIR_ANALYSIS.md) | Live same-step XY, persistence, density, linear correlation and inactive-view scheduling |
| [STUDIO_WINDOW_INSPECTION.md](STUDIO_WINDOW_INSPECTION.md) | Shared A/B cursors, raw/derived region statistics, frozen-window JSON/CSV formats and cancellation |
| [STUDIO_TRIGGERING.md](STUDIO_TRIGGERING.md) | Stable periodic Time traces: Auto/Normal/Single edge capture, hysteresis, pre-trigger, holdoff, partial captures and metadata |
| [LIVE_OSCILLOSCOPE_DESIGN.md](LIVE_OSCILLOSCOPE_DESIGN.md) | Proposed bounded live oscilloscope: adjustable acquisition, exact cursor values, time/spectrum/XY/correlation, triggers, ownership and acceptance gates |
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
- `s7core/include/s7/convolution.hpp` — FIR and FFT complex frame processors
- `s7core/include/s7/element_registry.hpp`, `element_metadata.hpp` — registry and metadata
- `s7core/include/s7/schema.hpp`, `compiler.hpp`, `runtime.hpp` — schema, compiler, `NodeRuntime`
- `s7core/include/s7/schema_library.hpp` — schema library loader
- `s7runtime/src/runtime_service.cpp` — JSON schema loading, node identity, control-plane wiring
- `s7runtime/src/control_plane.cpp` — control-plane message handlers
- `s7transport/src/transport.cpp` — in-memory and WebSocket transport backends

- [Automated Studio GUI tests](STUDIO_GUI_TESTING.md): Qt Test/Quick Test workflows, Xvfb, real 3D rendering, skips and failure artifacts.

- [Numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md): currently validated linear configurations, independent references, tolerances and bundled-example classification; distinct from the planned 0.2.0 research matrix.

- [Studio visual usability audit (2026-10-21)](usability/2026-10-21/AUDIT.md): production screenshots, severity-ranked findings, incremental improvements and human-review limits.
