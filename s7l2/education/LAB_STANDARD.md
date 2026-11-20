# S7 educational laboratory contract and verification standard

This document defines what **"reference-qualified teaching lab"** means.
The [course map](COURSE_MAP.md) is a proposal, not completed work.
The [0.2.0 release work plan](../docs/RELEASE_0.2.0_WORKPLAN.md)
records actual accepted evidence. Creating a diagram, obtaining a
plausible-looking plot or passing a schema-load test is not enough.

## 1. Status and evidence vocabulary

Each lab has a stable ID (FN01–FN10, SP01–SP16, CT01–CT14,
IN01–IN08), course/topic mappings and an explicit status:

| Status | Meaning |
| --- | --- |
| `proposed` | Curriculum intent only; diagrams/tests need not exist |
| `source-reviewed` | MIT topic and any precise textbook/example attribution checked, scope bounded |
| `diagram-ready` | Saved canonical diagram exists, loads/validates, and elements are supported; no numerical claim |
| `headless-verified` | Exact saved diagram executes in headless mode; independent oracle and negative/boundary tests pass |
| `studio-verified` | Headless-qualified lab also passes intended Studio open/run/observe/edit/rerun workflow on stated platform |
| `release-qualified` | All required evidence reproduced on final tagged source/artifacts and links included in release matrix |
| `blocked` | Prerequisite or correctness issue recorded; do not count as qualified |
| `deferred` | Outside current release's accepted scope |

Statuses advance only after a **recorded command, commit SHA, result and
reviewed evidence**. The creator's assurance or an AI-generated conclusion
is not a test. A referenced existing example's prior tests may count only
after its exact diagram, parameters and applicable coverage have been
verified.

## 2. Student-facing laboratory format

Each student-facing `education/labs/<ID>-<slug>/README.md` (created when
implementation begins, not as empty placeholders) should contain:

1. **Heading and metadata:** ID, title, time estimate (15/30/60/90 minutes),
   level, prerequisites, associated course/topic, exact revision/date of
   readings, status, supported S7 version/platform.
2. **Learning outcomes:** two to four measurable predictions or skills,
   not generic statements such as "learn control".
3. **Source and rights:** official MIT course link and source section,
   optional verified book edition/page/problem, license/dataset provenance.
   Label **topic alignment** versus **exact example reproduction**.
4. **Physical/mathematical model:** independent equations, units, signal
   types, causal dependencies, delay convention, time-index origin,
   initial conditions and input schedule.
5. **Prediction:** explicit formula/table or estimated range **before
   running**. Include units and expected failure/limitation.
6. **Construction:** link to a canonical runnable diagram under
   `s7l2/examples/`; explain essential elements, any nested schemas
   and what students should change. Don't create duplicate diagrams
   for separate course labels.
7. **Run instructions:** exact S7 or Studio menu path, effective sample
   rate/time step, duration, seed, observer names and channels, packages
   and commands. State offline/online and hardware prerequisites.
8. **Measurement:** exact signals, axes/units, time interval, sample
   indices, frequency-bin/window normalization and what to inspect.
9. **Verification:** independent reference equations, executable test
   path, convergence or stochastic confidence bounds, numeric tolerance
   and reasons for the chosen metrics.
10. **Failure experiment:** deliberately incorrect rate, delay, gain,
    omitted initial state, unstable method or out-of-envelope behavior
    with a predicted interpretation; failures must not be quietly
    counted as successful physics.
11. **Student extension:** a new parameter set, alternate diagram or
    diagnostic question with no copied MIT solution.
12. **Limitations and troubleshooting:** software/hardware profile,
    unsupported solvers, finite record length, aliasing/edge effects,
    current GUI limitations and known numerical caveats.

The learning pattern is:
**Predict → Construct → Simulate → Measure → Verify → Explain → Extend**.
It is acceptable to use two diagrams for one exercise when comparing
algorithms; it is not acceptable to count those as two qualified labs.

## 3. Independent reference design

The **saved S7 diagram** must be the system under test. A headless
reference may not secretly replace it with an easier handwritten model.

For every accepted lab:

- Freeze the mathematical system, parameter values, initial states, event
  timing, model-rate convention, signal type, sample count and output
  capture interval **before** declaring tolerances.
- Write an **independent oracle**: closed-form physical response, direct
  difference-equation recurrence, symbolic pole/zero calculation,
  stand-alone reference solver, analytic DFT or independently coded
  convolution. A second S7 graph is **not** an independent oracle.
- Separate at least three comparisons when applicable:
  1. **Exact graph recurrence** versus executed S7 samples (runtime
     correctness).
  2. **Numerical discretization** versus exact continuous system
     (approximation/truncation error).
  3. **Physical model** versus actual measurements (model discrepancy).
  Do not collapse these into a single misleading "accuracy" score.
- Specify whether a state is observed **before or after** integration and
  whether the first sample is x[0] or x[1]. Include one exact early-step
  comparison and enough later samples to detect phase/amplitude drift.
- Explicitly model causal memory publication and delay placement;
  automatically treating a transport hop as a model-step delay is a bug.
- State absolute/relative error metrics and tolerance justification.
  Never invent a percentage as a release budget before comparing to
  mathematical conditioning, numerical precision and a baseline method.
- Add timestep refinement when approximation order is a learning target,
  at least two distinct step sizes and a documented expected convergence
  trend. Test instability deliberately at an admissible contrasting
  setting when pedagogically useful.
- Keep invalid/nonsolvable, divergent or out-of-range cases as distinct
  negative tests with declared expected error/behavior.

### Deterministic signal-processing checks

- **FIR:** verify h[0..2] = (1/3,1/3,1/3) for SP06, including initial
  delay states. For H(e^(jw)) compare complex magnitude *and phase*;
  include DC, a known zero, an interior frequency and a nontrivial
  finite input. Expect element-level sample ordering to match the
  stated discrete recurrence.
- **Sampling:** use analytically chosen frequencies related by
  integer multiples of the sampling frequency to prove sample equality;
  distinguish continuous signals from their sampled values.
- **DFT:** independently compute a direct O(N^2) DFT on short vectors;
  check bin indexing, normalization, DC/Nyquist, window coherent gain,
  off-bin leakage and circular-convolution aliasing. FFT and DFT
  implementations that share code are not independent oracles.
- **STFT/CWT:** check selected coefficients, time/frequency indexing,
  window support and masked edge values, not only image appearance.
  Wavelet magnitudes are not automatically PSD or calibrated tone
  amplitudes.
- **Control:** verify the **actual discrete controller and plant
  recurrences** in saturation and disturbance cases. Continuous-time
  pole plots alone do not qualify a sampled controller.
- **Chaotic dynamics:** short-horizon trajectories and long-horizon
  invariant/statistical properties have different metrics; do not
  require long-run pointwise Lorenz equality.

### Stochastic and inference checks

- Fix seeds and RNG identity for deterministic reproducibility, but
  also test **multiple independent seeds** when evaluating statistical
  properties, noise robustness or detection rates.
- State number of trials, chosen confidence intervals and analytical
  expectation/variance. Do not label an unqualified single random
  realization a verification of a stochastic claim.
- Correlation sign, lag convention, unbiased/biased normalization,
  PSD density units, bin widths and one-sided/two-sided power must
  be explicit.
- Use small exact matrices/recurrences as first references for LQR,
  Wiener and Kalman; compare covariance and error dynamics rather
  than just a visually plausible estimation trajectory.
- Never turn availability of a plotting plugin into evidence that a
  controller or stochastic estimator works as part of the causal graph.

## 4. Required test layers

| Layer | Required check and evidence |
| --- | --- |
| **Static** | Exact on-disk diagram loads in production loader, validates and compiles; all referenced element/plugin resources present |
| **Headless numeric** | Execute the diagram under the C++ runtime; compare recorded samples against independent reference, with early/late and boundary cases |
| **Negative/variation** | Change at least one meaningful parameter and exercise a predicted failure mode, solver limit or aliasing/instability contrast |
| **Studio workflow** | Open from examples list or explicit path; run; observe correct named channels, axes, plots and units; edit a parameter; rerun; save/reopen where part of the lesson |
| **Distribution, only where specified** | Run the **same reference-qualified graph** with explicit split on two physical headless hosts; assert order/values/faults. Do not require distribution for other labs |
| **Release packaging** | Verify referenced diagrams, guides, help assets and required plugins are available in actual intended installed/relocated packages, not merely source checkout |

Headless numeric and GUI layers must not be conflated. GUI screenshots or
offscreen renderer tests supplement inspection but do not establish all
real user interactions. A lab cannot be `studio-verified` just because
the headless numerical smoke test passed.

## 5. Time, sample and plot semantics

Every lab must fix:

- Sample rate F_s and period h=1/F_s; exact step index for stimulus,
  first output and measurement. Name **model time** explicitly.
- Units, complex/real types, state initialization, signal orientation,
  rate conversion/interpolation and changes made inside/outside a run.
- For a spectral display: N, window, hop (where relevant), amplitude,
  power or PSD normalization, one-/two-sided treatment, DC/Nyquist,
  edge/validity masks and bin-selection policy.
- For a feedback model: delay locations and whether algebraic cycles
  are valid, rejected or solved by a specifically qualified coupled
  integration method.
- Export or recording provenance sufficient to trace the exact
  diagram, parameters, seed, sample interval and selected channels.
  If existing formats cannot preserve every metadata field, document
  what is missing instead of silently claiming it.

Do not demand hard real-time performance, physical-clock alignment,
multi-rate distributed feedback, remote live viewing or hardware
device qualification from an otherwise local deterministic lab.

## 6. Implementation and tests organization

Use new purpose-built educational diagrams (maintainer decision, 2026-11-19).
Prefer existing S7 primitives and test harnesses; inspect earlier generators
and diagrams as implementation references without copying their graphs.
Codex should first survey
[`../examples/`](../examples/README.md),
[`../docs/NUMERICAL_VALIDATION_MATRIX.md`](../docs/NUMERICAL_VALIDATION_MATRIX.md)
and the actual CTest targets. Avoid introducing a new framework
merely to present a convenient curriculum table.

Suggested **future** directory shape, not pre-created empty content:

```text
s7l2/education/
  README.md
  COURSE_MAP.md
  SOURCES.md
  LAB_STANDARD.md
  IMPLEMENTATION_PLAN.md
  labs/
    SP06-three-tap-fir/
      README.md
      reference.py           # only if an independent oracle is needed
    CT03-discretization/
      README.md
      reference.py
  catalog.yaml               # optional after schema/tooling decision
```

If a machine-readable catalog is implemented, the **minimal record**
should include stable ID, title, level, estimated time, prereqs, mapped
course/topic, source URL/citation status, canonical diagram path(s),
status, reference implementation/test paths, supported platform,
validated rate/parameter envelope, CI/GUI evidence and maintainer
review link. Keep status derived from reviewed evidence; a generated
catalog is not an evidence source by itself.

Tests should use existing CTest labels where available. Introduce
focused lab tests in small PRs, but run the full relevant numerical
and Studio suites for merge and release qualification. Never mark an
unexecuted test `pass`; log `skip`, `xfail` or `not run` explicitly.

## 7. Source and copyright controls

- Provide precise **course/topic** references; cite an exact textbook
  section/example/problem only after examining the actual source.
- Do not copy MIT assignments, full textbook problems, solution guides,
  figures or paid content into the repository. Use S7-original
  explanations, numerical datasets and diagrams.
- For any adapted source, record year/edition, page number,
  source availability and license/permission status.
- Build examples with synthetic data unless an openly licensed real
  dataset is separately justified, licensed and attributed.
- Never imply MIT endorsement, official affiliation or compatibility
  with current grading/equipment.

## 8. Review checklist for each lab PR

- [ ] Correct source/topic link; "illustration" versus reproduction
      precisely stated and source edition checked where necessary
- [ ] Stable lab ID, objective, prerequisite, instructions and estimated time
- [ ] Canonical on-disk diagram, valid with the production parser
- [ ] All units/initial states/sample indices, methods and limits specified
- [ ] Independent oracle and reproducible headless numeric tests
- [ ] One deliberate parameter variation and one honest failure/limit
- [ ] Studio open/run/observe/edit/rerun coverage with named observables
- [ ] Tested version, platform, commands, results and tolerances recorded
- [ ] Packaged location/readability checked when release-qualified
- [ ] Existing architectural invariants and unrelated tests preserved
- [ ] Course map, lab status and release acceptance row updated with
      evidence, **not** a mere "implemented" checkbox

An exercise reaches release status only when its evidence is independently
reviewable. A collection that teaches *why a model fails* is more valuable
than one presenting 48 always-beautiful plots.
