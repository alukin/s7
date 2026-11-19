# MIT-aligned S7 teaching collection — implementation and verification plan

**Planning scope agreed 2026-11-18.** This is a work order for future Codex
implementation and human review. Do not regard the 48 proposed lab IDs as
existing, implemented or tested. Preserve all current S7 architectural
contracts and the 0.2.0 [release gates](../docs/RELEASE_0.2.0.md).

Related: [curriculum map](COURSE_MAP.md),
[lab verification standard](LAB_STANDARD.md),
[source and attribution record](SOURCES.md) and
[0.2.0 work-plan acceptance matrix](../docs/RELEASE_0.2.0_WORKPLAN.md).

**2026-11-19 maintainer revision:** all education deliverables use new,
purpose-built diagrams in `examples/education/`. Earlier instructions below
to reuse or adapt existing runnable diagrams are superseded: inspect those
examples and their tests as engineering references, then design the teaching
graph afresh. Continue using existing primitives, loaders and test tools.
The [core manifest](CORE_MANIFEST.md) records per-lab gaps and delivery order.

## Closing reminder — viewer audit

**Maintainer request, 2026-11-19:** before declaring the education work
finished, remind the maintainer to audit **all delivered educational examples
for viewer correctness**. Keep this reminder open until that review occurs.
Check displayed samples against recorded data, channel names and units,
model-time axes, selected analysis intervals, frequency axes, amplitude/power
normalization, overlays and save/reopen/rerun behavior. Per-lab automated
checks support this collection-wide review; they do not replace it.

- [ ] Remind the maintainer at the final education review.
- [ ] Record the collection-wide viewer audit and resolve its findings.

## Objective and scope decision

Build an **open, original, MIT-aligned learning laboratory** for signals,
systems, numerical modeling and feedback control. Students should be able
to predict, construct, run, inspect and independently verify mathematical
models, then perturb them to investigate limits.

The broad syllabus contains **48 exercises** (FN10 + SP16 + CT14 + IN8).
Start by qualifying the **24 named 0.2.0 core exercises** below and expand
where ready. Their existence in the curriculum is a **target, not a
green build**. The maintainer may adjust the named set after comparing
implementation cost and pedagogical value, with an explicit release
scope revision. Do not bypass required correctness checks to meet a count.

```text
Foundations (6): FN01 FN02 FN03 FN04 FN05 FN06
Signal processing (10): SP01 SP03 SP05 SP06 SP07 SP08 SP09 SP10 SP11 SP14
Modeling/control (8): CT01 CT02 CT03 CT04 CT05 CT06 CT07 CT08
Total named core: 24
```

**Important:** no new global ODE solver, implicit general algebraic-loop
solver, parallel graph execution, remote live observer API or new
hardware/video subsystem is required. All initial student lessons should
run locally with a headless C++ runtime and an existing Studio observer
workflow; distribution is applied to **one already verified** suitable
engineering lab (SP06 preferred).

## Roles and PR boundaries

- **Maintainer:** approve pedagogical priorities, exact reference claims,
  architecture and numerical acceptance; review student usability and
  decide final release inclusion.
- **Codex:** inventory existing diagrams/tests; implement and verify small
  changes; update lab guides/catalog, record commands/results; prepare
  focused PRs, **never claim tests not run**.
- **teren_ukr:** retains D0/D1/D2 Python/distributed responsibilities.
  Educational work may consume the stable common subset but should not
  silently change network protocols or assign the student's workstream
  without coordination.
- **Reference calculation:** derive/implement independently from the S7
  graph runtime. A different S7 diagram, same solver implementation,
  or same FFT function wrapped twice is not an independent oracle.

A PR should normally cover **one coherent lab concept or a small linked
family**, plus reference implementation, tests and teaching notes. Avoid
one mega-PR with 24 examples and no reviewable provenance.

## Phase 0 — Inventory, baseline and course-source audit

**Deliverable:** a reviewed per-lab implementation manifest for the
24 core IDs, without changing runtime behavior.

1. Enumerate existing `s7l2/examples/**/*.s7diagram`, embedded
   schema resources, generators and CTest/smoke tests. Inspect the
   diagram payload and runtime configuration; do not infer behavior
   solely from filenames or existing prose.
2. Map each core ID to **existing verified** diagram(s), **candidate
   reuse needing modification**, or **new work**. Compare
   `NUMERICAL_VALIDATION_MATRIX.md`, numerical research reports,
   source tests and Studio UX tests. Record genuinely missing items.
3. Recheck official MIT course pages and the actual textbook passages
   used for *specific* claims. Report whether a lab is a topic
   illustration or exact cross-checked adaptation; no invented page
   numbers, figures or exercise identifiers.
4. Identify supported software/OS/Studio features and optional
   dependencies. Choose canonical example paths and a minimal test
   matrix that does not duplicate the whole existing suite.
5. Record the initial status as `proposed`, `source-reviewed`
   or `diagram-ready`, **not** `headless-verified` until the new
   oracle has actually been exercised.

**Exit:** inspectable mapping and prioritized PR list, no silent
overwriting of earlier established research conclusions.

## Development progress — 2026-11-19

Twenty-four core lessons now have new graphs and student guides: FN01–FN06,
CT01–CT08, SP01, SP03, SP05–SP11 and SP14. The [manifest](CORE_MANIFEST.md) and
[executed evidence](EVIDENCE.md) distinguish development checks from final
release qualification. The named core implementation set is complete.

The latest batches verify controller/plant states, windup and saved nested
edits (CT04/CT05), sampled poles and measured Bode/Nyquist curves (CT06/CT07),
a reusable lead/lag network with matched DC gain and control effort (CT08),
and coherent AM phase, finite-filter states and selected spectra (SP14).
Earlier signal-processing lessons independently check runtime sequences and
production convolution/spectral processors without changing causal semantics.

Next: integration, installed/relocated package navigation and the named
release-candidate checks. The closing collection-wide viewer-audit reminder
above remains open; native Windows and human review are not inferred from
Linux development tests. The additional 24 non-core curriculum items remain
outside the accepted 0.2.0 implementation set.

## Phase 1 — Classical foundations and discrete modeling (FN)

Use the existing Siebert/Mahajan–Freeman reading record and local
reference-qualified RC/RLC and oscillator diagrams.

Suggested sequence:

1. FN01–FN03: delay/gain/adder, explicitly initialized recurrence,
   natural/forced responses. Verify first samples against written
   recurrences; include wrong-delay/initial-state negative case.
2. FN04: input impulse versus transfer function, using current
   `frequency_response` examples and a direct recurrence for a small
   stable LTI system.
3. FN05/FN06: discrete Euler/backward/trapezoid realizations and
   pole maps, with the exact methods and applicable eligibility
   restrictions already in `INTEGRATORS.md`. Do not mislabel
   Euler–Cromer, leapfrog or Gauss–Legendre. Test unstable/large-step
   regimes as explicit negative teaching results.
4. Preserve headless first; qualify Studio open/run/observe, parameter
   editing and save/reopen where the lab needs it. Do not invent
   additional integrator production modes for a diagram.

**Exit:** six qualified discrete-systems labs with independent
initial-state and numerical-method evidence.

## Phase 2 — Core signal processing (SP)

Build in short thematic PRs:

| Order | IDs | New instructional outcome / reuse |
| --- | --- | --- |
| A | SP01 | Fourier synthesis and partial-sum/Gibbs verification |
| B | SP03 | Exact sampled-tone aliasing, correct model-rate labeling |
| C | SP05, SP06 | Direct discrete convolution, three-tap FIR; reuse convolution/runtime primitives only when they preserve the written graph-level block semantics |
| D | SP07 | RC/second-order frequency response and discrete versus continuous models |
| E | SP08, SP09 | DFT leakage and window normalization; independently implement short direct DFT |
| F | SP10 | Linear/circular convolution and explicit zero-padding |
| G | SP11 | STFT window/hop/chirp verification, edge labeling |
| H | SP14 | Coherent AM sidebands and demodulation with reference and transient interpretation |

**SP10 workflow decision, 2026-11-19:** retain the scalar graph-port contract.
Three explicit pulse/Delay word builders feed three FIR networks; repeating
the word every 4 or 8 ticks demonstrates circular folding and padding after
warm-up. The isolated word begins at n=8 so all three share the comparison
block n=8…15. Headless checks pass the actual recorded word to the production
FIR/FFT frame APIs and independently verify full linear output, folded output,
streaming partitions and complete tails. This uses existing primitives and
does not introduce frame-valued ports, observation-side computation in the
runtime, or new causal semantics. SP11 now adds an original gated-tone/chirp
graph, independent complex STFT checks and actual map/slice rendering checks.
CT04/CT05 now add independently checked controller/windup comparisons.
CT06/CT07 now add sampled poles and independently checked stability margins.
CT08 now adds a reusable schema compensator with independent state/response checks.
SP14 completes the named core implementation set. Next: integration/package
checks and the final collection-wide viewer review.

**SP06 reference contract, proposed first engineering flagship:**

- `h=[1/3,1/3,1/3]`, `y[n]=(x[n]+x[n-1]+x[n-2])/3`,
  `x[n<0]=0` unless otherwise declared.
- Build filter from explicit two unit delays, three gains, adder.
  Confirm a one-sample impulse produces exactly the three-tap response.
- Verify the complex response `H(e^(jw))=(1+e^(-jw)+e^(-j2w))/3`
  at DC, the two unit-circle zeros and nonzero interior frequencies.
- Test multi-tone input with both a retained and an attenuated
  component; verify output sequence against independent direct
  convolution and analytic transfer values.
- Only after local qualification, run the same *meaningful* chain
  with source and filter on two physical headless hosts, compare
  values/order and capture the output through an existing supported
  recording or observation path. This is G2's practical S7
  demonstration, **not** a multi-host feedback stress test.
- Record transport and wall-clock performance separately from the
  exact discrete-filter result. No hard real-time promise.

**Exit:** ten core signal-processing teaching labs, including a
reference-qualified candidate that feeds G2 without additional
engineering systems. Do not make SP16 image processing a release
prerequisite merely because MIT covers images.

## Phase 3 — Core modeling and feedback control (CT)

Build around existing linear and control families, not copied MIT
hardware labs.

| Order | IDs | Independent verification focus |
| --- | --- | --- |
| A | CT01, CT02 | Analytical first-/second-order responses, units, damping and physical analogy |
| B | CT03 | Explicit continuous-time versus discrete pole comparison and timestep stability |
| C | CT04, CT05 | Real saved P/PI/PID and saturation/anti-windup recurrences |
| D | CT06, CT07 | Closed-loop characteristic roots, sampled Bode/Nyquist and correctly defined margins |
| E | CT08 | Original lead/lag controller with independently evaluated discrete transfer and step response |

The existing `examples/control` and `examples/frequency_response`
are candidates to audit. A Bode plot alone cannot verify a
closed-loop controller under saturation; a pretty step response
alone cannot verify a stability margin. Separate these claims.

**Exit:** eight core modeling/control labs, qualified both
numerically and through the intended Studio workflows.

## Phase 4 — Broader subject coverage (stretch / future choices)

The remaining FN07–FN10, SP02/SP04/SP12/SP13/SP15/SP16,
CT09–CT14 and IN01–IN08 belong to the **48-lab syllabus** but not
to the initial 24-lab gate. Promote them individually as their
math, element capabilities and independent tests mature.

- Advanced state-space/LQR/observer labs should use small exact
  matrix models and independent Riccati/eigenvalue calculations.
- Stochastic detection labs require multiple seeds, defined
  confidence intervals and explicit false-alarm/miss rates.
- Whole-frame image processing (SP16) requires a separate media
  execution/ownership decision; a synthetic static matrix
  experiment may be viable earlier without shipping live video.
- Distributed time-difference-of-arrival (IN08) requires honest
  timing provenance and uncertainty, not the assertion that
  packet arrival implies synchronized sampling.

Do not accidentally transfer these dependencies into 0.2.0.

## Phase 5 — Integration, curation and packaging

1. Add a **course/concept navigator** in Markdown first, pointing to
   canonical diagrams and lab notes. Add Studio Help/example-menu
   integration only after UX and offline packaging behavior are
   reviewed. Educational metadata must not alter runtime execution.
2. Ensure each qualified lab has valid source citation, units,
   prerequisites, explicit equations, predicted output, commands,
   inspection instructions, documented failed experiment and
   independent reference test.
3. Verify all referenced diagrams and notes in actual Linux
   portable, RPM/DEB and applicable Windows package profiles.
   Existing Studio manual workflow checks may require human review;
   report automated/offscreen and native-interactive evidence
   separately.
4. Make focused tests efficient, but run the **full applicable**
   local numerical, Studio and network regressions on the final
   release candidate. Never hide skipped/unfinished tests.
5. Reconcile `education/COURSE_MAP.md` with lab status and the
   [single release acceptance matrix](../docs/RELEASE_0.2.0_WORKPLAN.md).
   The completed educational slice should be visible to an
   unfamiliar student without requiring an oral explanation.

## Acceptance and success criteria

The initial educational delivery target is the **24 named core labs**,
but release quality is not a diagram-count contest. Before treating
the educational goal as complete, each counted lab must have:

- A reviewed mapping to an actual MIT topic and properly checked
  source attribution (no unverified assignment claims).
- Canonical S7 diagram and instructions that a new student can run
  without obscure local files, accounts, private datasets or hardware.
- Independent reference, stated numerical envelope, saved-graph
  headless test, negative/failure experiment and recorded actual run.
- Studio open → run → observe → modify → rerun evidence and a known
  compatible installed/relocated profile.
- Exact provenance: candidate SHA, test command, tolerance/data
  convention and maintainer-reviewed result.

If a targeted item cannot meet the contract, leave it visibly
**blocked/deferred**, propose a replacement in the curriculum
(if pedagogically appropriate), and request an **explicit maintainer
scope decision** rather than silently counting it as passed.
A release may proceed only when the agreed G1–G4 criteria,
including the explicitly approved educational core, are met.

## Codex execution handshake

For every implementation PR, Codex should:

1. Read `AGENTS.md`, these four education documents, the existing
   examples/validation matrix and the selected source material.
2. State the lab IDs, source alignment, exact existing files
   to reuse and *new* files to create; identify runtime changes
   that would need separate architectural review.
3. Freeze the equations, sample/initial conventions, independent
   oracle and acceptance conditions **before** comparing numbers.
4. Implement the smallest complete lab, generator/test and student
   documentation together. Do not create placeholder lab folders.
5. Run focused tests, then relevant regressions. Report concrete
   commands, pass/fail/skip counts, platform and unresolved issues.
6. Verify or clearly mark unverified Studio interaction and
   installed-package location. Do not treat screenshots as full
   interface certification.
7. Update curriculum status and evidence. Never claim that
   an exercise reproduces a named textbook problem until its
   actual primary-source equations were checked.
8. Prepare an independently reviewable PR. **No automatic merge**;
   the maintainer reviews equations, source claims, UI usefulness
   and test provenance.

This handoff separates **design now** from **implementation and
verification later**. Future Codex activity is not evidence of
work already completed by this planning commit.
