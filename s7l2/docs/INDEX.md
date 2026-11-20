# S7 Documentation Index (C++ implementation)

S7 is in pre-MVP development. Backward compatibility of APIs, file formats, and protocols is not a goal yet; documents describe the current design direction and say explicitly where the code differs.

The language-neutral documentation (vision, formal model, architecture, terminology, decisions, and the control and wire protocols) is in [`../../doc`](../../doc/README.md). This directory documents the C++ implementation and Studio.

## Start here

[Studio user guide](STUDIO_USER_GUIDE.md) and
[bundled example descriptions](STUDIO_EXAMPLES.md) are also available offline
inside Studio through **Help → S7 Studio Help** (F1).
The [complete examples index](../examples/README.md) links all bundled diagrams
and their experiment guides.

The [0.1.2 preview notes](RELEASE_0.1.2_NOTES.md) summarize the current
release scope, verified checks and known limits. The
[0.1.1 notes](RELEASE_0.1.1_NOTES.md) and [0.1.0 checklist](RELEASE_0.1.0.md)
record the previous releases.

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

The [revised 0.2.0 goals](RELEASE_0.2.0.md) emphasize
independently verified local numerical results, a useful two-host
engineering experiment, predictable distributed faults and
Studio/package qualification. A new
[MIT-aligned S7 education curriculum](../education/README.md)
maps **48 proposed exercises** across foundations, signal processing,
control and advanced inference. Its
[24 named initial 0.2.0 core labs](../education/IMPLEMENTATION_PLAN.md)
are qualification **targets**, not completed tests. See the
[course map](../education/COURSE_MAP.md),
[Fall 2026 lecture crosswalk](../education/LECTURE_ALIGNMENT.md),
[source review](../education/SOURCES.md) and
[lab quality standard](../education/LAB_STANDARD.md).

The [core implementation manifest](../education/CORE_MANIFEST.md) records the
2026-11-19 decision to create new educational diagrams and the remaining gaps.
Begin with [FN01 — one sample of memory](../education/labs/FN01-unit-delay/README.md),
then FN02–FN06 and CT01–CT03 in the [learning route](../education/index.md).
[SP01 — Fourier synthesis](../education/labs/SP01-fourier-synthesis/README.md)
and [SP03 — aliasing](../education/labs/SP03-aliasing/README.md) add harmonic
and sampling experiments, with explicit viewer verification.
[SP06 — three-tap FIR](../education/labs/SP06-three-tap-fir/README.md) applies
the delay concept to filtering. [SP05 — convolution](../education/labs/SP05-convolution/README.md) and
[SP07 — filter transfer](../education/labs/SP07-filter-transfer/README.md) add
shifted responses and independently checked Bode magnitude/phase.
[SP08 — leakage](../education/labs/SP08-dft-leakage/README.md) and
[SP09 — periodic windows](../education/labs/SP09-window-normalization/README.md)
add checked interval selection and amplitude/power/PSD normalization.
[SP10 — circular convolution](../education/labs/SP10-circular-convolution/README.md)
adds explicit padding, startup interpretation and recorded-input verification
of both production convolution processors.
[SP11 — short-time Fourier analysis](../education/labs/SP11-short-time-fourier/README.md)
adds independently checked burst/chirp maps, window/hop controls and edge interpretation.
[CT04/CT05 — controller comparison and windup](../education/labs/CT04-controller-comparison/README.md)
add checked feedback states, actuator limits and saved anti-windup edits.
[CT06/CT07 — sampled poles and margins](../education/labs/CT06-closed-loop-poles/README.md)
connect mode powers with measured Bode/Nyquist responses.
[CT08 — lead/lag compensation](../education/labs/CT08-lead-lag/README.md)
adds a reusable schema controller, matched-DC comparison and control effort.
[SP14 — coherent AM](../education/labs/SP14-coherent-am/README.md)
checks sidebands, phase error, finite-filter states and calibrated spectra.
[Development evidence](../education/EVIDENCE.md)
is separate from release qualification. Description frames link to offline
guides and original MIT online teaching materials.

The [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) remains the single
[status and acceptance matrix](RELEASE_0.2.0_WORKPLAN.md#status-and-acceptance-matrix),
with the delivered N0–N4 research, existing runtime baseline,
owners and remaining qualification tasks. Older remote administration,
certificate GUI, printing, signal generator and production video tasks
moved to the [0.3.0 candidate backlog](RELEASE_0.3.0.md).
Those historical assignments remain in Git history.
Within-node parallel scheduling and its unresolved design choices are recorded
for [0.3.0 planning](RELEASE_0.3.0_RUNTIME_PARALLELISM.md).
The [0.3.0 contract](RELEASE_0.3.0.md) and
[work plan](RELEASE_0.3.0_WORKPLAN.md) mandate 24 SDR and 12 audio
teaching labs, four video checks, **production audio and video**
[GStreamer integration](RELEASE_0.3.0_GSTREAMER.md), plus H8
backend-neutral capture/replay qualification.

The [version roadmap](RELEASE_ROADMAP.md) assigns
[0.4.0 native C++ generation](RELEASE_0.4.0.md),
[0.5.0 distributed coherence](RELEASE_0.5.0.md), and
[0.6.0 pre-GA ARM Linux deployment beta](RELEASE_0.6.0.md).
1.0.0 is a separately qualified GA target, not a scheduled guarantee.

1. `../README.md` — what this implementation contains, how to build and run it.
2. [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) — the 0.1.x local-simulation development record, acceptance criteria and deferred backlog.
3. [OPEN_PROBLEMS.md](OPEN_PROBLEMS.md) — remaining local-simulation gaps and broader open design decisions.
4. [VISUAL_NUMERICAL_TESTS_PLAN.md](VISUAL_NUMERICAL_TESTS_PLAN.md) — the diagram-based numerical-validation methodology (companion to the [language-neutral test plan](../../doc/S7_Numerical_Modeling_Test_Plan.md)). The [0.2.0 research record](RELEASE_0.2.0.md#numerical-integration-and-feedback-settling-research--p1-planned) links the original assignment and delivered reports.

Local simulation quality was the 0.1.x target. The 0.2.0 goals and work plan now
define the next release direction; open problems and the visual numerical plan
track remaining work. Existing tests establish only their stated cases, not
completion of the 0.2.0 release criteria.

Numerical examples and measured integrator comparisons: [small-angle double pendulum](../examples/double_pendulum/README.md) (nine diagrams, continuous/discrete references and reproducible reports), and [nonlinear single pendulum](../examples/pendulum/README.md) (100-second comparisons at 1/10 kHz, energy/phase/convergence and full Scope/CSV fidelity).

[Production coupled integration](COUPLED_INTEGRATION.md) documents F64 Euler
substeps and two-stage Gauss–Legendre, including compiler eligibility,
publication, editable examples, numerical tests and Release cost. Its
[design record](COUPLED_INTEGRATION_0.2.0_DESIGN.md) is separate from the
historical research reports.

The [linear-response laboratory](../examples/linear/README.md) contains editable
RC, RLC and mechanical analogues. [Phase 2 discrete-structure mathematics](NUMERICAL_FEEDBACK_RESEARCH_PHASE2.md),
the [pole-zero research report](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md)
and its [compact numerical summary](NUMERICAL_FEEDBACK_PHASE2_SUMMARY.json)
extend the [Phase 1 feedback report](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md)
without changing production causal semantics.
[Phase 3's integration contract](NUMERICAL_INTEGRATION_PHASE3_SPEC.md),
[real-graph C++ results](NUMERICAL_INTEGRATION_PHASE3_REPORT.md) and
[compact reproducible summary](NUMERICAL_INTEGRATION_PHASE3_SUMMARY.json)
compare coupled Euler substeps with two-stage Gauss–Legendre on the oscillator,
RC and RLC. The Phase 3 runner and its execution policy are research tools;
the production methods above implement the separately reviewed runtime
contract.

Phase-view example: [two-integrator oscillator with Phase Portrait](../examples/2int_osc_phase_portrait.md), with same-step state-pair and plot verification at 1/10/100 kHz. See also the [three-integrator helix with 3D Phase Trajectory](../examples/3int_helix_phase_trajectory.md), verified at 1/10 kHz.

[Local simulation acceptance](LOCAL_SIMULATION_ACCEPTANCE.md) records Release regression, installed-package and measured operational evidence with remaining release gates.

## Implementation architecture

| Document | Content |
|---|---|
| [RUNTIME_PERFORMANCE_ARCHITECTURE.md](RUNTIME_PERFORMANCE_ARCHITECTURE.md) | Indexed serial runtime design and measured results; corrected FFT/FIR experiments and deferred scheduler ideas |
| [RELEASE_0.3.0_RUNTIME_PARALLELISM.md](RELEASE_0.3.0_RUNTIME_PARALLELISM.md) | Established facts, candidate approaches and open decisions for within-node parallel execution |
| [RELEASE_ROADMAP.md](RELEASE_ROADMAP.md) | Hardware, native compilation, coherence, ARM deployment beta, GA |
| [RELEASE_0.3.0.md](RELEASE_0.3.0.md) | Mandatory hardware, SDR/audio/video labs, GStreamer and H8 capture/replay |
| [RELEASE_0.3.0_WORKPLAN.md](RELEASE_0.3.0_WORKPLAN.md) | H0–H8 qualification and work phases |
| [RELEASE_0.4.0.md](RELEASE_0.4.0.md) | Preliminary diagram-to-native-C++ compiler plan |
| [RELEASE_0.5.0.md](RELEASE_0.5.0.md) | Preliminary coherence and distributed sensing plan |
| [RELEASE_0.6.0.md](RELEASE_0.6.0.md) | Preliminary pre-GA ARM Linux deployment beta plan |
| `S7_Transport_Architecture.md` | `s7transport` / `s7runtime` / `s7net` boundaries, security and data-plane baseline |
| [SDR_TRANSPORT_IMPROVEMENT_PLAN.md](SDR_TRANSPORT_IMPROVEMENT_PLAN.md) | Proposed staged high-rate sample transport work: validation, benchmarks, frame/metadata contract, bounded buffers and block execution |
| [SAMPLE_TRANSPORT_BENCHMARK.md](SAMPLE_TRANSPORT_BENCHMARK.md) | Opt-in verified codec, bulk/scalar graph, duplex and paced sample measurements; scope and reproduction commands |
| [S7_Audio_and_Frame_Compression_Research.md](../../doc/S7_Audio_and_Frame_Compression_Research.md) | Audio device clocks/deadlines, compression source/sink candidates and proposed qualification work |
| [FRAME_TRANSPORT.md](FRAME_TRANSPORT.md) | C++ frame source/sink service, bounded ownership, independent LZ4/Zstandard codecs and audio validation |
| [S7_Hardware_IO_Contract.md](../../doc/S7_Hardware_IO_Contract.md) | Accepted hardware I/O direction and initial implementation scope: independent sound, public/private SDR providers, ownership, clocks and faults |
| [HARDWARE_IO.md](HARDWARE_IO.md) | Building and using the sound/SDR SDK, plugins and opt-in device-paced runner; qualification limits |
| [VIDEO_IO_PLAN.md](VIDEO_IO_PLAN.md) | Proposed numeric video source/sink plan: two-host PipeWire webcam/viewing spike, screen capture, Windows Media Foundation and user-tunable compression |
| [VIDEO_IO_SPIKE.md](VIDEO_IO_SPIKE.md) | Opt-in two-host numeric-video driver, scalar/batched/bulk comparison, raw/LZ4/Zstd, PipeWire adapters and bounded receiver-viewer checks |
| [VIDEO_IO_FRAME_DESIGN.md](VIDEO_IO_FRAME_DESIGN.md) | Revised whole-frame video direction: receiver processing/numeric results, owned buffers, native/GPU paths and established media transport candidates |
| [VIDEO_IO_FULL_HD.md](VIDEO_IO_FULL_HD.md) | FFmpeg whole-frame Full HD experiment: native synthetic 1080p/30, upscaled webcam, receiver grayscale and qualification limits |
| [HARDWARE_IO_LIVE_TESTS.md](HARDWARE_IO_LIVE_TESTS.md) | Initial two-host audio, USB input and radio experiments, failure fixes and measured limits |
| [WINDOWS_HARDWARE_RESEARCH.md](WINDOWS_HARDWARE_RESEARCH.md) | Windows hardware feasibility, upstream dependencies, build/packaging gaps and proposed qualification sequence |
| [S7_Frame_Interoperability.md](../../doc/S7_Frame_Interoperability.md) | Compatible Python/Java frame services, codec providers and shared conformance across all three languages |
| [SPLIT_TRANSPORT.md](SPLIT_TRANSPORT.md) | Separate mTLS control and configurable encrypted/trusted-plaintext data listeners, configuration and cross-language contract |
| [CROSS_NODE_CAUSALITY_RESEARCH.md](CROSS_NODE_CAUSALITY_RESEARCH.md) | Literature used before X1 design: dataflow/feedback, bounded scheduling, and MIT/Stanford teaching material, with applicability limits |
| [CROSS_NODE_CAUSALITY_DESIGN.md](CROSS_NODE_CAUSALITY_DESIGN.md) | Opt-in fixed/projected/readiness scheduling spikes, actual-runtime and three-host evidence, credit-progress findings and open decisions |
| [CROSS_NODE_CAUSALITY_PHASE2.md](CROSS_NODE_CAUSALITY_PHASE2.md) | ACK-progress regression/correction, controlled ACK-policy comparison and 51 three-host S7 WebSocket/mTLS cases including reconnect and identity rejection |
| [CROSS_NODE_CAUSALITY_PHASE3.md](CROSS_NODE_CAUSALITY_PHASE3.md) | Derived single-rate research schedules, planning/dispatch scaling and repeated three-host throughput, latency and CPU measurements |
| [CROSS_NODE_CAUSALITY_PHASE4.md](CROSS_NODE_CAUSALITY_PHASE4.md) | Outgoing-first static versus incremental readiness, 184 transport cases, C++ runtime CPU/rollback checks and polling/adapter cost controls |
| [DISTRIBUTED_CAUSAL_SCHEDULE.md](DISTRIBUTED_CAUSAL_SCHEDULE.md) | Implemented static distributed planning, schedule/capability checks, initial limits and real-loop test guide (ADR-027) |

## Developer guides

| Document | Content |
|---|---|
| [SIMULATION_MODEL.md](SIMULATION_MODEL.md) | Analog-computer simulation semantics; independent solvers allowed as test references |
| [CONVOLUTION.md](CONVOLUTION.md) | FIR and FFT complex frame convolution processors, independent/continuous modes and graph-port limit |
| [CONTROL_ELEMENTS.md](CONTROL_ELEMENTS.md) | Local control/measurement primitives and the separate reusable schema resource |
| [TRAPEZOID_INTEGRATION.md](TRAPEZOID_INTEGRATION.md) | Implicit coupled trapezoidal integration, sampled quadrature mode, causality, solver limits and verification |
| [INTEGRATORS.md](INTEGRATORS.md) | Forward/backward Euler, trapezoidal rule, explicit delay realizations, startup, s/z maps and MIT Leapfrog terminology |
| [DIAGRAM_SYMBOLS.md](DIAGRAM_SYMBOLS.md) | MIT-style basic block symbols, coefficients, delay notation and compact numerical-method labels |
| [INTEGRATOR_REFERENCES.md](INTEGRATOR_REFERENCES.md) | Siebert and MIT primary sources, exact sections/pages and visual reading record |
| [INTEGRATOR_REVIEW_2026-11-17.md](INTEGRATOR_REVIEW_2026-11-17.md) | Integrator findings/fixes, independent numerical evidence, GUI help checks and remaining limits |
| [STUDIO_OBSERVATION_VIEWERS.md](STUDIO_OBSERVATION_VIEWERS.md) | Studio's Scope, Phase Portrait and 3D trajectory viewers (Qt Graphs, Qt Quick 3D): read-only viewer interfaces, independent run data/authoritative capture, registry and lifecycle, adding a viewer type, tests and manual visual test plan |
| [STUDIO_SURFACE_AND_CONTROL_ANALYSIS_PLAN.md](STUDIO_SURFACE_AND_CONTROL_ANALYSIS_PLAN.md) | Implementation plan and validation record for surfaces, control-response and time-frequency analysis |
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
| [BUILD_DEPENDENCIES.md](BUILD_DEPENDENCIES.md) | Ubuntu 26.04/26.10 and Fedora 45 dependency installers, options and container qualification |
| [NATIVE_LINUX_PACKAGES.md](NATIVE_LINUX_PACKAGES.md) | Native Fedora RPM and Ubuntu DEB builds, component packages, installed checks and qualification |
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

- The 2026-10-21 Studio visual usability audit was a historical local artifact and is not present in the public tree; the [Studio triage record](STUDIO_0.1.1_TRIAGE.md) and [current window inspection notes](STUDIO_WINDOW_INSPECTION.md) contain checked-in follow-up information.

- [Frame source/sink elements](FRAME_ELEMENTS.md): named channel ports, lossless encoding, Studio configuration and explicit endpoint setup.

- [Surface and analysis testing guide](STUDIO_ANALYSIS_TESTING.md): user workflows, result files, parameter sweeps and optional analysis plugins.
