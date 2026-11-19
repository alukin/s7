# S7 teaching laboratories — MIT-aligned curriculum

**Status: curriculum and verification plan, not a shipped course bundle.**
Planning baseline: 2026-11-18. The [course map](COURSE_MAP.md) contains
**48 proposed lab exercises**, not 48 implemented or qualified diagrams.
Each delivered lab needs a new educational diagram; existing examples
provide engineering references. Some topics also need new capabilities. Never advertise an exercise as complete simply
because its prerequisite S7 example runs.

**Maintainer decision, 2026-11-19:** create new diagrams specifically for
the education collection, under `examples/education/`. Existing examples
and tests are implementation references, not the diagrams delivered as labs.
Each educational diagram remains canonical for its lab and may be linked
from several course routes. See the [core implementation manifest](CORE_MANIFEST.md).

New students can now start with [FN01 — one sample of memory](labs/FN01-unit-delay/README.md),
then [FN02 — natural and forced response](labs/FN02-natural-forced/README.md)
and [FN03 — initial conditions](labs/FN03-initial-conditions/README.md).
These new diagrams save a short 20-sample run and link directly to their
student guides and MIT readings. See the [learning route](../examples/education/README.md).

Continue with [FN04 — impulse and transfer](labs/FN04-impulse-transfer/README.md),
[FN05 — integration rules](labs/FN05-integration-rules/README.md),
[FN06 — pole maps](labs/FN06-pole-maps/README.md), then
[CT01 — first-order charging](labs/CT01-first-order/README.md),
[CT02 — damped motion](labs/CT02-damped-modes/README.md) and
[CT03 — sample interval](labs/CT03-sample-time/README.md). These six new lessons
include independent formulas, deliberate failure cases and Studio workflows.

New signal-processing lessons now include
[SP01 — Fourier synthesis](labs/SP01-fourier-synthesis/README.md) and
[SP03 — sampling and aliasing](labs/SP03-aliasing/README.md). They include
explicit spectrum settings and checks of the displayed samples, axes,
selected intervals and amplitude/power/PSD against independent references.

Continue with [SP05 — shifted impulse responses](labs/SP05-convolution/README.md)
and [SP07 — filter transfer functions](labs/SP07-filter-transfer/README.md).
They check convolution startup, superposition, measured complex transfer,
Bode magnitude/phase and the distinction between discrete and continuous models.

[SP08 — DFT leakage](labs/SP08-dft-leakage/README.md) and
[SP09 — periodic window normalization](labs/SP09-window-normalization/README.md)
add independently checked interval selection, complex DFTs, coherent gain,
ENBW and amplitude/power/PSD displays, including dB and mean-removal behavior.

[SP10 — circular versus linear convolution](labs/SP10-circular-convolution/README.md)
shows tail wrap-around and sufficient zero-padding using explicit scalar
networks. Its recorded input also verifies the production FIR/FFT frame
processors against independent sums, including streaming boundaries and tails.

[SP11 — short-time Fourier analysis](labs/SP11-short-time-fourier/README.md)
uses a new burst/chirp graph to verify complex STFT coefficients, window and
hop coordinates, power normalization and the actual map/slice displays.

[CT04/CT05](labs/CT04-controller-comparison/README.md) add original controller
and windup comparisons with independent discrete-state checks, separate
applied/unlimited/state plots and an instance-local nested edit.

[CT06/CT07](labs/CT06-closed-loop-poles/README.md) connect closed-loop
unit-circle stability with independent rational frequency responses and
actual Bode/Nyquist displays.

[CT08](labs/CT08-lead-lag/README.md) adds an original reusable lead/lag
comparison with matched DC gain, explicit control effort and cancellation.

The first implemented signal-processing lab is [SP06 — three-tap FIR](labs/SP06-three-tap-fir/README.md):
predict a filter's response, run and inspect it, then remove one tap and explain
the changed result. The diagram's description links to the offline S7 guide
and MIT online teaching materials. [Development evidence](EVIDENCE.md) records
exact checks and remaining qualification.

S7 is an independent open-source educational/research project, **not an
official MIT course resource or MIT-endorsed product**. This collection
illustrates concepts taught in MIT subjects and books through original
S7 examples; it does not mirror, distribute or solve MIT assignments.

## What a student should be able to do

**Predict → Construct → Simulate → Measure → Verify → Explain → Extend.**
The goal is to understand signal-processing and control models by building
explicit causal networks, comparing measured behavior with independent
equations, and discovering when a method or model fails. The visible graph,
initial state, delay placement, signal rate and observation path are part of
the experiment—not implementation details to hide.

The labs also serve as **executable specifications for S7**. An independent
reference fixes the equations, units, initial state, sample convention and
numerical method; the saved graph must reproduce the declared behavior and
its limits. A matching plot alone is insufficient. This establishes bounded
conformance for the tested model, not a proof of every runtime path. Claims
of reproducing a book example additionally require its exact edition,
page and equations to be recorded and cross-checked.

A standard lab has an estimated learning time, prerequisites, a defined
mathematical problem, a diagram (or construction steps), a headless
reference test, an interactive Studio inspection path and suggested
variations. See [the lab/verification contract](LAB_STANDARD.md).

## Navigate by subject

| Learning route | Proposed labs | MIT source / level |
| --- | ---: | --- |
| [Classical signals, blocks and systems](COURSE_MAP.md#foundations-fn-10-labs) | 10 | Siebert; Mahajan–Freeman; historical 6.003 |
| [Signal processing](COURSE_MAP.md#signal-processing-sp-16-labs) | 16 | MIT 6.3000, Fall 2026 |
| [Dynamic modeling and feedback control](COURSE_MAP.md#modeling-and-control-ct-14-labs) | 14 | MIT 6.3100, Fall 2026 |
| [Systems and inference](COURSE_MAP.md#systems-and-inference-in-8-labs) | 8 | MIT 6.3010; historical 6.011 |
| **Total planned** | **48** | Topics may reuse the same underlying diagram |

The [verified primary sources](SOURCES.md) document which material was
consulted, what is only a *topic alignment*, and when a precise book/problem
citation requires additional inspection. Course codes and schedules can
change; the curriculum is a dated snapshot, not a claim of MIT affiliation.

## Recommended progression

1. **First encounter:** FN01 (delay/gain/adder), FN03 (initial conditions),
   SP01 (Fourier synthesis), SP03 (sampling/aliasing), CT01 (RC/thermal step).
2. **After mastering discrete-time models:** FN05/FN06 (integrators and
   pole mappings), SP06 (FIR/convolution), SP08 (DFT leakage), CT04/CT05
   (PID, load rejection and windup).
3. **Advanced work:** SP11 (STFT), CT10 (state space), CT12 (observer),
   IN01–IN08 (noise, estimation, inference). Advanced items have
   prerequisites and may not belong to the 0.2.0 supported subset.

This order is pedagogical, not a mandate to implement lab IDs in sequence.

## One model, several curricula

The new canonical runnable files live under
[`../examples/education/`](../examples/education/README.md). The curriculum
references them rather than copying diagrams into multiple courses. An RC response can
support a circuit lesson, a discrete approximation lesson and an intro
control lesson using distinct lab questions and verification conditions.
**Count an instructional exercise once by its lab ID**, not once per
diagram or alternate method.

The 0.2.0 [release contract](../docs/RELEASE_0.2.0.md) and
[work plan](../docs/RELEASE_0.2.0_WORKPLAN.md) own release gates; this
directory owns the education scope and lab quality standard. The
[implementation plan](IMPLEMENTATION_PLAN.md) prioritizes a coherent
reference-qualified core, then expansion. A lab's actual implementation
status must be updated only after checked evidence is committed.

## Boundaries

- Local deterministic labs must work with the headless C++ runtime without
  hardware, private datasets, network access or Studio-only computations.
  Studio visualization is an additional required user workflow for
  instructional labs accepted into the release.
- Hardware, cameras, remote observations, general multi-rate networks and
  unimplemented numerical methods are not implicit prerequisites.
- The proposed **SP06 FIR/filter lab** may be reused as the practical
  two-host 0.2.0 engineering demo, but its local numerical reference must
  be qualified first.
- Use independently authored explanations and figures. Point students to
  original MIT/publisher pages for readings. Do not copy problem statements,
  answer keys or book figures into S7.
- A lab guide must distinguish **illustrates the topic** from
  **reproduces a named example**; the latter requires actual edition,
  page/problem and equations cross-checked against the source.

## Documents and responsibilities

- [COURSE_MAP.md](COURSE_MAP.md): complete 48-lab syllabus and
  existing-versus-new starting points.
- [LECTURE_ALIGNMENT.md](LECTURE_ALIGNMENT.md): **Fall 2026 MIT week-by-week
  crosswalk** for 6.3000 and 6.3100, with supported topic IDs and
  explicit gaps, not copied homework exercises.
- [SOURCES.md](SOURCES.md): course/textbook links, provenance and licensing.
- [LAB_STANDARD.md](LAB_STANDARD.md): student-facing format, numerical
  reference discipline, CI/GUI qualification and status vocabulary.
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md): ordered Codex work,
  dependencies, PR boundaries, sample releases and acceptance checklist.

Before coding, review the actual example inventory and choose the first
small, reviewable set. Completion claims must cite runs on a named candidate
commit and not rely on this proposal alone.
