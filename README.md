# S7 — Signals and systems for engineering, research and learning

> “… everything simple is true and everything complicated is untrue.”
>
> — Ukrainian philosopher **Hryhorii Skovoroda** (Григорій Сковорода),
> quoted in William McC. Siebert's *Circuits, Signals, and Systems*,
> [p. v](https://books.google.com/books?id=zBTUiIrb2WIC&pg=PR5) (excerpt).

S7 is inspired first and foremost by **MIT Professor William McC. Siebert's
[*Circuits, Signals, and Systems*](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/)**:
understanding a system through its signals, interconnected blocks, state and
feedback. S7 turns that way of thinking into an open-source tool for
**students, researchers, scientists and engineers** to build models, run
experiments and inspect their results.

S7 is an engineering workbench for signal processing, dynamic systems,
control, and distributed causal computation. Build a system as a diagram
of connected elements, make its state and dependencies explicit, execute
the model, and check its behavior against theory or measurements.

**The diagram is the specification.** Today, S7 validates diagrams and runs
them through a dynamic reference runtime. The [roadmap](s7l2/docs/RELEASE_ROADMAP.md)
extends that workflow toward native C++ generation and deployment on actual
signal-processing hardware. Those are goals, **not features of the current
release**.

[**Explore a teaching lab**](s7l2/education/index.md) ·
[**Build and run S7**](s7l2/README.md#build) ·
[**View Studio screenshots**](screenshots/README.md) ·
[**Read the roadmap**](s7l2/docs/RELEASE_ROADMAP.md)

[![S7 Studio running a Lorenz attractor: causal diagram, recorded signal views, and a 3D trajectory.](screenshots/07-lorenz-overview.png)](screenshots/07-lorenz-overview.png)

*An S7 Studio experiment: a saved causal diagram, recorded signals, and
a three-dimensional Lorenz attractor. More examples in the
[screenshot tour](screenshots/README.md), including a buck converter
and AM detector.*

> **Project status:** S7 is in **pre-MVP development**. The
> [v0.1.2 preview](https://github.com/alukin/s7/releases/tag/v0.1.2)
> is an earlier tagged build; the development tree contains newer work.
> APIs, saved formats, and protocols can change. The 0.2.0 acceptance
> gates are not yet closed; see the [live work plan](s7l2/docs/RELEASE_0.2.0_WORKPLAN.md).

## Why S7?

Signal-processing and control systems are often explained with block
diagrams, then implemented with a different set of programming abstractions.
That gap makes assumptions about feedback, timing, state, and numerical
methods difficult to inspect.

S7 is an attempt to keep the **engineering model itself executable and
inspectable**:

1. **Describe** a system with typed connections, reusable blocks, explicit
   state, and feedback.
2. **Validate** the graph's connections and causal dependencies before it
   runs.
3. **Execute** the saved model locally, headlessly, or—within the documented
   supported subset—across networked nodes.
4. **Observe and verify** traces, spectra, phase trajectories, or numerical
   results, using independent reference calculations where appropriate.
5. **Reuse the specification** for further experiments and, on the roadmap,
   generate a specialized native implementation from the same diagram.

The aim is not simply to draw signal-flow graphs. It is to connect
**design, experiment, evidence, and implementation** without losing the
meaning of the original model.

## What you can do today

| Area | Available in the development tree |
| --- | --- |
| **Visual modeling** | Draw and edit causal block diagrams in Qt6 Studio, use nested schemas and element libraries, and run local models with Studio's embedded runtime. |
| **Simulation and analysis** | Model dynamics, integrators, oscillators, filters, nonlinear systems, and feedback; inspect recorded Scope traces, live oscilloscope views, phase portraits, and 3D trajectories. |
| **Learning and numerical verification** | Explore 24 original, MIT-aligned core labs with saved diagrams, guides, and independent numerical checks. Their development tests exist; full release and platform qualification remain open. |
| **Headless and distributed execution** | Run the C++ runtime without Studio. Deploy a supported static, single-rate causal graph across nodes with explicit network streams and authenticated control. Python and Java implement documented, different subsets of node and protocol functionality. |
| **Physical signal I/O experiments** | Connect selected audio and SDR sources/sinks via PortAudio, native PipeWire, and SoapySDR. The current device-paced runner has [specific ownership, rate, and boundary limits](s7l2/docs/HARDWARE_IO.md); this is not yet a general low-latency media processing product. |

See the [C++ implementation guide](s7l2/README.md) for exact features,
build profiles and known limitations. The
[distributed causal scheduling contract](s7l2/docs/DISTRIBUTED_CAUSAL_SCHEDULE.md)
and [hardware I/O guide](s7l2/docs/HARDWARE_IO.md) define the supported
execution envelopes.

## Try a first experiment

The easiest entry point is the original teaching laboratory
[**FN01 — One sample of memory**](s7l2/education/labs/FN01-unit-delay/README.md).
It illustrates a fundamental S7 idea: feedback is meaningful when the model
specifies where information from a previous step is retained.

1. Follow the [build instructions](s7l2/README.md#build) or inspect the
   [v0.1.2 preview downloads](https://github.com/alukin/s7/releases/tag/v0.1.2)
   (the preview predates some current development-tree labs).
2. In a current source build of Studio, open **File → Open Labs…** or
   **Help → Education: start here**, then select the FN01 diagram.
3. Predict the output samples, run the diagram, inspect the signals in Scope,
   and change a parameter. Compare the new trace with your prediction.

The full [learning route](s7l2/education/index.md) covers signals,
convolution, Fourier analysis, numerical integration, filters, and control.
The labs are original S7 teaching material aligned with topics in MIT
courses—not official MIT coursework or an MIT-endorsed project. Each lab
is also an opportunity to test S7's numerical semantics, rather than
merely demonstrate a picture.

For a larger system, explore the
[example diagrams and experiment guides](s7l2/examples/README.md).
The [educational verification record](s7l2/education/EVIDENCE.md)
distinguishes passing development checks from release qualification.

## The execution model

An S7 diagram is a schema of **elements, typed ports, connections, and
element-owned state**. Validation checks structure and causality; the
current compiler builds an executable graph, which the runtime steps in
causal order. In particular, cycles require explicit causal memory.
Observers read results without becoming part of the computation.

Three principles guide the design:

- **Causality before scheduling.** Dependencies and state determine which
  values a computation may use. Thread execution order and packet arrival
  do not define the model's meaning.
- **Time and clocks are explicit.** Model steps, device pacing, acquisition
  timestamps, and transport latency are different concepts. More general
  multi-clock coherence is future work.
- **The core is headless.** Studio is a design and observation tool, not a
  prerequisite for an executing model. Network boundaries and control
  responsibilities remain visible.

The architectural detail lives in the
[vision](doc/S7_VISION.md),
[formal model](doc/S7_Formal_Model.md), and
[simulation semantics](s7l2/docs/SIMULATION_MODEL.md).
These principles are design constraints, not a claim of universal
determinism, crash-durable delivery, or hard real-time execution.

## Where S7 is going

The roadmap is intentionally staged: **first trust the model, then
connect it to physical signals, compile it, and deploy it**. A planned
feature is not a released or qualified capability.

| Milestone | Intended result |
| --- | --- |
| [**0.2.0 — Verify the workbench**](s7l2/docs/RELEASE_0.2.0_WORKPLAN.md) | Reproducible numerical and causal execution; 24 core educational labs; an evidence-backed two-host scenario; reliable Studio and **diagram/viewer printing** to PDF and physical printers; qualified packages. |
| [**0.3.0 — Bridge to hardware**](s7l2/docs/RELEASE_0.3.0_WORKPLAN.md) | 24 SDR labs, 12 audio labs and four preliminary video checks; device timing and low-latency experiments; **GStreamer audio/video** integration; separation of device, model and host with deterministic capture/replay. |
| [**0.4.0 — Compile the diagram**](s7l2/docs/RELEASE_0.4.0.md) | Generate efficient, readable native C++ from an explicitly supported local model subset and compare its results with the dynamic reference runtime. |
| [**0.5.0 — Distributed coherence**](s7l2/docs/RELEASE_0.5.0.md) | Relate physically acquired signals across independent clocks, with explicit drift, alignment and uncertainty; validate distributed acoustic and SDR sensing experiments. |
| [**0.6.0 — Real-device beta**](s7l2/docs/RELEASE_0.6.0.md) | Compile and deploy a verified DSP model to **ARM Linux**, with real microphone or SDR input; measure latency, resources, data loss and recovery on named hardware. This is **pre-GA beta**, not 1.0. |
| **1.0.0 — General Availability** | A separate release decision based on supported platforms, compatibility commitments, reproducible builds, and documented reliability, security and performance evidence. |

The [complete roadmap](s7l2/docs/RELEASE_ROADMAP.md) explains the
dependencies between these goals and their acceptance criteria.
For example, the current diagram compiler builds an *executable graph*;
it does **not** generate C++ source code. Native compilation belongs to
the planned 0.4.0 milestone. Likewise, the current audio/SDR adapters
must not be confused with the broader hardware and GStreamer goals of
0.3.0.

## Repository and documentation

| Path | What lives there |
| --- | --- |
| [**`doc/`**](doc/README.md) | Language-neutral vision, formal model, architecture, decisions, and common protocols. |
| [**`s7l2/`**](s7l2/README.md) | C++ reference implementation, graph compiler/runtime, transport, device integrations, command-line tools and Qt6 Studio. |
| [**`s7py/`**](s7py/README.md) | Python headless node with a supported subset of execution and control/data interoperability. |
| [**`s7java/`**](s7java/README.md) | Java data node and protocol implementation; its control-plane support is not yet equivalent to the C++/Python nodes. |

Start with the [documentation index](s7l2/docs/INDEX.md) for build,
runtime, numerical, Studio, packaging and research guides. Open design
problems are tracked in the [problem list](s7l2/docs/OPEN_PROBLEMS.md).

S7 is developed with help from AI coding tools; the maintainer directs
and reviews the work. Contributors and coding agents should read
[AGENTS.md](AGENTS.md) and the applicable component documentation,
especially before modifying causal semantics or a protocol.

## Origins and scientific context

**S7 began in 1993.** **Oleksiy Lukin** implemented its first version in
**Turbo Pascal 5.5**. **Sehii Kondratiuk** subsequently added a graphical
user interface. The present S7 project carries that early diagram-based
engineering idea forward with modern runtimes, numerical verification, and
distributed computation.

That early S7 history and Siebert's book are part of the project's
identity, not merely background references. The epigraph at the top
expresses the goal of making complex behavior understandable through
simple, explicit connections.

S7 also draws on work in signals and systems, dataflow, formal modeling,
and distributed computation: among others, **Dennis Freeman, Sanjoy
Mahajan, Alan Oppenheim, Alan Willsky, S. Hamid Nawab, Edward A. Lee,
Sanjit Seshia, Nancy Lynch, Leslie Lamport**, and the Ptolemy research
community. See the
[integrator reference record](s7l2/docs/INTEGRATOR_REFERENCES.md)
and [dataflow/causality research record](s7l2/docs/CROSS_NODE_CAUSALITY_RESEARCH.md)
for the publications and their relevance to S7.

These are intellectual influences, not institutional affiliations or
endorsements. We also acknowledge the maintainers of the free software
and scientific tools on which S7 depends.

## License

S7 is free software. The runtime and plugin SDK are generally
**LGPL-3.0-or-later**, while Studio, command-line tools and most other
code are **GPL-3.0-or-later** (with applicable plugin permissions).
Documentation is **CC-BY-4.0** and example models are **CC0-1.0**.
Your own diagrams and results remain yours.

The authoritative component-by-component terms and third-party credits
are in [LICENSING.md](LICENSING.md).
