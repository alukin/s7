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
