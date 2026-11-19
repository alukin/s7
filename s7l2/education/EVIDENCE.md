# Education development evidence

This file records executed development checks, separately from the final
release acceptance matrix. Earlier batches used baseline `96fe7b438ceeb9a206d404b4ae2a05943c89b21d`.
Current working changes are on `education/core-labs` at
`440eb388b5250c127fa3196f2887294affba142a` (README acknowledgment update).
No final-candidate or installed-package qualification is claimed.

## SP06 — initial local check, 2026-11-19

Environment: Fedora 45 x86-64, Linux 7.3.3, GCC 16.2.1, CMake 4.3.0,
existing Studio-enabled Release configuration.

The first execution of `s7tests_education_sp06` passed:

- 1,200 samples, source formula/direct-convolution maximum absolute residual
  **1.43441e−13 V** against the frozen 1e−10 V threshold.
- Measured H(0)=1; H(10 Hz)=0.963898−0.204883j;
  H(100 Hz)≈(9.43e−16)+(9.70e−16)j.
- Unit-sample response and six complex transfer checks passed, including
  both zeros and Nyquist.
- The missing-middle-tap teaching case matched its altered recurrence;
  error against the intended filter reached **0.521867 V**. Predicted mean
  and residual 100 Hz amplitude were each **1/6 V**.
- A fresh repeated execution produced identical sampled input/output values
  in the same process/build. No cross-platform bitwise claim.

The first check preceded the diagram's move to `examples/education/` and
its linked Markdown notes. Final working-change checks are recorded below
after rebuilding the diagram path and Studio resources.

The new diagram generator, test, student guide and UI workflow are development
work. Lab status remains `diagram-ready` until the source revision, per-lab
review and remaining qualification are recorded. Physical two-host execution,
native Windows interaction and release-package access remain open.

## Completed development checks — 2026-11-19

Platform: Fedora 45 x86-64, Linux 7.3.3-300.fc45, GCC 16.2.1,
CMake 4.3.0, Qt 6.11.2. Build: existing `build-release`, Release,
Studio enabled. GUI interaction ran on private Xvfb with Mesa software
rendering. Browser dispatch was intercepted with `QDesktopServices` URL
handlers; these checks do not claim that a real desktop browser fetched
the readings or that native Windows was tested.

The graph is new work under `examples/education/`, not a copy of an older
example. The saved graph, input convention, 300 Hz rate, 1,200-step count,
oracle and tolerances are stated in the [student guide](labs/SP06-three-tap-fir/README.md).
Status remains **diagram-ready with passing development checks**, pending
maintainer review and a named release candidate; no core lab is counted as
release-qualified by this record.

### Executed commands

From `s7l2/`:

```bash
cmake -S . -B build-release
cmake --build build-release --target s7tests_education_sp06 s7studio_gui_workflows_test s7studio_gui_capture s7studio_diagram_test --parallel 2
cmake --build build-release --target s7studio s7tests_linear_examples_smoke s7tests_control_examples_smoke s7tests_measurement_examples_smoke s7tests_diagram_harness_smoke s7studio_model_time_test s7studio_spectrum_test s7studio_response_test s7studio_time_frequency_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_sp06|s7tests_linear_examples_smoke|s7tests_control_examples_smoke|s7tests_measurement_examples_smoke|s7tests_diagram_harness_smoke|s7studio_diagram|s7studio_model_time|s7studio_spectrum|s7studio_response|s7studio_time_frequency|s7studio_gui_education_sp06|s7studio_gui_workflows)$' --output-on-failure
./build-release/s7tests/s7tests_education_sp06 --trace /tmp/s7-sp06-trace.dat
python3 -B tools/check_example_diagrams.py
git diff --check
```

The selected CTest regression run passed **12/12 entries** in 130.63 s.
The ordinary GUI workflow suite reported **96 passed, 0 failed, 99 skipped**:
the skips are its per-diagram rendering rows, run only by the separate
`s7studio_gui_examples_*` shards. Those shards were not run here. The focused
education/link GUI suite reported **5 passed, 0 failed, 0 skipped**
(three workflow cases plus initialization/cleanup).

After visual inspection, the notes frame was enlarged to avoid clipped links
and the Scope captures were zoomed to 0–0.2 s. Rebuilding the workflow test and
running the following final selection passed **3/3 CTest entries** in 3.17 s:

```bash
cmake --build build-release --target s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_sp06|s7studio_diagram|s7studio_gui_education_sp06)$' --output-on-failure
```

### Numerical results

| Check | Observed result |
| --- | --- |
| Saved graph versus independent convolution, all 1,200 samples | Maximum absolute error `1.43441e-13 V`, budget `1e-10 V` |
| Startup, delay histories, first/final samples and model timestamps | Pass; n=0 is step 1 at 1/300 s; nanoseconds rounded from absolute model time |
| Same-process repeated-run comparison | Identical input/output sequences |
| Impulse and six complex-response points | Pass; `[1/3,1/3,1/3,0,...]`; `1e-14` budget |
| DC output/input coefficient | `H=(1,0)` |
| 10 Hz complex gain | Approximately `(0.963898,-0.204883)`, including phase |
| 100 Hz complex gain | Approximately `(9.43e-16,9.70e-16)` |
| Missing middle tap | Matches altered recurrence; intended-reference maximum error `0.521867 V`; output mean and 100 Hz amplitude both `1/6 V` |
| Example geometry/index audit | 99 diagrams, zero reported issues |

The generator reproduced the saved JSON byte-for-byte. The manifest contains
24 distinct core IDs. `ldd` on the numerical test showed no Qt dependency.
The tested source identity is the base commit above plus the working changes;
these SHA-256 values identify the final SP06 files independently of filenames:

```text
b0b21250d42c64e0fcc2a682c164e6efaca4ee85afb1d73ae1268613110932e5  examples/education/sp06_three_tap_fir.s7diagram
8e696ec57b51babc906d830bfd9761b665aad6cacbc7155a09eaccc52c821193  examples/education/generate_sp06.py
15184141ea8865f709bb7622d00e67dad883dd29419a8c2abe4e75c798b8277a  s7tests/education_sp06_smoke.cpp
```

Its headless path uses the production schema loader, validator, compiler,
runtime and observation API. Impulse and wrong-tap cases modify a copy of
the saved schema for their declared variations; the nominal graph is executed
without replacing it with a hand-written reference graph.

### Studio and student-path evidence

Automated checks exercised:

- Web-link clicks and the explicit external-browser context-menu action,
  preserving the current help page; HTTP query/fragment handling.
- Navigation to a bundled guide outside the top-level topic list and its
  fragment; unavailable/non-help targets leave the current page intact.
- Markdown and plain-URL frame links, click versus drag, border selection,
  F4 editing, format change and save/reopen.
- SP06 open → Scope → run at 300 Hz → check all recorded input/output
  samples against independent equations → edit `tap1` through the parameter
  dialog → save a copy → reopen → rerun and check the altered recurrence.
  Reopening uses fresh observer bindings; no stale viewer pointer is retained.
- Scope channel units and exactly 1,200 samples per channel; all three
  description-frame links within its visible bounds.

Screenshots were retained in
`build-release/s7studio/gui-artifacts/education-sp06/`: `education-sp06-diagram.png`,
`education-sp06-correct.png`, `education-sp06-missing-tap.png` and
`education-sp06-help.png`. Diagram and Scope captures were visually inspected;
inspection led to a more compact graph and enlarged instructions. Initial
test-fixture failures (an escaped test URL and a stale viewer pointer after
reopening) were corrected before the passing regression run; they were not
silently counted as passes.

### Remaining qualification

No fresh final-candidate full CTest/network/sanitizer suite, complete rendering
shards, physical-display usability review, two-host deployment, RPM/DEB/portable
relocation, Wine or native Windows qualification was performed. The guide is
embedded in the newly built Studio help; education-directory installation as
standalone files and actual candidate-package navigation still need checks.
The SP06 record above covers one new lab. FN01–FN03 were the next batch;
their separate results follow below.

## FN01–FN03 foundations

**Development checks, 2026-11-19.** Base HEAD is now `440eb38`
(README acknowledgment only), plus the education working changes. Environment
is the same Fedora 45/GCC 16.2.1/Qt 6.11.2 Release configuration as above.
These are three new diagrams, not reused example files. Each has its own
student guide, explicit Delay/Gain/Adder paths, saved **2 s / 10 Hz** run,
prediction table, source attribution, parameter experiment and negative case.
Lab status remains `diagram-ready` with passing development checks pending
review and final candidate/package qualification.

### Numerical contract and measured results

All signals are F64 voltages, with dimensionless gain a=0.5. Sample n=0 is
successful step 1 at Scope time 0.1 s. Every channel has 20 samples, ending
at time 2 s. Independent references use closed forms and finite sums:

```text
y[n] = a^(n+1) y[-1] + sum(j=0..n) a^(n-j) u[j]
```

The production schema loader, validator, compiler, runtime and observation
API execute the saved graphs. **Maximum absolute reference error was 0 V**
in all tested baseline and declared variation runs on this build. Budget:
1e-12 V, except 1e-10 V for FN01's growing gain-1.25 response. This is a
bounded discrete-model result, not a continuous-physics or cross-platform
bitwise guarantee. Freshly loaded repeated runs matched exactly.

| Lab | Baseline checks | Parameter and negative checks |
| --- | --- | --- |
| FN01 | Input=1 V, stored past=0 V; output starts 1, 1.5, 1.75, 1.875 V; previous channel lags one sample | Gain 1.25 matches its geometric sum and exceeds 300 V by sample 20; bypassing the Delay produces the explicit causal-memory cycle validation error |
| FN02 | Natural decay from stored past=2 V; unit-height impulse and step start at n=5, Scope time 0.6 s | Doubling step final_value doubles only that response; area=1 V·s gives a 10 V impulse at 10 Hz and fails the intended unit-height input claim |
| FN03 | Total equals natural + forced; residual=0 V; all input/state/sample conventions checked | Removing only total's stored past gives first residual −1 V; an extra input Delay shifts that contribution one sample and gives residual −1 V at n=5 |

### Student workflows and visual inspection

The dedicated native-Linux Xvfb suite passed all three data rows:

- Open each new diagram and click its **actual visible** guide link.
  All three frame links are within the visible frame bounds.
- Read the offline guide; then open Scope and accept the file's saved
  duration/rate **without repairing settings in the test**.
- Check every captured sample, timestamp, named channel, V unit and sample
  count against independent formulas. Saved rerun settings avoid old comparison
  traces being mistaken for new results.
- Select the named block, use F4 to change the guide's parameter, save a copy,
  reopen, reacquire the observer binding and run again. Check the altered
  recordings: FN01 gain 1.25, FN02 step height 2 V, FN03 total's initial memory 0 V.

The focused GUI suite reported **5 passed, 0 failed, 0 skipped** (three lab
rows plus initialization/cleanup). Screenshots are in
`build-release/s7studio/gui-artifacts/education-foundations/`, named
`FN01-diagram.png`, `FN01-student-guide.png`, `FN01-baseline-scope.png`,
`FN01-edited-scope.png`, and the corresponding FN02/FN03 files.
Visual inspection led to larger instruction text, readable lab titles and
wrapped help commands. A learning route and previous/next guide links were
added; the short-run notice and the n-versus-Scope-time convention are
explained explicitly. There was no manual physical-display usability session.

### Executed verification

```bash
python3 -B examples/education/generate_foundations.py
python3 -B tools/check_example_diagrams.py
cmake -S . -B build-release
cmake --build build-release --target s7studio s7studio_gui_workflows_test s7tests_education_foundations --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_foundations|s7studio_gui_education_foundations|s7studio_gui_education_sp06|s7studio_gui_workflows|s7tests_education_sp06|s7tests_diagram_harness_smoke|s7studio_diagram|s7studio_model_time)$' --output-on-failure
git diff --check
```

**8/8 selected CTest entries passed**, 136.02 s. The complete ordinary
Studio workflow suite reported **99 passed, 0 failed, 102 skipped**. Those
102 skips are the separately sharded bundled-example rendering matrix, not
passed renders. The dedicated FN and SP06 GUI selections also passed. No
full release, transport, sanitizer, Wine, RPM/DEB or relocation suite is claimed.

The geometry/index check covered **102 diagrams with zero issues**. All
three generated JSON files reproduced byte-for-byte, and every relative link
in the three new student guides resolved to an existing source-tree file.
The new graph/test hashes are:

```text
e7381f9459f4f18875db73e268c0519e484340dfa2b285f5fa839a99c77486fc  examples/education/fn01_unit_delay.s7diagram
7706bdee69a35030367b2a4083805335029df39c0b89794ffcc259b9d2662ddd  examples/education/fn02_natural_forced.s7diagram
6ef0ed4dfb8f248fe25b0cd7eedc0b283418dc3861df678339b00ed745822bc7  examples/education/fn03_initial_conditions.s7diagram
37933973fa22c84873bfad984dcc49364487035b1694e1728b5cd59ea52c9c86  s7tests/education_foundations_smoke.cpp
```

At the end of that batch the source included four developed labs
(FN01–FN03 and SP06), leaving 20 core labs. Final source/artifact
qualification, installed-package navigation, platform checks and review remain
open for these labs. The next batch is recorded below.

## FN04–FN06 and CT01–CT03 — linear models, 2026-11-19

Six original graphs and offline student guides extend the collection to
**10 developed core labs**; **14 core labs remain proposed**. Status is
**diagram-ready with passing development checks**, not release-qualified.
Working base: `440eb388b5250c127fa3196f2887294affba142a` plus the education
working changes. Platform/build are the Linux Release environment above.
No runtime/compiler/validator semantics or vendor sources changed.

### Frozen models and independent references

All saved graphs go through the production schema loader, validator,
compiler, runtime and non-invasive observation API. Every output sample,
fresh-run equality, primary edit and timestamp is checked. F64 sample budget:
`1e−10 × max(1, |reference|)` in the channel's units; timestamp budget 1e−12 s,
plus absolute integer-nanosecond rounding. State-bearing modes represent
x[n] at physical state time n h and appear at Scope time (n+1)h.

| Lab | Saved profile | Independent oracle | Additional checked limit/variation |
| --- | --- | --- | --- |
| FN04 | 20 Hz, 10 s, 200 samples | Shifted geometric impulse response; direct complex finite sums versus H(z)=0.5/(1−a z⁻¹) | a=0.5→0.8; complex H at DC, π/3, π/2 and Nyquist, budget 1e−11; nonzero stored past contaminates Y/X |
| FN05 | 2 Hz, 6 s, 12 samples | Powers of separately derived forward/backward/trapezoid poles; exp(−t) physical reference | Wrong fixed coefficient; h=0.25 with all coefficients redesigned; expected refinement order |
| FN06 | 1/3 Hz, 30 s, 10 samples | Closed-form powers of the four mapped poles | Initial amplitude 1→2; forward-Euler h=1.9, 2, 2.1 stability boundary; growing versus stable alternating modes |
| CT01 | 10 Hz, 5 s, 50 samples | 1−(1−h/τ)ⁿ and continuous 1−exp(−t/τ) | τ=1→2; h=0.05 refinement; h=2.5 nonphysical overshoot |
| CT02 | 100 Hz, 8 s, 800 samples | Independent complex matrix-mode powers and continuous damped sinusoids | b=1→3; h=0.005 refinement; negative damping b=−1 is physically active growth |
| CT03 | 2 Hz, 10 s, 20 samples | (1−h)ⁿ and exp(−t) | Actual rate edit to 0.4 Hz produces 1, −1.5, 2.25, −3.375; 4 Hz refinement |

FN05/FN06 implement explicitly solved **scalar linear** recurrences with
Delay/Gain blocks. They make no arbitrary implicit-solver claim. CT01–CT03
use existing EulerIntegrator elements; these follow the actual selected rate.
FN04's pulse is one unit-height discrete sample (area=h), with zero stored
past. A separate FN04 test sets stored past to 2 V, checks the added
natural response and its misleading DC ratio of 3 versus the zero-state
gain of 1. Finite runs use floor(duration/h), including the initial stored state.

Measured continuous-reference maxima (coarse → half h):

| Model/channel | Maximum absolute error | Budget on coarse run |
| --- | --- | --- |
| FN05 forward | 0.117879 → 0.0514732 V | 0.12 V |
| FN05 backward | 0.076565 → 0.0417206 V | 0.08 V |
| FN05 trapezoid | 0.00787944 → 0.00192913 V | 0.009 V |
| CT01 voltage | 0.019201 → 0.00939352 V | 0.02 V |
| CT02 position | 0.0149216 → 0.00739655 m | 0.04 m |
| CT02 velocity | 0.030979 → 0.0153171 m/s | 0.04 m/s |

Worst discrete-reference absolute residual was 7.78755e−12 in CT02's growing
negative-damping variant; every sample passed the scaled budget. Baseline
CT02 residual was 4.77396e−15. Small discrete residuals do not establish
physical accuracy, particularly for FN06 and CT03's intentionally coarse run.

### Student and Studio evidence

`educationLinear` opens each actual file, follows its visible description-frame
link into the offline guide, accepts the saved run without repairing it,
checks all recorded samples and units, performs the guide's primary edit,
saves, reopens and reruns. CT03 edits the real Run Configuration. Viewer
pointers are reacquired after reopen. FN04's measured response is saved and
checked as complex values against the independent H(z) at every returned
frequency (budget 1e−10), including pulse metadata and V/V units.

Screenshots are retained under
`build-release/s7studio/gui-artifacts/education-linear/`: for each ID,
`<ID>-diagram.png`, `<ID>-student-guide.png`, baseline/edited Scope files;
FN04 also has baseline/edited response captures. FN06 and CT02 each capture
two Scopes. Images were visually inspected; FN05/FN06 were compacted and
instruction text enlarged after the first captures. FN04's pulse view uses
0–1 s; FN06's stable-mode view uses 0–12 s. CT02 plots metres and metres/second
separately. This is Xvfb/software-rendered inspection, not a physical-display
student usability study. Public MIT resource links were checked separately;
no real desktop-browser navigation or native Windows interaction is claimed.

### Verification and reproduction

```bash
cmake -S . -B build-release
cmake --build build-release --target \
  s7studio s7studio_gui_workflows_test s7studio_gui_capture \
  s7tests_education_sp06 s7tests_education_foundations \
  s7tests_education_linear s7core_integrators_smoke \
  s7core_model_time_smoke s7tests_linear_examples_smoke \
  s7studio_diagram_test s7studio_model_time_test --parallel 2
ctest --test-dir build-release \
  -R '^(s7tests_education_(sp06|foundations|linear)|s7core_(integrators|model_time)_smoke|s7tests_linear_examples_smoke|s7studio_(diagram|model_time|gui_workflows|gui_education_(sp06|foundations|linear)))$' \
  --output-on-failure -V
python3 -B tools/check_example_diagrams.py
git diff --check
```

**12/12 selected CTest entries passed**, 163.83 s. The ordinary Studio
workflow suite reported **105 passed, 0 failed, 108 skipped**. The 108 skips
are the separately sharded bundled-example release-render matrix, not
successful renders. The dedicated linear selection reported **8 passed,
0 failed, 0 skipped** (six lab rows plus setup/cleanup). Its rebuilt binary
included the strengthened FN04 complex-result assertion; the ordinary
workflow suite ran before that additional assertion was added.

After adding FN04's nonzero-initial-state negative case, the final rebuilt
headless and dedicated Studio selections passed **2/2**, 12.83 s. The added
200-sample trace matched its independent closed form exactly on this build;
the misleading DC ratio was 3 as predicted. This supplements the 12-entry
regression run above.

Geometry/index coverage: **108 diagrams, zero issues**. All ten education
JSON files reproduced byte-for-byte from their three generators. Every
relative link in all ten student guides resolved to an existing source-tree
file. Final graph/reference/generator SHA-256 hashes:

```text
fecacb64042b6d9beb84298ee1cc258b88e037ebd72a6f62e1a3fbf9f522b06b examples/education/fn04_impulse_transfer.s7diagram
286a08c5ccb841574cd4c2e2feba6700ef7735ae82c54ab9c86615a83ff6f149 examples/education/fn05_integration_rules.s7diagram
3088ec4a0808f2de00f0716c96b4468f0fbddc37e12c9c2742aff4c9a8338db7 examples/education/fn06_pole_maps.s7diagram
fe62cca4a8e4c10249196b66685a66d7a067eaaccf0fe8d8df7dc7138231d1f2 examples/education/ct01_first_order.s7diagram
7b5db64665cf8a38826b43b450ecbd6b3b4e543d309f46f0f410656fba2f93f2 examples/education/ct02_damped_modes.s7diagram
6416eab6d14093dfd96e1a8fee05ca4363561078926d6c6f45457984f82be108 examples/education/ct03_sample_time.s7diagram
b0e792fafb75d9f877a3267a1884534114d66c971a4ae6e7fe6d4725f9d46872 s7tests/education_linear_smoke.cpp
cad9bde9aa7fca5ed3494a59d0c461466037b423108d0dcbfe6d2e69fbb852ae s7tests/education_linear_reference.hpp
9c86d4d9f0bb1412991c043227eb83832aba249a7b9278698aa3857868436346 examples/education/generate_linear_labs.py
```

Source and Studio help were rebuilt after guide/index/evidence updates.
No installed-bundle, relocation, native Windows, Wine, sanitizer, two-host
or final-candidate qualification is claimed. Next development batch:
**SP01/SP03**, followed by **SP05/SP07**. Maintainer review and the existing
release gates remain open; none of the 24 core IDs was dropped.
