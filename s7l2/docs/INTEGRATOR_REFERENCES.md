# Integrator references and reading record

These references support the [integrator guide](INTEGRATORS.md). The guide's
derivations, diagrams, code and tests are S7 work. Source equations and S7's
runtime publication/startup conventions are identified separately. Full-text
availability is not a claim that a whole book has been read.

## William McC. Siebert: the foundation of S7

William McC. Siebert, *Circuits, Signals, and Systems*, MIT Press, 1985.
The maintainer identifies this book as the inspiration behind S7: systems
constructed from interconnected local blocks, with explicit state and causality.
[Publisher/DOI and contents](https://doi.org/10.7551/mitpress/1839.001.0001).

Original book reference: MIT Press's
[edition record](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/)
lists the 1985 publication, paperback ISBN 978-0-262-69095-9 and hardcover
ISBN 978-0-262-19229-3. The
[book preview, p. v](https://books.google.com/books?id=zBTUiIrb2WIC&pg=PR5)
is the source of the short English epigraph excerpt in the
[project README](../../README.md), attributed there to Ukrainian philosopher
Hryhorii Skovoroda (Григорій Сковорода). The preview catalogue labels its printing
1986; the publisher dates the original publication 1985. The excerpt preserves
the English wording reproduced in Siebert, rather than supplying a new
translation of a Ukrainian original. Its wording and printed page number were
checked against Google Books' indexed book passage; the complete front matter
was not available for inspection.

Readable chapter scans are hosted by MIT's control course:

- [Chapter 7: Discrete-Time Signals and Linear Difference Equations](https://introcontrol.mit.edu/_static/spring25/extras/siebert7.pdf).
  Example 7.1-2, printed pp. 210–211, contrasts forward Euler with coupled
  backward-Euler equations. Section 7.2, pp. 212–218, develops accumulator,
  delay, gain and adder realizations. Figure 7.2-1, p. 213, specifies the
  forward accumulator's initial value and sample indexing.
- [Chapter 8: The Unilateral Z-Transform and Its Applications](https://introcontrol.mit.edu/_static/spring25/extras/siebert8.pdf).
  Section 8.3, pp. 241–248, relates block diagrams to system functions and
  explains the pole effect of forward-Euler integration. Figure 8.3-7,
  p. 246, includes the initial-state term. **Problem 8.7, p. 255**, gives
  backward-Euler/trapezoid accumulator transforms with initial conditions and
  asks for their s-plane/z-plane stability mappings. Problem 8.8, p. 256,
  introduces step-invariant transformations, a distinct discretization choice.

Reading status, 2026-11-16: the relevant equations and text of these sections
were reviewed; rendered page images were directly inspected for the cited
accumulator, Euler and mapping material. This is a chapter/section review,
not a claim to have read all twenty chapters. Earlier project notes recorded
only catalogue/contents access; these MIT-hosted chapter scans now supply
the missing primary material. No scan is redistributed in S7 packages.

## MIT 6.003 supplementary notes

Sanjoy Mahajan and Dennis Freeman, *Discrete-time Signals and Systems:
An Operator Approach*, source revision `66261db0f9ed+`, 2009-10-18; supplied
by MIT OpenCourseWare for **6.003 Signals and Systems, Fall 2011**.

- [Course readings and attribution](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/).
- [Complete notes PDF](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/7b3f66736bb503468103ce8a55fe16e0_MIT6_003F11_notes.pdf).
  The local project-root copy is named `MIT6_003F11_notes.pdf`.
- Section 1.4, printed pp. 14–16: the spring model and the effect of changing
  its difference approximation.
- Sections 3.2 and 3.5, pp. 35–39 and 45–49: delay/gain/adder block diagrams,
  accumulators and feedback. The operator `R` means a unit delay.
- Chapter 6, pp. 71–82: **Forward Euler**, **Backward Euler**, and **Leapfrog**.
  Section 6.2, pp. 76–79, explicitly eliminates the implicit oscillator
  equations into executable recurrences. Section 6.3, pp. 79–82, uses a mixed
  forward/backward update. Preserve its MIT terminology and written recurrence.

Reading status: the integration, oscillator, delay and feedback material was
studied using both the supplied PDF and rendered page images, not text
extraction alone. The project does not claim that every exercise has been
solved or that the notes' terminology removes the need to state an equation.

## MIT 6.003 Lecture 7

Dennis Freeman and Sanjoy Mahajan, **Discrete Approximation of Continuous-Time
Systems**, Fall 2011:
[MIT lecture resource](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec07/),
[lecture PDF](https://web.mit.edu/6.003/F11/www/handouts/lec07.pdf).

The trapezoidal-rule slides derive `h/2 (z+1)/(z−1)` and the mapping
`z=(1+s h/2)/(1−s h/2)`. Pole-map diagrams compare forward Euler, backward
Euler and the trapezoidal rule. The PDF has incremental/answer slides, so
PDF page indices need not match the slide number printed on the slide.
These derivations and diagrams were visually inspected. The S7 guide
re-derives the formulas and plots original maps rather than copying slides.

## How to use the sources

Start with Siebert's block realizations, then the MIT notes' oscillator example,
then Lecture 7 and Siebert Problem 8.7 for the plane mappings. Check initial
conditions, input timing and delay placement before comparing methods. Use
an independent recurrence or closed-form modal solution to verify the program;
use timestep refinement to assess approximation of the continuous system.

These links open the primary sources in a browser. The original S7 explanation,
figures and reference record are available offline through Studio Help.
