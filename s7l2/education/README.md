# S7 teaching laboratories — MIT-aligned curriculum

**Status: curriculum and verification plan, not a shipped course bundle.**
Planning baseline: 2026-11-18. The [course map](COURSE_MAP.md) contains
**48 proposed lab exercises**, not 48 implemented or qualified diagrams.
Some reuse the existing S7 example library; some need new diagrams,
references or capabilities. Never advertise an exercise as complete simply
because its prerequisite S7 example runs.

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
   (PID and sampling).
3. **Advanced work:** SP11 (STFT), CT10 (state space), CT12 (observer),
   IN01–IN08 (noise, estimation, inference). Advanced items have
   prerequisites and may not belong to the 0.2.0 supported subset.

This order is pedagogical, not a mandate to implement lab IDs in sequence.

## One model, several curricula

The canonical runnable files remain under
[`../examples/`](../examples/README.md). The curriculum references them
rather than copying diagrams into multiple courses. An RC response can
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
