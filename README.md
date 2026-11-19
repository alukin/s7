# S7 — Signals and systems for learning, research and engineering

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

The project grows out of long practical experience with signal processing,
software-defined radio, distributed sensing and dynamic-system modeling.
Its aim is to make the path from an idea to an experiment visible: express the
model, choose its parameters and initial conditions, run it, and compare its
behavior with theory and measurements.

Models are explicit networks of sources, integrators, gains, summing junctions,
filters, delays and other local computation and state elements. The same
approach supports studying a physical system, testing a controller or exploring
a signal-processing chain. Reusable plants and controllers are graphs
themselves, so their internal assumptions and connections remain inspectable.

Studio provides a visual workbench for drawing these networks and observing
their behavior. The runtime also works headlessly for scripted experiments,
automated checks and execution on other machines.

[![The Lorenz system in S7 Studio: its causal block diagram, a three-dimensional butterfly attractor and a Scope showing the x, y and z state signals.](screenshots/07-lorenz-overview.png)](screenshots/07-lorenz-overview.png)

*Exploring the Lorenz system in S7: an explicit causal model, its
three-dimensional attractor and recorded state signals.*

See [the screenshot tour](screenshots/README.md) for actual runs of a Lorenz
attractor, a controlled buck converter and an AM detector, with their diagrams,
recorded Scope results and Live Oscilloscope views.

## What you can explore

- **Dynamics and feedback:** construct oscillator, nonlinear-system and control
  models from explicit state and function blocks. Inspect initial conditions,
  discretization choices and the effect of changing the model's sample rate.
- **Signals and measurements:** generate and replay signals, build filtering
  chains, and inspect time traces, spectra, correlations and phase trajectories.
- **Sound and radio experiments:** connect computation to PortAudio, native
  PipeWire and SoapySDR through the explicit device-paced runner or SDK. See
  the [hardware I/O guide](s7l2/docs/HARDWARE_IO.md) for supported workflows.
- **Distributed computation:** place parts of a graph on separate nodes and
  exchange typed streams across explicit network boundaries. The
  [causal-schedule guide](s7l2/docs/DISTRIBUTED_CAUSAL_SCHEDULE.md) records the
  implemented single-rate scope and its limits.

The bundled [examples and experiment guides](s7l2/examples/README.md) are a
starting point for changing a model and checking what follows. Numerical guides
state the equations and initialization conventions; tests use independent
recurrences, analytic solutions or reference solvers where appropriate.

## Learn with S7

The current development tree includes **twenty-four original teaching labs**, with
new diagrams designed for students. Each guide starts with a question and
predictions, explains the units and initial state, and gives a small experiment
to change and explain. Description-frame links open offline student guides
and original MIT online teaching materials; web links open in the external browser.

| Labs | What you investigate |
| --- | --- |
| [FN01–FN03](s7l2/education/labs/FN01-unit-delay/README.md) | Unit delay, natural and forced response, and initial conditions |
| [FN04–FN06](s7l2/education/labs/FN04-impulse-transfer/README.md) | Impulse response and transfer functions, integration rules, and stability versus accuracy |
| [CT01–CT03](s7l2/education/labs/CT01-first-order/README.md) | First-order charging, damped motion, and numerical instability caused by a coarse sample interval |
| [CT04/CT05](s7l2/education/labs/CT04-controller-comparison/README.md) | P/PI/PID load rejection, actuator saturation, integral windup and instance-local anti-windup edits |
| [CT06/CT07](s7l2/education/labs/CT06-closed-loop-poles/README.md) | Closed-loop pole motion, sampled Bode/Nyquist response and independently calculated stability margins |
| [CT08](s7l2/education/labs/CT08-lead-lag/README.md) | Reusable lead/lag networks, pole/zero cancellation, matched DC gain and transient control effort |
| [SP01](s7l2/education/labs/SP01-fourier-synthesis/README.md) | Fourier synthesis, harmonic amplitudes, ringing and Parseval’s relation |
| [SP03](s7l2/education/labs/SP03-aliasing/README.md) | Distinct tones with identical sampled values, and how sample rate exposes aliasing |
| [SP05](s7l2/education/labs/SP05-convolution/README.md) | Convolution as a sum of shifted impulse responses, overlap and initial history |
| [SP07](s7l2/education/labs/SP07-filter-transfer/README.md) | Low-pass and resonant filters, measured Bode response and the continuous limit |
| [SP08](s7l2/education/labs/SP08-dft-leakage/README.md) | Finite-record DFT leakage, coherent sampling and interval selection |
| [SP09](s7l2/education/labs/SP09-window-normalization/README.md) | Periodic windows, coherent gain, bandwidth and calibrated spectrum units |
| [SP10](s7l2/education/labs/SP10-circular-convolution/README.md) | Circular wrap-around, zero-padding and independent verification of FIR/FFT convolution |
| [SP11](s7l2/education/labs/SP11-short-time-fourier/README.md) | Bursts and chirps, independently checked STFT coefficients, window/hop settings and time–frequency displays |
| [SP14](s7l2/education/labs/SP14-coherent-am/README.md) | AM sidebands, coherent phase error, finite-filter settling and calibrated recovered spectra |
| [SP06](s7l2/education/labs/SP06-three-tap-fir/README.md) | A three-tap FIR filter built from explicit delays, gains and a summer |

Start with the [student learning route](s7l2/examples/education/README.md).
In a current source build of Studio, choose **File → Example Diagrams… →
education → fn01_unit_delay.s7diagram**. Predict the first samples using its
guide, click **Run** and keep the saved settings, then double-click
**Compare the named signals** to open Scope. Follow the guide's parameter
experiment and explain the changed result.
Studio Help also opens diagram links as working copies. Installed packages
include the offline lab guides beside the examples under `share/s7/`.

These labs also serve as **executable specifications for S7**. Independent
formulas check the saved graphs' samples, initial conditions, timing and
expected failure cases; Studio checks cover opening, editing, saving and
rerunning them. Agreement provides evidence for each declared model and
tested numerical envelope. The [verification record](s7l2/education/EVIDENCE.md)
states the checks, tolerances and remaining qualification.

The [MIT-aligned curriculum](s7l2/education/README.md) plans a broader collection;
these twenty-four labs have passing development checks, while release/package and
platform qualification remain open. They are original S7 exercises with
links to supporting readings, **not an official or endorsed MIT resource**.

## How S7 works

A model is a schema of elements and typed connections. S7 **validates the
schema, compiles an executable graph, executes it and exposes observations**.
Simulation follows the analog-computer approach: dynamics arise from the
connected local blocks and their explicit state transitions.

**Causality comes before clock time.** Dependencies and causal memory determine
which values a computation may use. Machine speed and packet arrival time do
not supply missing model state. Model time, device clocks and execution pacing
have explicit roles; distribution keeps network boundaries visible. Nodes use
X.509 identities and authenticated control over mutual TLS.

Start with the [vision](doc/S7_VISION.md), [formal model](doc/S7_Formal_Model.md)
and [simulation semantics](s7l2/docs/SIMULATION_MODEL.md) for the reasoning behind
these choices.

## Start an experiment

1. Build the [C++ runtime and Studio](s7l2/README.md), or try the
   [0.1.2 preview downloads](https://github.com/alukin/s7/releases/tag/v0.1.2).
   The [release notes](s7l2/docs/RELEASE_0.1.2_NOTES.md) describe those archives;
   current source contains later work.
2. In Studio, choose **File → Example Diagrams…** and open a bundled model.
   Follow its experiment guide, select a rate and duration, and run it locally.
   Studio includes its runtime; a separate service is unnecessary for local runs.
3. Inspect a recorded Scope or open a live observer, then change a parameter,
   initial condition or connection and compare the results.

For scripted work, use the headless runtime and command-line tools. Build on
Linux with the [Fedora and Ubuntu dependency installers](s7l2/docs/BUILD_DEPENDENCIES.md).
[Native RPM/DEB packaging](s7l2/docs/NATIVE_LINUX_PACKAGES.md) and the
[Windows cross-build](s7l2/docs/WINDOWS_BUILD.md) have separate guides.

S7 is in **pre-MVP development**. APIs, file formats and protocols may change.
The [0.2.0 work plan](s7l2/docs/RELEASE_0.2.0_WORKPLAN.md) distinguishes delivered
work, experiments and remaining qualification. For example, the
[Full HD video experiment](s7l2/docs/VIDEO_IO_FULL_HD.md) is research evidence;
production video adapters are still being designed. Open questions are recorded
in [the problem list](s7l2/docs/OPEN_PROBLEMS.md).

## Scientific foundations and thanks

We are grateful to **William McC. Siebert** for the book that inspired S7, and
to the MIT educators who have carried this signals-and-systems teaching
tradition forward. In particular, we thank **Dennis Freeman and Sanjoy Mahajan**
for their explanations of difference equations, operators, feedback and
discrete approximations, and **Alan V. Oppenheim, Alan S. Willsky and
S. Hamid Nawab** for their signals-and-systems textbook. We also thank
**Nancy Lynch** for her teaching on reasoning about distributed systems.

Beyond MIT, the work of **Edward A. Lee, Sanjit A. Seshia, David G. Messerschmitt,
Thomas M. Parks and the Ptolemy researchers**, together with **Leslie Lamport's**
work on causality, informs our study of dataflow, bounded execution and
distributed computation. The original publications and teaching materials are:

- **William McC. Siebert**, *Circuits, Signals, and Systems*. MIT Press, 1985.
  [Publisher and editions](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/)
  (paperback ISBN 978-0-262-69095-9),
  [book DOI](https://doi.org/10.7551/mitpress/1839.001.0001).
  The epigraph excerpt above is on p. v; chapters 7–8 inform our treatment of
  discrete-time block networks and integrator realizations.
- **Sanjoy Mahajan and Dennis Freeman**, *Discrete-time Signals and Systems:
  An Operator Approach* (2009 notes), used in
  [MIT 6.003, Fall 2011](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/).
  The [complete original notes](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/7b3f66736bb503468103ce8a55fe16e0_MIT6_003F11_notes.pdf)
  develop block diagrams and operators in chapter 3 and discrete integration
  through an oscillator in chapter 6.
- **Alan V. Oppenheim, Alan S. Willsky and S. Hamid Nawab**,
  [*Signals and Systems*, second edition](https://www.pearson.com/en-us/subject-catalog/p/signals-and-systems/P200000003155?view=educator).
  Prentice Hall, 1997 (published 1996), ISBN 978-0-13-814757-0.
  This is also the textbook listed for MIT 6.003.
- **Nancy A. Lynch**, [*Distributed Algorithms*](https://groups.csail.mit.edu/tds/distalgs.html).
  Morgan Kaufmann, 1996, ISBN 978-1-55860-348-6. Her
  [MIT 6.852J course](https://ocw.mit.edu/courses/6-852j-distributed-algorithms-fall-2009/pages/lecture-notes/)
  supplied teaching material for our causality and deadlock investigation.
- **Edward A. Lee and Sanjit A. Seshia**,
  [*Introduction to Embedded Systems: A Cyber-Physical Systems Approach*](https://ptolemy.berkeley.edu/books/leeseshia/).
  Second edition, MIT Press, 2017, ISBN 978-0-262-53381-2; especially §6.3
  on dataflow and initialized feedback.
- **Edward A. Lee and David G. Messerschmitt**,
  [“Synchronous Data Flow”](https://ptolemy.berkeley.edu/publications/papers/87/synchdataflow/).
  *Proceedings of the IEEE* 75(9), 1235–1245, 1987,
  [DOI](https://doi.org/10.1109/PROC.1987.13876); and **Thomas M. Parks**,
  [*Bounded Scheduling of Process Networks*](https://ptolemy.berkeley.edu/publications/papers/95/parksThesis/),
  PhD dissertation, UC Berkeley, 1995.
- **Leslie Lamport**,
  [“Time, Clocks, and the Ordering of Events in a Distributed System”](https://lamport.org/pubs/time-clocks.pdf).
  *Communications of the ACM* 21(7), 558–565, 1978,
  [DOI](https://doi.org/10.1145/359545.359563).

The [integrator reading record](s7l2/docs/INTEGRATOR_REFERENCES.md) and
[dataflow/causality research record](s7l2/docs/CROSS_NODE_CAUSALITY_RESEARCH.md)
identify the sections consulted, what we learned from them and the limits of
their applicability to S7. These acknowledgments recognize intellectual
influences, rather than institutional affiliation or endorsement.

We also thank the researchers, engineers and maintainers of the free software
on which S7 depends. Their work makes an inspectable, extensible research tool
possible; [licensing and third-party credits](LICENSING.md) identify the components.

## Repository

| Directory | Content |
|---|---|
| [`doc/`](doc/README.md) | Language-neutral documentation: vision, formal model, architecture, terminology, architectural decisions, and the control and data-stream protocols that every implementation speaks |
| [`s7l2/`](s7l2/README.md) | C++26 reference implementation: core model and compiler, headless runtime `s7runtime`, transport and lossless data streams, `s7ctl` command-line tool, element plugins, and the Qt6 Studio. Builds on Linux, and cross-builds for Windows on Linux |
| [`s7py/`](s7py/README.md) | Minimal headless node in Python (standard library only): the control protocol, deployments over mTLS, data streams with other nodes, and execution of a subset of the built-in elements, F64 only |
| [`s7java/`](s7java/README.md) | Minimal data node in Java (standard library only, Java 21+): the data stream wire format and lossless delivery over mTLS, and its part of a deployed diagram, F64 only. It has no control plane yet: its part comes from a deployed diagram file |

The implementations interoperate through the protocols in `doc/`. Their conformance data lives in `s7l2/s7runtime/tests/fixtures/` (control protocol) and `s7l2/s7transport/tests/fixtures/` (wire format). For example, Studio or `s7ctl` can deploy a diagram to a Python node.

The [documentation index](s7l2/docs/INDEX.md) links the implementation guides,
mathematical help, research reports and release plans. For another language's
node, start with its README and the protocol specifications in `doc/`.

## License

S7 is free software. Studio, `s7ctl`, the tests, tools and `s7py` are licensed under the GNU General Public License, version 3 or later ([`LICENSE`](LICENSE)). The runtime and the plugin SDK (`s7core` and the libraries `s7runtime` is built from) are licensed under the GNU Lesser General Public License, version 3 or later, so element plugins and applications that embed the runtime may use any licence. Documentation is CC-BY-4.0; example models and protocol fixtures are CC0-1.0. Diagrams and results you create with S7 are your own.

Details, including the plugin permission and third-party components: [`LICENSING.md`](LICENSING.md).

## Development with AI tools

Thanks to ChatGPT and Codex (OpenAI), Claude Code (Anthropic), and GitHub Copilot for assistance with design, implementation, testing and documentation. The maintainer directs and reviews this work.

Many thanks to the companies and engineers who build these AI tools: they make development a real pleasure.
