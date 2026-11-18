# MIT Fall 2026 teaching sequence → S7 lab crosswalk

**Source snapshot: 2026-11-18.** These tables are a course-navigation aid,
not an assertion of MIT affiliation or an answer key. The official Fall 2026
calendars say they are **tentative** and may change. Recheck the source
before advertising any fixed lecture, date, handout or homework mapping.
The [curriculum map](COURSE_MAP.md) owns the canonical laboratory IDs;
a week may point to several labs and a lab may serve several weeks.
No row below certifies that the referenced S7 lab is implemented.

## 6.3000 Signal Processing (Fall 2026)

[Official course calendar](https://sigproc.mit.edu/fall26/) and
[course information](https://sigproc.mit.edu/fall26/information).
The site uses the short name **6.300** in its header; the official
subject number is **6.3000**. The mapping tracks the broad lecture
sequence and avoids reusing MIT homework or assessment content.

| Official tentative week | Topic on MIT calendar | S7 lab IDs | Education scope |
| --- | --- | --- | --- |
| 1–2 (Sep 10–17) | Sinusoids; trigonometric and complex Fourier series | SP01, SP02 | SP01 core, SP02 later |
| 3 (Sep 22–24) | Sampling/aliasing; discrete-time Fourier series | SP03, SP04, FN01 | SP03 core; others supporting/later |
| 4 (Sep 29–Oct 1) | Continuous- and discrete-time Fourier transforms | SP02, SP07, FN04 | Topic bridges; frequency response is not the same as a full general CTFT tool |
| 5 (Oct 8) | Systems, linearity and shift invariance | FN01, FN02, SP05 | FN01/FN02/SP05 core |
| 6 (Oct 15) | Unit-sample response, superposition and convolution | SP05, SP06 | Core |
| 7 (Oct 20–22) | Frequency response/filtering; communication systems | SP07, SP14 | Core |
| 8 (Oct 27–29) | Discrete Fourier transform and circular convolution | SP08, SP09, SP10 | Core |
| 9 (Nov 3–5) | Short-time Fourier transform; speech | SP11, SP13 | SP11 core; speech/audio datasets and SP13 later |
| 10 (Nov 12) | FFT | SP08, SP09, SP10 | Existing DFT labs illustrate transform semantics; FFT implementation/performance is a distinct extension |
| 11 (Nov 17–19) | Two-dimensional Fourier transforms and convolution | SP16 | Later; static matrix exercises before live video |
| 12–13 (Nov 24–Dec 3) | Image filtering, DCT/JPEG, inverse filtering, MRI | SP16 | Later; no automatic 0.2.0 video/camera requirement |
| 14 (Dec 8–10) | Synthetic-aperture optics and course review | — | Not currently mapped; do not invent an S7 course match |

**Important:** the course is not a software requirements specification.
An S7 student may explore Fourier response with a diagram even when the
corresponding MIT lecture used mathematical derivation or Python. A
topic alignment is **not** a claim that MIT assigns S7.

## 6.3100 Dynamic System Modeling and Control Design (Fall 2026)

[Official course calendar](https://introcontrol.mit.edu/fall26/) and
[course information](https://introcontrol.mit.edu/fall26/information).
The official website labels itself **preliminary** and lists physical
labs; S7 offers *original simulated analogues*, not replications of
the MIT equipment or student assignments.

| Official tentative week | Topic on MIT calendar | S7 lab IDs | Education scope |
| --- | --- | --- | --- |
| 1 (Sep 9) | Intro to control; thermal example | CT01, FN03 | Core |
| 2 (Sep 14–16) | First-order systems, stability, parameter identification | CT01, CT03, CT09 | CT01/CT03 core; identification later |
| 3 (Sep 21–23) | Second-order systems, matrix powers, complex eigenvalues | CT02, CT10 | CT02 core; state-space extension later |
| 4 (Sep 28–30) | Disturbances, PD/PID properties | CT04, CT05 | Core |
| 5 (Oct 5–7) | Block diagrams and introduction to frequency response | FN01, FN04, CT07 | Core |
| 6 (Oct 13–14) | Operational calculus, transfer functions, complex exponentials | FN04, FN06, CT06, CT07 | Core |
| 7 (Oct 19–21) | Stability and phase margin; PD and lead design | CT06, CT07, CT08 | Core |
| 8 (Oct 26–28) | Lead demonstration, metric-based design and state space | CT08, CT10, CT11 | CT08 core; advanced state-space later |
| 9–10 (Nov 2–11) | Weighted optimization, LQR, LQR transfer functions | CT11, CT12 | Later; independent Riccati reference required |
| 11 (Nov 16–18) | State estimation and Luenberger observers | CT13 | Later |
| 12 (Nov 23) | Optimization of state observers | CT13, IN07 | Later |
| 13 (Nov 30–Dec 2) | Robustness to model errors, output-feedback optimization | CT14 | Later |
| 14 (Dec 7–9) | Reinforcement learning and next steps | — | Not currently mapped; no implied reinforcement-learning subsystem |

## Historical MIT reading bridge

| Source | Topic alignment | Proposed S7 use |
| --- | --- | --- |
| [Siebert, *Circuits, Signals, and Systems* (MIT Press)](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/) | Chapters 7–8 reviewed in [S7 reading record](../docs/INTEGRATOR_REFERENCES.md): accumulators, difference equations, unilateral Z-transform and pole mappings | FN01–FN06, CT03 |
| [Mahajan–Freeman, MIT 6.003 notes](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/) | Operators, state/initial conditions, Euler/backward/mixed integration | FN01–FN06 |
| [Oppenheim–Willsky–Nawab, *Signals and Systems*](../../README.md#scientific-foundations-and-thanks) | Classical convolution, transforms, frequency response and sampling | SP03, SP05–SP10; exact edition/pages to be checked per lab |
| [MIT 6.3010 (current catalog)](https://student.mit.edu/catalog/m6c.html) with [historical 6.011 OCW](https://ocw.mit.edu/courses/6-011-signals-systems-and-inference-spring-2018/) | Random processes, correlation, spectral estimation, Wiener filtering and detection | IN01–IN08, later research/qualification |

## Maintenance and rigor

- Course week/topic rows are checked against public **calendar headings**,
  not inferred from an unverified textbook index.
- Future calendar rows and preliminary course descriptions may change.
  Cite **Fall 2026** explicitly rather than presenting the map as timeless.
- Verify named book problems, editions, printed pages, update equations
  and licensing individually before describing an S7 lab as a reproduction.
- Maintain independent numerical references and the lab quality standards
  in [LAB_STANDARD.md](LAB_STANDARD.md). Do not copy MIT course handouts,
  figures, homework, solutions or restricted datasets into the repository.
- Navigation is by stable **S7 lab IDs**. A future course-term change
  should only update this crosswalk; it should not create duplicate
  underlying model diagrams.
