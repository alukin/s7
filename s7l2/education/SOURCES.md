# MIT course and textbook sources — reading and attribution record

Snapshot: **2026-11-18**. The links below are public primary MIT course,
publisher or course-catalog resources. **Verified** here means the page,
syllabus, catalog or the existing S7 reading record supports the *topic
mapping*, **not** that each proposed S7 exercise reproduces a particular
book figure or assigned problem.

S7 is an independent project. This is neither an official MIT exercise
collection nor an MIT endorsement. Use original S7 diagrams, equations,
explanations and independently derived test data.

## Current subjects (2026 teaching/catalog)

| Source | Authoritative public link | Coverage used for proposed labs | Mapping rule |
| --- | --- | --- | --- |
| MIT 6.3000, **Signal Processing**, Fall 2026 | [course home](https://sigproc.mit.edu/fall26), [course information](https://sigproc.mit.edu/fall26/information) | Fourier methods, sinusoids/series, sampling, convolution, DFT, STFT, filtering, noise, image topics | SP labs follow the course's broad topic progression; no guarantee of matching lecture numbers or assignment order |
| MIT 6.3100, **Dynamical System Modeling and Control Design**, Fall 2026 | [course home](https://introcontrol.mit.edu/fall26/), [course information](https://introcontrol.mit.edu/fall26/information), [readings](https://introcontrol.mit.edu/fall26/extras) | Difference/differential models, poles, PID, root locus, lead/lag, state-space, LQR, observers and identification | CT labs use independent simulated plants, not replicas of MIT lab hardware |
| MIT 6.3010, **Signals, Systems and Inference** | [MIT catalog](https://student.mit.edu/catalog/m6c.html), [EECS numbering history](https://eecsis.mit.edu/numbering.html) | Correlation, random processes, PSD, state-space and statistical inference | IN labs are a proposed advanced track; use [historical 6.011 OCW](https://ocw.mit.edu/courses/6-011-signals-systems-and-inference-spring-2018/pages/syllabus/) for public pedagogical context |
| MIT 6.003, **Signals and Systems**, Fall 2011 (historical) | [MIT OpenCourseWare](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/), [readings](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/) | Classical difference equations, operators, feedback, Fourier, Laplace and Z-transform | FN track supplements the contemporary subjects and historical course; historical numbering is not the current 6.3000 syllabus |

The [Fall 2026 lecture/topic crosswalk](LECTURE_ALIGNMENT.md) relates
published weekly calendar headings to S7 lab IDs without claiming that
the MIT courses use S7.

These official websites may be modified after the planning date.
Before citing a **lecture number**, assignment or week in a finished lab,
verify the course edition and exact resource again. Avoid referring to
unpublished 2026 lectures as completed course material.

## Primary textbooks and reading records

### William McC. Siebert — *Circuits, Signals, and Systems*

MIT Press, 1985, paperback ISBN 978-0-262-69095-9;
[publisher](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/),
[DOI](https://doi.org/10.7551/mitpress/1839.001.0001).
This is the central intellectual influence stated in the
[project README](../../README.md).

S7's [existing primary-source review](../docs/INTEGRATOR_REFERENCES.md)
records inspected Chapter 7 (discrete-time signals, linear difference
equations, accumulators) and Chapter 8 (unilateral Z-transform, initial
conditions and pole mappings), including specific printed page numbers.
The reviewed examples/problems provide **candidate** source mappings for
FN01/FN05/FN06 and CT03. Additional chapter/page claims need source
inspection before adding them to a lab. The public teaching links are to
the MIT-hosted [Chapter 7](https://introcontrol.mit.edu/_static/spring25/extras/siebert7.pdf)
and [Chapter 8](https://introcontrol.mit.edu/_static/spring25/extras/siebert8.pdf).
Do not bundle these scans.

### Sanjoy Mahajan and Dennis Freeman — *Discrete-Time Signals and Systems:
An Operator Approach*

MIT 6.003 Fall 2011 [OCW readings](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/).
The [existing S7 source review](../docs/INTEGRATOR_REFERENCES.md)
identifies difference equations (Chapter 1), delay/gain/adder blocks
(Chapter 3), forward/backward Euler and mixed integrators (Chapter 6).
Use its exact operator/initial-condition conventions when claiming a
specific derivation. **Leapfrog**, **Euler–Cromer** and **symplectic
Euler** names must not be treated as automatically interchangeable;
write the exact state-update equations.

### Alan V. Oppenheim, Alan S. Willsky and S. Hamid Nawab —
*Signals and Systems*

Cited in the [project bibliography](../../README.md) and in MIT 6.003
materials. Use this as a **textbook topic reference** for convolution,
frequency response, transform and sampling labs unless an authorized,
exact edition and relevant page have been inspected. It is not presently
evidence that any existing S7 diagram is a literal book-example reproduction.

### Advanced inference context

MIT [6.011 Spring 2018 syllabus](https://ocw.mit.edu/courses/6-011-signals-systems-and-inference-spring-2018/pages/syllabus/)
provides publicly available course-level context for autocorrelation,
spectral-density, state-space and inference. The contemporary 6.3010
catalog identifies continuity of subject area but **does not by itself
authorize exact lecture/problem mappings** to historical 6.011 content.

## Per-lab citation ledger

### SP06 supporting materials — checked 2026-11-19

The new S7 FIR lab links directly to the public
[6.3000 Fall 2026 convolution handout](https://sigproc.mit.edu/_static/fall26/lectures/Unit-Sample_Response_and_Convolution-handout.pdf)
and [frequency-response/filtering handout](https://sigproc.mit.edu/_static/fall26/lectures/Frequency_Response_and_Filtering-handout.pdf),
plus the [6.003 Chapter 3 reading/download page](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap3/).
The handout covers unit-sample response, superposition and discrete convolution;
the OCW resource is the block-diagram/operator reading. These are supporting
teaching links, not a claim to reproduce a named problem or its solution.
They remain on MIT's sites; only S7-original models and explanations ship here.

### FN01–FN03 supporting materials — checked 2026-11-19

The public MIT 6.003 Fall 2011 readings page lists Mahajan and Freeman's
supplementary chapters on [difference equations](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap1/),
[modularity](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap2/)
and [block diagrams/operators](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap3/).
These original reading/download pages were rechecked and are linked from the
new guides and diagram frames. FN01–FN03 are independently authored
first-order recurrence experiments illustrating those topics; no particular
MIT exercise, textbook page or answer key is claimed as reproduced. The
formulas and prediction tables in S7 were derived independently. No MIT
chapter is included in the source or help resources.

### Required ledger fields

Every lab that reaches reference-qualified status must record:

1. MIT course code, term (when applicable) and **stable official URL**;
   distinguish verified topic alignment from a particular assignment.
2. Book author, title, edition, publisher/year, section and page/example
   number **only when physically checked**. If not checked, label the
   source as broad conceptual inspiration.
3. Exact mathematical statement used in the S7 lab, independently
   derived or legitimately quoted minimally, with source conventions
   reconciled (index origin, units, unilateral transforms, initial states).
4. Independent reference derivation/test provenance and a link to the
   committed test, not merely a plotted screenshot.
5. Copyright/license provenance for any datasets, graphics, code
   dependencies and test fixtures. Use synthetic or openly licensed
   material by default; never commit copyrighted chapters or solution
   manuals as repository assets.

Do not use MIT names, logos, branding or statements of endorsement.
Prefer language such as **"S7 original lab illustrating topics taught
in MIT 6.3000"** over **"MIT lab"**. For exact adaptations,
verify legal reuse permission and source fidelity first.

### FN04–FN06 and CT01–CT03 — checked 2026-11-19

The public MIT 6.003 Fall 2011 lecture-note index and its resource/download
pages were checked for direct student links:

- FN04: [lecture 9, Frequency response](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec09/).
- FN05/FN06/CT03: [lecture 7, Discrete approximation of continuous-time systems](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec07/).
- CT01/CT02: [lecture 4, Continuous-time systems](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec04/).

These links establish topic alignment and access to original teaching
materials. No exact slide/problem equivalence is asserted. The six graphs,
experiments and formulas are independently authored for S7. In particular,
FN05/FN06 explicitly derive scalar linear recurrences; they do not claim
that S7 solves arbitrary implicit differential equations. CT02's physical
and discrete references use analytic modes, independently of the runtime.
No MIT PDFs or solutions are redistributed.

### SP01/SP03 — checked 2026-11-19

- SP01: [MIT 6.003 lecture 15, Fourier series](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec15/).
  The public 2011 notes were inspected for harmonic analysis/synthesis and
  orthogonal decomposition (printed slides 3–7). This supports the topic
  mapping. The square-wave sums, finite Parseval check and Gibbs discussion
  here are independently authored; no exact slide/problem reproduction or
  claim that this lecture supplies all those derivations is made.
- SP03: [MIT 6.003 lecture 21, Sampling](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec21/).
  The public notes were inspected for spectral folding, harmonic aliases and
  filtering before sampling (printed slides 35–54 and summary slide 60).
  S7's frequencies, phases and residual experiment are original choices,
  not copied questions or solution data.

Both guides and diagram descriptions link directly to the MIT resource
pages. No external handout, figure or solution key is bundled. Runtime
source samples and the actual viewer path are checked independently; source
alignment alone is not numerical or interface verification.

### SP05/SP07 — checked 2026-11-19

- SP05: [MIT 6.003 lecture 8, Convolution](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec08/).
  The public notes' superposition, time invariance and convolution discussion
  (printed slides 15–22) supports the topic mapping. The signed three-tap
  kernel, two pulses, initial-history contrast and graph are original S7 work.
- SP07: [lecture 9, Frequency response](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec09/)
  and [lecture 11, Continuous-time frequency response and Bode plots](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec11/).
  The public resource pages identify the relevant teaching topics; lecture 11
  notes provide context for continuous frequency response and Bode plots.
  S7's exact forward-Euler state equations, discrete rational transfers,
  rate/damping choices and numerical envelopes are independently derived.

These are topic illustrations, not reproduced MIT questions or solution keys.
No exact textbook-problem equivalence is claimed. Both guides link original
MIT material; no external PDF or figure is bundled. Runtime, observation and
viewer checks remain separate evidence from source attribution.

### SP08/SP09 — checked 2026-11-19

- SP08: [MIT 6.003 lecture 18, Discrete-time Fourier representations](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec18/).
  The public 2011 notes discuss finite DT Fourier representations and FFT
  computation (including printed slides 31–32). S7's exact cosine frequencies,
  interval boundaries and geometric-sum/DFT comparisons are independent work.
- SP09: [MIT 2.161 lecture 12, FFT, leakage and windowing](https://ocw.mit.edu/courses/2-161-signal-processing-continuous-and-discrete-fall-2008/resources/lecture_12/),
  Derek Rowell, Fall 2008. The public notes' leakage and windowing discussion
  (printed 0–6 through 0–9) was inspected, including periodic Hann/Hamming
  expressions and the main-lobe/sidelobe tradeoff. S7 explicitly freezes its
  own finite normalization formulas, units and independently chosen tones.

These are original topic illustrations, not reproductions of named MIT
problems. No external PDF, figure or solution key is bundled. Student links
open original MIT resource pages. The direct DFT and window references share
no production FFT/window implementation; numerical and viewer evidence remain
separate from source attribution.

### SP10 — checked 2026-11-19

[MIT RES.6-008, Digital Signal Processing, lecture 10: Circular Convolution](https://ocw.mit.edu/courses/res-6-008-digital-signal-processing-spring-2011/resources/lecture-10-circular-convolution/),
Alan V. Oppenheim, OCW edition Spring 2011. The inspected official resource
page explicitly identifies finite-sequence circular convolution, aliasing
of the linear result, and implementing linear convolution through circular
convolution. This establishes topic alignment; no exact timestamp, textbook
page or assigned-problem equivalence is asserted. The prerequisite link is
[MIT 6.003 lecture 8, Convolution, Fall 2011](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec08/).

The signed input word, taps, periodic warm-up, primary edit and graph are
original S7 choices. The direct product-deposit and short DFT references
were independently authored to verify the graph and the separate production
frame convolution APIs. No MIT figure, PDF, assignment or solution is bundled.

### SP11 — checked 2026-11-19

[MIT 6.300 Fall 2025, Week 9 Lecture A: Short-Time Fourier Transform](https://sigproc.mit.edu/_static/fall25/lectures/Short-Time_Fourier_Transform.pdf),
PDF pages 3–7, was inspected for windowed-DFT analysis, hop/step size and
time/frequency map orientation. The public
[Spring 2025 Computing Spectrograms page](https://sigproc.mit.edu/spring25/labs/Computing_Spectrograms)
provides additional topic context. No assignment, implementation, tune,
figure or solution is reproduced. S7's gated 32 Hz tone, discrete chirp,
parameters, graph and independent direct-DFT references are original work.
The guide explicitly distinguishes raw-transform teaching conventions from
S7's one-sided peak-amplitude, power and PSD normalization.

## CT04/CT05 — original controller and windup experiments, 2026-11-19

- [MIT 16.06, Fall 2012, Lecture 10: PID Control](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_10/):
  resource/PDF inspected; PDF pages 1–3 introduce P, D and I and discuss
  derivative feedback on measurement. CT04 uses a different original
  first-order plant and an explicitly forward-Euler filtered derivative,
  not its worked problem or ideal derivative realization.
- [MIT 2.017J, Fall 2009, Feedback Control](https://ocw.mit.edu/courses/2-017j-design-of-electromechanical-robotic-systems-fall-2009/MIT2_017JF09_feedback.pdf):
  PDF pages 13–14 (integral/derivative context) and 20 (integrator windup)
  inspected. CT05 illustrates the topic using original bounded PI loops
  and explicit back-calculation; it does not reproduce the lecture's
  illustration or imply that its brief remedy prescribes this recurrence.

Synthetic schedules, new diagrams and long-double scalar reference equations
are original S7 work. No PDF, figure, assignment or solution is bundled.
The current 6.3100 calendar timed out during this batch; no new claim about
its current lecture numbering is made. Topic links above use dated OCW material.

## CT06/CT07 — original sampled loop, 2026-11-19

[MIT 16.06, Fall 2012, Lecture 11](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_11/),
PDF pages 1–3, was inspected for gain-dependent characteristic-root reasoning.
[Lecture 23](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_23/),
PDF pages 1–2, was inspected for gain/phase margin definitions and Bode/Nyquist
interpretation. These continuous-time examples are topic sources, not copied
models: S7's new loop explicitly uses the discrete unit-circle criterion,
exact sampled transfer and original schedules. No handout/figure/assignment
or solution is bundled; formulas and datasets were derived independently.

## CT08 — original discrete lead/lag network, 2026-11-19

[MIT 16.06, Fall 2012, Lecture 25: Lead Compensation](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_25/),
PDF pages 1–2, and [Lecture 14: Lag Compensation](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_14/),
PDF pages 2–3, were inspected for topic alignment. The notes describe
continuous-time pole/zero shaping; CT08 explicitly defines a different
original sampled recurrence, a matched-DC comparison and its control effort.
No worked problem, figure, assignment or solution is copied or bundled.
