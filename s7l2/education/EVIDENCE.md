# Education development evidence

This file records executed development checks, separately from the final
release acceptance matrix. Earlier batches used baseline `96fe7b438ceeb9a206d404b4ae2a05943c89b21d`.
SP01/SP03, SP05/SP07, SP08/SP09, SP10 and SP11 were developed from
`a27b2bb2ba08deeea97aef470c50723b6ac03a38` and committed as `e6e0b36`.
CT04/CT05 use that committed baseline. Earlier sections retain their
original baseline and measured results.
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

## SP01/SP03 — synthesis, sampling and viewer checks, 2026-11-19

Working base: `a27b2bb2ba08deeea97aef470c50723b6ac03a38` plus this batch.
Platform: Fedora 45 x86-64, GCC 16.2.1, CMake 4.3.0, Qt 6.11.2;
Studio-enabled `build-release`, native Linux Xvfb with software rendering.
The two original graphs bring the source inventory to **12 developed core
labs and 110 example diagrams**. Twelve core labs remain proposed.
Status: **diagram-ready with passing development checks**; release review,
installed-package and platform qualification remain open.

### Frozen numerical contracts

Both use production schema loading, validation, compilation and execution,
with read-only observation of every sample. Sources emit signal time n/F_s;
Scope records that sample at model time (n+1)/F_s. The finite F64 sample and
normalized complex coefficient budget is **1e−10 V**; power budget
**1e−11 V²**; time budget **1e−12 s**, plus integer-nanosecond rounding.
The test oracle evaluates independent long-double formulas and a direct
complex DFT, sharing no FFT or S7 element implementation.

| Lab | Saved experiment | Independent reference and variations |
| --- | --- | --- |
| SP01 | 2 s at 128 Hz; 256 samples/channel | Odd sine harmonics 1,3,5,7,9 of 2 Hz, coefficients 4/(mπ), one-/three-/five-term sums; ±1 V square target with right-hand values at jumps |
| SP03 | 2 s at 64 Hz; 128 samples/channel | 10 and 74 Hz sines at phase 0.375 rad; 54 Hz sine at phase π−0.375; exact sample equality and shifted-minus-reference residual |

SP01 tests signed complex coefficients (including DC and empty bins),
finite mean square and Parseval. Measured values were **0.810569**, **0.933056**
and **0.959605 V²**. The five-term sampled peak was **1.178202 V**, and all
partial sums had the predicted jump midpoint. Setting the ninth coefficient
to zero removes its bin. At 32 Hz, the ninth harmonic aliases negatively
onto the seventh: the 14 Hz amplitude becomes **8/(63π) V**. This deliberate
failure is distinct from finite-series ringing at a sufficient sample rate.

SP03 tests the equality of all three sampled source sequences and the near-zero
residual. Editing shifted from 74 to 70 Hz yields a 6 Hz alias and a nonzero
difference. Restoring 74 Hz and running at 256 Hz yields separate 10, 74 and
54 Hz peaks. Fresh runs matched exactly in this process/build. Maximum
sample residual across the named cases was **3.91631e−14 V**; baseline SP01
was **3.06977e−14 V**, baseline SP03 **2.64233e−14 V**.

The source-registration omission in the first headless harness was corrected
with the existing built-in registry hook. No runtime, compiler, validator,
solver or vendor implementation changes were needed.

### Viewer correctness evidence

The `educationSignals` workflow follows each actual visible guide link,
accepts the saved rate/duration, opens Scope and checks the journal against
the independent formulas. It then checks **every plotted time-series point**
against both the recording and independent oracle, with exact channel
mapping, names, V units, model-time axes, first/final timestamps and counts.
The primary edit is performed through F4, saved, reopened and rerun. SP03
also restores the source and changes the actual Run Configuration to 256 Hz.

For each baseline, edited and higher-rate profile, the actual Spectrum UI is
configured for a rectangular window, all real inputs, no mean removal and
linear display. The full 2 s interval is analyzed with N=256 (SP01), N=128
(SP03) or N=512 (SP03 at 256 Hz). Every returned **and plotted** one-sided
bin is checked against a direct DFT with proper DC/Nyquist treatment:

- Peak amplitude: 2|X[k]|/N for interior bins, no doubling at the endpoints.
- Power/bin: 2|X[k]/N|² for interior bins, with endpoint treatment.
- PSD: power/bin divided by Δf; integration is checked against time mean square.
- Spectrum budget: **1e−9** in the displayed quantity's units; integrated
  power budget **1e−10 V²**.
- Frequency spacing, Nyquist bound, axis titles, units, legend-to-channel
  mapping and the current run identity are verified.
- Selecting **0–1 s** then analyzing must produce half as many samples and
  **1 Hz** bin spacing instead of 0.5 Hz. Frame steps, values and bounds
  are checked, detecting stale full-interval results after selection changes.

The first GUI pass exposed clipped links in SP03's instruction frame; the
frame was enlarged, and the actual link-click check then passed. Visual
inspection also prompted role-based labels **reference**, **shifted**,
**reflected**, **difference**, and **full sum**. These remain truthful after
frequency/coefficient edits. An edited 70 Hz source is no longer misleadingly
named “74 Hz” in a viewer legend.

Retained images under `build-release/s7studio/gui-artifacts/education-signals/`
include both diagrams/guides and baseline/edited time, spectrum and selected-
interval captures; SP03 also has `SP03-256Hz-*.png`. These images were visually
inspected. Initial failure captures may remain alongside the final passing
`results.xml`. This is software-rendered inspection, not a physical-display
student usability study. The checks cover the Time/Spectrum paths used by
these lessons, not a completed audit of every viewer or every education lab.
The maintainer's **final collection-wide viewer-audit reminder** remains open
in [IMPLEMENTATION_PLAN](IMPLEMENTATION_PLAN.md#closing-reminder--viewer-audit).

### Executed verification

```bash
cmake -S . -B build-release
cmake --build build-release --target \
  s7studio s7studio_gui_workflows_test s7studio_gui_capture \
  s7studio_scope_view_test s7studio_spectrum_test \
  s7tests_education_signals s7tests_education_linear \
  s7tests_education_foundations s7tests_education_sp06 \
  s7core_integrators_smoke s7core_model_time_smoke \
  s7tests_linear_examples_smoke s7studio_diagram_test \
  s7studio_model_time_test --parallel 2
ctest --test-dir build-release \
  -R '^(s7tests_education_(sp06|foundations|linear|signals)|s7core_(integrators|model_time)_smoke|s7tests_linear_examples_smoke|s7studio_(diagram|model_time|scope_view|spectrum|gui_workflows|gui_education_(sp06|foundations|linear|signals)))$' \
  --output-on-failure -V
python3 -B tools/check_example_diagrams.py
git diff --check
```

**16/16 selected CTest entries passed**, 179.01 s. Ordinary Studio workflows:
**107 passed, 0 failed, 110 skipped**; the 110 skips are the separately sharded
bundled-example release-render matrix. Dedicated signals GUI: **4 passed,
0 failed, 0 skipped** (two lab rows plus setup/cleanup). After the role-label
refinement and resource rebuild, final numerical and signals GUI selections
passed **2/2**, 8.73 s. No blanket full-release test claim is made.

All **12 education diagrams** reproduced byte-for-byte from their four
generators. The static index/geometry check covered **110 diagrams with zero
issues**. All relative links in the top-level README and twelve student guides
resolved in the source tree. Final SHA-256 values:

```text
b3022dcfdca68bce7f0604dcb5a7a81fb874e63234232bf493cd14fbbb7d786f examples/education/sp01_fourier_synthesis.s7diagram
d10de16ea84ce486891a98304904c0a1cb4a2aa701539fc79ed4ac5a54f6a20a examples/education/sp03_aliasing.s7diagram
64548655494038ceae140d76fd7c5ef0b8dbe5e731a2a090dd11465b56fc5729 examples/education/generate_signals.py
4b0d130ee4ad5e36b3b1cb14f06160252a5210441d8a315252394191518515cb s7tests/education_signals_reference.hpp
2da4ac42d867b4a982d8fc5657e53dc92282607fc13015b2af2dc939ab83e811 s7tests/education_signals_smoke.cpp
cdc84cdd698b1424b502f2383a4e368fb1fee628c6990392b23dd62c6eb305a4 s7studio/tests/gui/workflows_test.cpp
```

MIT lecture resource pages and their relevant public notes were checked;
the source ledger distinguishes topic alignment from exact exercise
reproduction. No external handout or solution key is bundled. Next batch:
**SP05/SP07**. Installed/relocated packages, native Windows, two-host use,
sanitisers, final-candidate qualification and maintainer review remain open.

## SP05/SP07 — convolution and filter transfer, 2026-11-19

Working base: `a27b2bb2ba08deeea97aef470c50723b6ac03a38` plus the accumulated
SP01/SP03 and SP05/SP07 working changes. Platform/build: Fedora 45 x86-64,
GCC 16.2.1, CMake 4.3.0, Qt 6.11.2, Studio-enabled `build-release`, native
Linux Xvfb and software rendering. Source inventory: **14 developed core labs,
112 example diagrams**; **10 core labs remain proposed**. Status remains
**diagram-ready with passing development checks**, pending maintainer review
and the named release-candidate/package/platform checks.

### Frozen models and numerical results

Both new saved graphs are loaded by `SchemaLibraryLoader`, validated,
compiled and executed normally. `ObservationService` reads every named output
without changing the model. Sample n appears at Scope time (n+1)/F_s.
The F64 budget is `1e−10 × max(1, |reference|)` in each channel's units;
timestamp budget 1e−12 s plus absolute integer-nanosecond rounding.

**SP05:** h=[1,0.5,−0.25], input δ[n−4]+bδ[n−6], b=−0.5, 20 Hz, 2 s,
40 samples/channel. Three drawn Delay/Adder networks compare filtering the
combined input with summing separately shifted responses. All six Delay
initial values are zero. The independent oracle uses finite convolution and
separately expanded shifted kernels, not another executed S7 graph.

- Baseline output at n=4…8: **1, 0.5, −0.75, −0.25, 0.125 V**; residual zero.
- Edit b=0.75: the overlap/tail becomes **0.5, 0.375, −0.1875 V** at n=6…8.
- Negative case: set only `output_d2.initial_value=1`; output and residual
  become **−0.25 V at n=0**. This is remembered prehistory, invalidating the
  zero-state convolution comparison without invalidating runtime execution.
- All tested samples matched exactly in this build; a fresh run repeated
  identically. This is not a cross-platform bitwise claim.

**SP07:** low-pass x′=2(u−x); resonant q′=v, v′=16(u−q)−b v; b=1.6, all
initial states zero. Explicit forward Euler at 64 Hz, 64 s, 4096 samples.
The source area is 1/64 V·s at 0.125 s: one unit-height discrete sample at n=8.
Independent geometric and complex-mode powers check all states, including
unplotted velocity. The discrete rational references are

```text
H_low(z) = 2h / [z−(1−2h)]
H_res(z) = 16h² / [(z−1)(z−1+b h)+16h²]
```

These include one and two causal state stages respectively. The first
nonzero low-pass value is **0.03125 V at n=9**; resonant voltage first reaches
**0.00390625 V at n=10**. At the saved rate, resonant peak gain is about
**2.99043 at 0.625 Hz** on the measured grid; b=3.2 lowers it to about
**1.44494 at 0.546875 Hz**. Both DC gains remain 1.

Direct complex finite sums Y/X at eight frequencies per filter are compared
with H, budget **1e−9 V/V**. Fresh runs repeat identically. The 128 Hz refinement
keeps pulse area fixed, so pulse height becomes 2 V at n=16; transfer
normalization still holds. Complex error against the continuous transfer at
0.5 Hz decreases as follows:

| Filter | Error at 64 Hz | Error at 128 Hz |
| --- | ---: | ---: |
| Low-pass, 2/(s+2) | 0.0112445 | 0.00559054 |
| Resonant, 16/(s²+1.6s+16) | 0.133383 | 0.0651638 |

These are finite frequency-reference errors, not a universal accuracy score.
Maximum discrete sample residual in the stable profiles was **2.77556e−16**.
At 8 Hz, the physical resonant filter remains stable but Euler's pole modulus
is sqrt(1.05)>1. The deliberately growing discrete trace still matches its
own independent reference within the scaled budget (maximum absolute
residual **1.66983e−9**). A separate 2 s truncation of the stable capture gives
a resonant DC estimate **0.818323**, demonstrating missing-tail bias.

### Runtime-to-viewer checks

`educationLti` opens each saved diagram, follows its actual visible offline
guide link, accepts the saved run without repairing its settings, performs
the named F4 edit, saves, reopens and reruns. Every journal sample and full
Scope recording is checked against the independent formulas. Time-series
names, V units, timestamps and axes are verified.

SP07's dense Scope display intentionally retains a pixel envelope. The test
waits for the completed drawing, verifies every retained point is the correct
recorded sample, and checks endpoints and global extrema against the full
recording. SP05's small traces retain all 40 points. The initial harness
assumptions that all 4096 points must be drawn and that the final drawing is
synchronous with recording completion were corrected; no viewer algorithm
change was needed for these cases. The independent phase oracle also bounds
the inverse-log frequency at Nyquist to avoid a rounding-induced branch jump.
Its endpoint behavior is checked headlessly.

For **both SP07 response viewers**, baseline and damping edit:

- Saved analysis results contain **2049 complex bins**, 0…32 Hz at 1/64 Hz
  spacing. Every bin is compared with the independent H, budget 1e−9 V/V.
  Pulse height/index, response length, run identity and V/V units are checked.
- Every plotted Bode magnitude point is checked against **20 log10(|H|/r)**,
  budget **1e−7 dB**, at the frequency recovered from the log10 axis.
- Unwrapped phase uses an independent pole-factor reference, budget
  **1e−6 degrees**. Wrapped phase is checked modulo 360 and constrained to
  the principal interval. The endpoints are −180° (low-pass) and −360°
  (resonant) in the unwrapped representation.
- Changing gain reference **r=1→2** must give the proper dB shift. This
  changes presentation, not the measured complex transfer.
- The optional resonant **continuous reference** is enabled before its fields
  are edited: ω0=4 rad/s, ζ=0.2 (0.4 after editing), DC gain=1. Every orange
  magnitude/phase point is checked independently against G_res(s), budget
  1e−7 in the plotted quantity. Blue H(z) is rechecked with the overlay active.
  This reference is explicitly unsuitable for the first-order viewer.

Images under `build-release/s7studio/gui-artifacts/education-lti/` include
both diagrams/guides, baseline/edited Scopes, Bode captures, wrapped phase and
continuous-reference overlays. The images were visually inspected. Scopes
use manual zoom with **Fit full run** disabled for those captures; input and
output Scopes remain separate to avoid hiding the small filter outputs.
Some earlier failure artifacts may remain beside the final passing results.
This is software-rendered inspection, not a physical-display usability study
or a completed audit of every viewer. The final collection-wide audit reminder
remains open in [IMPLEMENTATION_PLAN](IMPLEMENTATION_PLAN.md#closing-reminder--viewer-audit).

### Executed verification

```bash
cmake -S . -B build-release
cmake --build build-release --target \
  s7studio s7studio_gui_workflows_test s7studio_gui_capture \
  s7studio_scope_view_test s7studio_spectrum_test s7studio_response_test \
  s7tests_education_lti s7tests_education_signals s7tests_education_linear \
  s7tests_education_foundations s7tests_education_sp06 \
  s7core_integrators_smoke s7core_model_time_smoke \
  s7tests_linear_examples_smoke s7studio_diagram_test \
  s7studio_model_time_test --parallel 2
ctest --test-dir build-release \
  -R '^(s7tests_education_(sp06|foundations|linear|signals|lti)|s7core_(integrators|model_time)_smoke|s7tests_linear_examples_smoke|s7studio_(diagram|model_time|scope_view|spectrum|response|gui_workflows|gui_education_(sp06|foundations|linear|signals|lti)))$' \
  --output-on-failure -V
python3 -B tools/check_example_diagrams.py
git diff --check
```

**19/19 selected CTest entries passed**, 199.68 s. Ordinary Studio workflows:
**109 passed, 0 failed, 112 skipped**. The 112 skips are the separately sharded
bundled-example release-render matrix, not successful renders. The dedicated
LTI GUI selection reported **4 passed, 0 failed, 0 skipped** (two lab rows
plus setup/cleanup). The subsequent continuous-overlay checks also passed;
final focused results are recorded below.

Final focused checks after the continuous-reference workflow and manual-zoom
capture refinement passed **3/3**, 9.48 s: headless LTI, dedicated LTI Studio
and the existing response suite. The complete ordinary workflow run above
preceded the additional continuous-overlay assertions; the final dedicated
selection includes them.

All **14 education diagrams** reproduced byte-for-byte from five generators.
The static index/geometry check covered **112 diagrams with zero issues**.
All relative links in the top-level README and fourteen guides resolved.
Final SHA-256 values:

```text
92db646b5f173e9589f15364cb659a333c7af1c3dfb3464185e3e3950785637b examples/education/sp05_convolution.s7diagram
90f717d41068cca2cdb4ba9773d6cb0f9ee5e58236ef9ba91059860f384bdcc3 examples/education/sp07_filter_transfer.s7diagram
dfeb5b96e0b6583c10a234e0decc895c409f008376b4d5a8d8e53b642e305d44 examples/education/generate_lti.py
32a422145c13a3564e773edeeb461b184e20da3b60292d180be81e078ddb4396 s7tests/education_lti_reference.hpp
3f6c8416c5023d9b4a670c1f9959c1e650752dadccb2b800dfbd939c499170e5 s7tests/education_lti_smoke.cpp
939e18d429df82f3995c21e87fdb750b74a3fe6f40143013c1fb16604be7cedc s7studio/tests/gui/workflows_test.cpp
```

MIT resource pages and relevant public teaching notes were inspected for
source alignment; no named textbook exercise, copied handout or solution key
is claimed. Runtime/compiler/validator semantics and vendor sources are
unchanged. The next batch is **SP08/SP09**, then SP10/SP11. Installed/relocated
packages, native Windows, full release rendering, two-host use, sanitizers,
maintainer review and final-candidate qualification remain open.

## SP08/SP09 — finite records, leakage and window normalization, 2026-11-19

The two original diagrams and linked student guides extend the collection to
**16 developed core labs**. Both saved profiles run for 2 s at 128 Hz:
256 samples per channel, four channels, 1024 recorded values. Source sample
n=0 is observed after step 1 at 1/128 s. Baselines, fresh repeated runs and
the prescribed edits are checked through the production loader, validator,
compiler, runtime and read-only observation service. The edits change SP08's
test frequency from 16.5 to 18 Hz and SP09's weak amplitude from 0.05 to 0.1 V.
Platform/build are the Linux Release configuration described above.

### Independent numerical references

Test references use closed-form cosines and a direct long-double DFT rather
than production FFT or window routines. A second finite geometric-sum
formula checks SP08's complex coefficients, including the negative-frequency
term. It covers the first 128 samples, all 256, the last 128 and the
boundary-inclusive 129-sample selection. Source and complex-coefficient
budgets are 1e−10; maximum observed source residuals were:

- SP08 baseline **1.51545e−14 V**, edited **2.13718e−14 V**.
- SP09 baseline and edited **4.26298e−14 V**.

SP08's 16.5 Hz, 1 V tone produces nearest-bin amplitudes
**0.629158798 V** and **0.644034823 V** in the first 128 samples. The full
256-sample record recovers a coherent 1 V peak at 16.5 Hz. Changing the
frequency to 18 Hz makes the first record coherent. Its offset mixture's
first-record mean is **0.2578125 V**, demonstrating that finite-record mean
is not necessarily the constant source's 0.25 V. Explicit mean removal is
checked separately.

SP09 independently constructs equivalent periodic weights using sine-squared
formulas. Rectangular/Hann/Hamming coherent gains are **1 / 0.5 / 0.54**;
mean-square weights are **1 / 0.375 / 0.3974**. At N=128 the equivalent noise
bandwidths are **1 / 1.5 / 1.362825789 Hz**. Moment and bandwidth budgets are
1e−12. The coherent calibration peak remains **2 V** and integrated power
**2 V²** for all three windows, within 1e−10. Omitting Hann coherent-gain
correction intentionally gives 1 V, exposing the normalization error.
Window-weighted Parseval is checked for all channels, both edits and mean
removal settings. The mixture's 24 Hz bin is **0.206230374 / 0.052550994 /
0.056471107 V**, illustrating leakage around a 0.05 V weak source.

### Viewer defect found and corrected

The initial real Studio test failed because Spectrum legend labels retained
the input unit `[V]` when displaying power or PSD. Their axis labels already
used V² or V²/Hz. `spectrum_view.cpp` now uses the displayed quantity's unit
for both single-input and multiple-input trace names, including hold names;
the input selector continues to identify the raw source unit. The regression
checks would fail against the previous labels. This is a presentation fix;
FFT, window algorithms and runtime semantics are unchanged.

### Runtime-to-viewer checks

The dedicated GUI workflow clicks each diagram's visible offline guide link,
accepts the saved run configuration, checks every journal/time-plot sample,
performs the named F4 edit, saves, reopens and reruns. It verifies literal
channel-to-element bindings, names, V units, model-time axes and current run
identity. Time plots retain all 256 samples per channel.

For baseline and edit, the actual Scope Spectrum controls exercise:

- SP08: first/full/late records and the boundary-inclusive N=129 record.
  Selecting From=1 s to To=2 s includes n=127…255: Δf=128/129 Hz and the
  last one-sided bin is 8192/129 Hz, below the 64 Hz Nyquist limit. Odd N
  therefore checks a distinct endpoint-doubling case.
- SP09: all three windows on N=128, Hann on N=256, explicit mean removal,
  and a separate single-input calibration PSD path.
- Every returned bin and plotted point in amplitude, power/bin and PSD,
  linear and dB displays with reference 1 and 2 and floor −120 dB.
  Numerical/display budgets are **1e−9** in linear quantity units and
  **1e−4 dB**; integrated power is checked against the independent
  window-weighted mean square. Frequency spacing/bounds, sample count,
  frame steps, window/mean settings, coherent gain and ENBW are checked.
- Quantity-specific axis/legend units, correct selected source, and unchanged
  functional schema after analysis. Viewer presentation state may change.

Retained artifacts are under
`build-release/s7studio/gui-artifacts/education-spectral/`. Both diagram
captures and the first/full SP08 amplitude and SP09 Hann power/PSD captures
were visually inspected. Their instruction links are visible and labels
match their quantities. This is Xvfb/software-rendered inspection, not a
physical-display usability study or a completed collection-wide viewer audit.
The [closing audit reminder](IMPLEMENTATION_PLAN.md#closing-reminder--viewer-audit)
remains open.

### Executed verification

```bash
ctest --test-dir build-release \
  -R '^(s7tests_education_(sp06|foundations|linear|signals|lti|spectral)|s7core_(integrators|model_time)_smoke|s7tests_linear_examples_smoke|s7studio_(diagram|model_time|scope_view|spectrum|response|live_spectrum|gui_workflows|gui_education_(sp06|foundations|linear|signals|lti|spectral)))$' \
  --output-on-failure -V
ctest --test-dir build-release \
  -R '^(s7tests_education_spectral|s7studio_gui_education_spectral|s7studio_gui_education_signals|s7studio_spectrum|s7studio_live_spectrum)$' \
  --output-on-failure -V
python3 -B tools/check_example_diagrams.py
git diff --check
```

**22/22 selected CTest entries passed**, 224.17 s. Ordinary GUI workflows:
**111 passed, 0 failed, 114 skipped**. Those skips are the separate bundled-
example release-render matrix, not successful renders. Dedicated spectral
GUI: **4 passed, 0 failed, 0 skipped** (two rows plus setup/cleanup).
After strengthening the single-input PSD assertions, the final focused
selection passed **5/5**, 22.10 s; the existing Spectrum suite reported
**33 passed, 0 failed, 0 skipped**. Full-run and focused logs are retained
locally as `/tmp/s7-spectral-regression.log` and
`/tmp/s7-spectral-final-check.log`.

All **16 education diagrams** reproduced byte-for-byte using the six
generators with `python3 -B`. The static check covered **114 diagrams,
zero issues**. All **162 relative links** in the top-level README and sixteen
student guides resolved. Final SHA-256 values for this batch:

```text
9566ffd2eddcf45f4b2a2d5a553ff5f620b35386a83909cff90a434294842bbc examples/education/sp08_dft_leakage.s7diagram
9474f85cb21e0100407a6ba09655e2471d6684ef912e2aee6599eeabcc4ea498 examples/education/sp09_window_normalization.s7diagram
a927094c2b74dea7b0d08e6343e18c79668859dd1774351852faf2b9e40a1e47 examples/education/generate_spectral_labs.py
66824bd3764c2480fe8b403fd8f0b77efe69364eface51cd246a71852634b56c s7tests/education_spectral_reference.hpp
04ee51e14d9b00bfc887341483465d7bd4c79900d15a7b39418b32f37f162179 s7tests/education_spectral_smoke.cpp
f141b76fac946af8af7fa6891f99799315cefcb8dfa2ef50b5ed21babebed1ce s7studio/tests/gui/workflows_test.cpp
cf1708d21ea46292bb6ab614d7d40789806a85c970bcf2a5c8f376d8f70ff105 s7studio/src/viewers/spectrum_view.cpp
```

MIT 6.003 lecture 18 and MIT 2.161 lecture 12 resource pages/notes were
checked for source alignment; graphs, exercises and references are original.
Eight of the 24 core labs remain. Next is SP10 after its finite-vector and
recording workflow is settled, then SP11. Installed packages, native Windows,
full release rendering, two-host use, sanitizers, maintainer review and
final-candidate qualification remain open. Lab status remains
**diagram-ready with passing development checks**.

## SP10 — convolution implementation verification, 2026-11-19

The new [scalar graph](../examples/education/sp10_circular_convolution.s7diagram)
and [student guide](labs/SP10-circular-convolution/README.md) bring the
collection to **17 developed core labs**. The saved profile is **2 s / 8 Hz**,
16 ticks, seven Scope channels and 112 journal values. All Delay states start
at zero. The word x=[1,2,−1,1] V is built from explicit pulse/Delay/summer
networks; h=[1,0.5,c] has one shared coefficient c=−0.25, edited to 0.5.
The isolated word starts at n=8; the periodic words repeat every 4 or 8 ticks.
The comparison block n=8…15 is observed at Scope times **1.125…2 s**, after
startup. This resolves the finite-sequence workflow without changing scalar
ports or execution/observation semantics. The graph does not pretend that
the separate frame processors are registered graph elements.

### Independent references and production convolution

The saved file is loaded through `SchemaLibraryLoader`, validated, compiled
and stepped with `NodeRuntime`; read-only snapshots check every input/output
and model timestamp. Both baseline and the prescribed edit repeat identically
in fresh runtimes in this process/build. Maximum graph residual was **0 V**
against the frozen 1e−12 V budget; these short dyadic-value cases are exactly
representable. No cross-platform bitwise claim is made.

Independent references deposit each product x[i]h[j] into a long-double
accumulator at i+j (linear) or (i+j) mod N (circular). A separately written
short direct DFT/product/inverse checks N=4/5/6/8 with raw forward transforms
and inverse division by N. Neither reference calls a production FIR/FFT.

| Profile | Linear, then zero extension to 8 (V) | Circular period 4 (V) |
| --- | --- | --- |
| c=−0.25 | [1,2.5,−0.25,0,0.75,−0.25,0,0] | [1.75,2.25,−0.25,0] |
| c=0.5 | [1,2.5,0.5,1.5,0,0.5,0,0] | [1,3,0.5,1.5] |

Period 8 recovers the complete linear result. Minimum sufficient padding
N=6 is tested independently of power-of-two convenience; N=5 still aliases
the final tail sample. The initial zero-history block is explicitly checked
as a failure of the periodic steady-state assumption. After the edit the
first circular sample happens to equal the first linear sample, so the
negative check uses the entire four-entry block rather than one coordinate.

The actual recorded word is passed to **both `FirConvolution` and
`FftConvolution`** in independent-frame mode; all six output entries, including
the tail, are checked. Folding their full outputs matches the circular graph.
In continuous-stream mode, all three recorded input rows are processed with
frame sizes 4 and 8 and compared with their graph outputs, including startup
and boundaries. Separate zero-frame flushing checks the full isolated-word
tail, then reset clears history while preserving the kernel. Processor/DFT
budgets are **1e−10 max(1,|reference|)**. The production APIs remain linear;
their FFT already pads internally. No circular/under-padded API was added.

The expanded **module** smoke test also checks one 72-sample complex signal
with frame sizes **1,2,3,4,6,8,9,12,18,24,36,72** and kernel lengths
**1,2,5,17,73**. Each partition is checked against a long-double pair-product
sum through the entire flushed tail. This covers kernels longer than frames,
empty-frame rejection between valid blocks, continuation, reset, independent
frames and interleaved instances. A hard-coded complex example detects
accidental conjugation/correlation. Existing matched-filter, buffered-frame
and invalid-input tests remain. Maximum absolute residuals were
**4.58286e−13** (FIR) and **5.68434e−13** (FFT), within the module's existing
2e−10 scaled budget. Its numerical comparator now rejects nonfinite outputs
explicitly; previously a NaN could evade the `error > tolerance` condition.
These checks found no arithmetic defect in either convolution processor.

### Viewer findings and fixes

The first real Studio workflow exposed a short-record limitation and a
misleading waiting state: the eight-sample selection was not adopted because
Scope/Spectrum had a 64-sample minimum, leaving N=4096 and reporting
`Filling: 8/4096` after the run had already finished.

- Spectrum now supports **N=2…65536**. Scope adopts the exact selected length,
  including odd lengths, in both current-journal and loaded-recording paths.
  One-sample selections request N=2 and get an explicit insufficient-data
  result. Correlation keeps its separate 64-sample minimum.
- Finished recordings shorter than N report **Unavailable** with instructions
  to select a smaller N or a longer interval/run. Active captures still report
  **Filling**. `SpectrumView::setFrame` also recognizes a change in finished
  status when the sample count is unchanged, so the message can update.
- A GUI regression submits eight active samples with N=16, marks that same
  frame finished without adding data, checks the unavailable state and clear
  plot, then selects N=8 and checks recovery and provenance.
- The same finished-record defect was present in Correlation. Its analyzer
  and view now distinguish active filling from a finished insufficient record
  and recognize completion without a sample-count change. The 64-sample
  minimum and correlation mathematics are unchanged. Headless and GUI cases
  check the finished transition and recovery from N=128 to N=64.
- Spectrum's new numerical cases cover N=**2,3,4,5,6,8,16,31,63**, real/IQ,
  all five periodic windows, mean removal on/off and amplitude/power/PSD.
  Independent direct DFTs and explicit window-series coefficients check every
  bin, odd/even folding, frequency coordinates, coherent gain, ENBW and
  integrated weighted power. Invalid N=0/1/65537 and insufficient finished
  records are checked explicitly. The Spectrum suite reports **51 passed**.

The newly linked convolution implementation article is now included in the
Help resources and topic list. The GUI workflow clicks both its actual visible
diagram link and the student-guide link and checks article contents. The MIT
reading remains an external web link. These are resource/navigation checks,
not an installed-package or live desktop-browser qualification.

### Studio coverage and limits

For baseline and edit, the dedicated workflow verifies all journal samples,
every plotted Time point, literal channel-to-element bindings, names, V units,
model-time axes and current run identity. The exact N=8 analysis frame has
steps 9…16; every selected raw value, returned amplitude bin and plotted point
is checked against independent formulas, including DC/Nyquist and Δf=1 Hz.
The coefficient is edited with F4, saved, reopened and rerun; analysis leaves
the functional schema unchanged. Time values are exact in this dyadic case;
the displayed Spectrum budget is 1e−10 V.

Artifacts are under
`build-release/s7studio/gui-artifacts/education-convolution/`. Diagram, guide,
baseline/edited Time plots and the selected short Spectrum were visually
inspected; the guide's prediction table uses compact headings to fit the Help
pane. Inspection uses private Xvfb/Mesa software rendering on the Linux
Release platform described above. It is not a physical-display usability
study or a completed collection-wide viewer audit. The
[closing audit reminder](IMPLEMENTATION_PLAN.md#closing-reminder--viewer-audit)
remains open.

### SP10 verification commands and reproducibility

```bash
cmake --build build-release --target s7tests_education_convolution s7core_convolution_smoke --parallel 2
cmake --build build-release --target s7studio s7studio_gui_workflows_test s7studio_pair_analysis_test s7studio_scope_view_test s7studio_response_test --parallel 2
ctest --test-dir build-release \
  -R '^(s7tests_education_(sp06|foundations|linear|signals|lti|spectral|convolution)|s7core_(convolution|integrators|model_time)_smoke|s7tests_linear_examples_smoke|s7studio_(diagram|model_time|scope_view|spectrum|pair_analysis|response|live_spectrum|gui_workflows|gui_education_(sp06|foundations|linear|signals|lti|spectral|convolution)))$' \
  --output-on-failure -V
python3 -B examples/education/generate_convolution_lab.py
python3 -B tools/check_example_diagrams.py
git diff --check
```

The initial broader run reported **24/25 passed**; the one failure was an
older cursor/export GUI test selecting FFT size by menu index 0, which changed
from N=64 to N=2 after adding short sizes. The test now selects **N=64 by item
data**, retaining its original 16 Hz-bin and 1000 Hz-axis-offset expectations.
This was a test-fixture assumption, not a cursor arithmetic defect. The final
selection additionally includes the Correlation suite after its lifecycle fix.

The **final 26/26 selected CTest entries passed**, **236.18 s**. Ordinary
GUI workflows reported **113 passed, 0 failed, 115 skipped**. Those 115 skips
are the separately sharded bundled-example release-render matrix, not
successful renders. The dedicated SP10 GUI selection reported **4 passed,
0 failed, 0 skipped** (lab, completion-state regression and setup/cleanup).
Spectrum reported **51 passed** and Correlation/XY **18 passed**, with no
failures or skips. Final log: `/tmp/s7-sp10-final-regression.log`.

All **17 education diagrams** reproduced byte-for-byte from seven generators
using `python3 -B`. The static index/geometry check covered **115 diagrams,
zero issues**. All **168 relative links** in the top-level README and seventeen
student guides resolved. These source checks do not imply installed-package
qualification. No vendor files, core convolution algorithms or runtime causal
semantics were modified.

Final SHA-256 values for this batch:

```text
c6eec248f4a00aa41955921a21907ba9552edfc81fe7cee4191ea7c2e4c27da9 examples/education/sp10_circular_convolution.s7diagram
8caf1b393ebffdf60f1b4b152c51d013c9c2b042ee408139c0f6a3ea881b4a95 examples/education/generate_convolution_lab.py
3d59f26acfdba7000ebf820e5c795d579b09a357bf856d1b26cbfee72e948298 s7tests/education_convolution_reference.hpp
02b26f7d5519e93c60dcd57734b0fad48bf6d873cffbd4ba8247a61aea31030b s7tests/education_convolution_smoke.cpp
8f665df2924c80869ce6f8957cf31b6b7f1c9a5dc7a04d51d0c4ec79c4bed801 s7core/tests/convolution_smoke.cpp
58c02b78b4b03280da31034b424208bb179104002e4111505d1dad3c4ea72aee s7studio/src/analysis/spectrum.cpp
1d7cf6c53cf52956b20894e887985e254cd0d8630be9ac7470dd5c0e85f41788 s7studio/src/viewers/spectrum_view.cpp
845a7bbaeff540d4593a568a6a1f9bf51afec0d5b312355cd2f47ad81cbd0ccf s7studio/src/scope_view_window.cpp
45cfcc1ece4e5824636803d114fc62b796c41a45caf9c81f58c42e17fdca0f0e s7studio/tests/spectrum_test.cpp
4722d2dbe91bd2aa1ad0819e733b853f58f8b22cb86a6876743e6ba2e3c9dd02 s7studio/src/analysis/pair_analysis.cpp
6245f38a43419077e6e40c5fe43379e637d875a27d280b811e5e6ea1cbddbe30 s7studio/src/viewers/pair_analysis_view.cpp
64b4f4c2743442a6cb47550dcbabe5feaf2d35c41420b08237ff785bc4713d0d s7studio/tests/pair_analysis_test.cpp
35b4f681b9a784a3580fd0d6a2d299d6e369318546914c025f842c37ec3d91c5 s7studio/tests/gui/workflows_test.cpp
```

The official MIT RES.6-008 lecture 10 resource page was inspected for topic
alignment; the dataset, graph and exercises are original. Seven core labs
remain, with **SP11 (STFT)** next. Installed/relocated packages, native
Windows, the full bundled-example release-render matrix, two-host use,
sanitizers, maintainer review and final-candidate qualification remain open.
Status remains **diagram-ready with passing development checks**.

## SP11 — STFT implementation and viewer checks, 2026-11-19

The original [burst/chirp graph](../examples/education/sp11_short_time_fourier.s7diagram)
and [student guide](labs/SP11-short-time-fourier/README.md) bring the
collection to **18 developed core labs**. The saved profile is **4 s at
128 Hz**, 512 ticks, three Scope channels and two single-input TimeFrequency
observers. The actual Studio journal has five bindings and **2560 values**.
The headless test also observes the dimensionless gate. All simulation uses
existing scalar sources/arithmetic; the STFT is read-only analysis, not a
runtime solver or an added frame-port contract.

### Frozen signal and independent transform contract

The 1 V peak 32 Hz cosine is gated for n=128…255. A 0.5 V discrete linear
chirp rises from 8 toward 40 Hz over the 512 samples. Its reference phase
sums the source's left-endpoint frequency increments analytically using
n(n−1), distinct from the continuous chirp integral's n² term. The primary
edit changes `stop.start_time` from 2 to 1.5 s, shortening the gate to
n=128…191 without changing the chirp. Source index n is observed after step
n+1 at model time (n+1)/128 s.

The exact saved file goes through the production loader, validation,
compiler, runtime and read-only observation. Every source sample and rounded
nanosecond timestamp is checked; a fresh baseline execution repeats
identically within this process/build. No cross-platform bitwise claim.
The independent STFT uses long-double direct complex sums and sine-squared
Hann weights, sharing no production FFT/window implementation. Its matrix
is frequency-major, separately checking the production time-major output and
Studio's transposition.

| Window N / hop H | Full columns | Δf | Center spacing | First observed center |
| --- | --- | --- | --- | --- |
| 64 / 32 | 15 | 2 Hz | 0.25 s | 0.25390625 s |
| 128 / 64 | 7 | 1 Hz | 0.5 s | 0.50390625 s |
| 64 / 16 | 29 | 2 Hz | 0.125 s | 0.25390625 s |

Both baseline and edit, all three signals, and an additional nonoverlapping
N=64/H=64 case are checked through the actual `s7spectral::spectrogram`
processor. Every complex coefficient, frequency, center time, full-support
flag, power/bin and PSD value is compared with independent formulas. Weighted
Parseval is checked per column. Source/coefficient/power budgets are 1e−10
in the respective units; timing budget is 1e−12 s. The measured source and
coefficient residuals are recorded with the final executed results below.

Other checks cover a 501-sample interval with a discarded incomplete end
window, one complete 64-sample window, and a shifted input interval/origin.
Hop zero, hop>N and insufficient input are rejected. Fully supported burst
windows recover a 1 V peak coefficient; a window crossing the onset is around
half that value. The duration edit removes later plateau windows. Every
other N=64/H=16 column matches N=64/H=32, showing that a smaller hop densifies
the same windows. The chirp ridge follows its independent discrete frequency
law within half the 2 Hz bin spacing (1.1 Hz allowance).

### Student controls and confirmed viewer fixes

- **Hop control:** Studio exposes H=1…N. Changing FFT size resets H=N/2,
  after which a custom value can be entered. The actual processor receives
  that hop; saved metadata and imports preserve it. CWT and read-only imports
  disable this irrelevant/unavailable control.
- **Boundary metadata:** STFT previously claimed zero extension, while the
  implementation returns full windows only and discards an incomplete final
  window. Metadata now states the actual behavior. CWT retains its distinct
  zero-extension/support-mask description. No STFT algorithm change was needed.
- **Slice units:** frequency slices previously omitted the quantity unit.
  Map/slice labels now agree for V, V², rad and V²/Hz.
- **Import consistency:** hop, rate/window-derived frequency coordinates,
  window-center coordinates and frozen-input window count are validated
  before replacing the displayed result. Five deliberately inconsistent
  files (hop 0, hop>N, shifted time, shifted frequency, truncated input) are
  rejected while the prior result and controls remain intact.
- **Retired plot geometry:** visual inspection found old magnitude/phase
  curves still drawn after changing the slice to PSD, although the numeric
  series already contained only PSD. Series are now hidden/emptied while
  attached, then detached on the GUI thread after scene synchronization;
  their existing scene-lifetime ownership protection is retained. The
  regression checks actual rendered strokes against an independent PSD
  polyline, with a 3-pixel geometric budget, as well as replacement of a
  hidden plot's old colored curve. This is separate from numeric-series checks.
- **Description usability:** the original instruction frame clipped its
  reading-link line. Its height was increased; all three links are visible.

An initial heatmap diagnosis was corrected after tracing its preparation:
the existing renderer already rasterizes numeric coordinates. The redundant
experimental resampler was removed. Final production heatmap rendering is
unchanged; only an accessor for the **last applied immutable grid** was added.
New checks verify numeric cells, actual colored pixels and cursor agreement,
including nonuniform coordinates on both axes. This is verified existing
behavior, not a claimed heatmap algorithm fix.

### Actual Studio path and review limits

The workflow opens the new saved graph, clicks its visible guide link, accepts
the saved 4 s/128 Hz profile, and checks every journal value and plotted Time
point, literal channel bindings, V units and model-time axes. Both maps are
checked at all three profiles above, in magnitude, squared magnitude, phase,
power/bin and PSD. It checks every returned coefficient and applied display
cell, coordinates, phase-threshold mask, map/slice units, every plotted slice
point, the raw input trace and current run provenance. Phase comparisons use
a 1e−6 rad modulo-2π budget; other displayed values use 1e−9 in quantity units.

The named F4 edit is saved, reopened and rerun. The map result is saved,
reopened after changing hop, and checked for exact coefficient retention and
restored H=16. Analysis does not change the functional graph schema.
Guide, graph, baseline/edited maps, phase masking, Time plots and the PSD
slice were visually reviewed under private Xvfb/Mesa software rendering on
the Linux Release platform described above. Artifacts are under
`build-release/s7studio/gui-artifacts/education-stft/`.

These development checks are not a physical-display student usability study,
installed-package/native-Windows qualification or the final collection-wide
viewer audit. The [closing audit reminder](IMPLEMENTATION_PLAN.md#closing-reminder--viewer-audit)
remains open. Six core labs remain; **CT04/CT05** are next, followed by
CT06–CT08 and SP14. Lab status remains **diagram-ready with development checks**.

### SP11 regression sensitivity and reproducibility

The rendered PSD check was exercised against a temporary restoration of the
original `LinePlotView::retire` implementation. The original implementation
failed `pixelsMatch()` in `educationStft`: stale strokes lay outside the
independent PSD polyline's 3-pixel budget. Log/artifacts:
`/tmp/s7-sp11-negative-render-final.log` and
`build-release/s7studio/gui-artifacts/sp11-retirement-negative-final/`.
The corrected implementation was restored and rebuilt immediately afterward.
The separate hidden-plot replacement check passed with both implementations;
it is additional coverage, not the defect's reproducer. Its pixel scan excludes
axis labels, whose font subpixel antialiasing can otherwise resemble colored
data pixels.

All **18 education diagrams** reproduced byte-for-byte from eight generators
using `python3 -B`. The static index/geometry check covered **116 diagrams,
zero issues**. All **173 relative links** in the top-level README and eighteen
student guides resolved. No vendor code, production STFT algorithm or runtime
causal semantics were modified.

The largest executed source residual was **4.71845e−16 V** (baseline) and
**3.33067e−16 V** (edited). Across the baseline/edit, complete/incomplete,
single-window and shifted-interval cases, the largest complex coefficient
residual was **2.96723e−16 V**, below the declared 1e−10 V budget.

Executed regression commands (from `s7l2/`):

```bash
cmake --build build-release --target s7tests_education_stft s7studio s7studio_gui_workflows_test s7studio_time_frequency_test s7studio_surface_test s7studio_viewers_test --parallel 2
ctest --test-dir build-release \
  -R '^(s7tests_education_(sp06|foundations|linear|signals|lti|spectral|convolution|stft)|s7core_(convolution|integrators|model_time)_smoke|s7tests_linear_examples_smoke|s7studio_(diagram|model_time|scope_view|spectrum|pair_analysis|response|live_spectrum|time_frequency|surface|viewers|window_lifecycle|live_oscilloscope|display_worker|gui_workflows|gui_education_(sp06|foundations|linear|signals|lti|spectral|convolution|stft)))$' \
  --output-on-failure -V
python3 -B tools/check_example_diagrams.py
git diff --check
```

Earlier regression executables were also relinked for the shared plot change:
`s7studio_response_test`, `s7studio_scope_view_test`, `s7studio_diagram_test`,
`s7studio_model_time_test`, `s7studio_window_lifecycle_test`,
`s7studio_live_oscilloscope_test` and `s7studio_display_worker_test`.

Final SHA-256 values for the SP11 artifacts and viewer checks:

```text
76332314df6a8a3e8fd93237713eefffd518e1fadc4e6a636e34179721baae87 examples/education/sp11_short_time_fourier.s7diagram
efee3d8be72159ab7ad7a297b93f06a97be9552c397eb6ca1c85bb9222719001 examples/education/generate_stft_lab.py
bbbc8e40c7505da27646f081cbb6cba32f292ee4497793eedf8f247fe24b24f9 s7tests/education_stft_reference.hpp
a6e9343dd9391420261bf217fa475a2ba8f0caf0a9f30b136f9cdee0f82ee875 s7tests/education_stft_smoke.cpp
0dd54d5b6435374e8127fce0977742eb75f43c6ca3814a4bf3a06f86767201fa s7studio/src/viewers/time_frequency_window.cpp
e76467ff8a1db2c1419a526f48558671997e7291e197f274b9146431e6fd8dde s7studio/src/viewers/line_plot_view.cpp
a518c422565a172595d0f28a8cbbbc609a4f49654f2ca61a966a69f09184a527 s7studio/src/viewers/line_plot_view.hpp
9dc7da28ac301af7ea0381e6320ec882f232a7add842bf992c130bac7724d364 s7studio/src/viewers/surface_plot.cpp
e2b61271a1798c7842265441cfb3709e9b45bcd18688529bc62b961d1982e490 s7studio/tests/gui/workflows_test.cpp
6478b70d7668d67b470d32329b33a8c6721738e924a92056f6a38b9c7f08ab2f s7studio/tests/surface_test.cpp
023efc55031a14c92e773e881fa2a9f42eba83eb68f03b6ead4d340699520e8e s7studio/tests/time_frequency_test.cpp
```

The **final 34/34 selected CTest entries passed**, **273.69 s** on the Linux
Release platform above. The ordinary GUI suite reported **115 passed,
0 failed, 116 skipped**; those skips are the separately sharded bundled-example
release-render matrix, not successful renders. The dedicated SP11 selection
reported **4 passed, 0 failed, 0 skipped** (lab, hidden-plot replacement and
setup/cleanup). The selection includes all eight education headless suites,
all eight dedicated educational GUI suites, and the affected plot, map,
surface, lifecycle and analysis regressions. Final log:
`/tmp/s7-sp11-final-regression.log`.

The final Studio build embeds the updated guides/evidence. Installed or
relocated packages, native Windows, the full bundled-example release-render
matrix and final collection-wide viewer review remain unqualified by this run.

## CT04/CT05 — controller states, load rejection and windup, 2026-11-19

Baseline: `e6e0b36` on `education/core-labs`; same Linux Release environment
as SP11. Two new original diagrams use bundled PI/PID schema definitions,
embedded with declared gains/limits/tracking. Extra read-only exposed outputs
publish the actual integral/filter/derivative states. No special C++ controller,
plant, global solver or runtime/observation mutation was added.

### Frozen contracts and numerical evidence

- **CT04:** 16 s/100 Hz, normalized unit 1, three identical first-order
  plants `y'=u−y−d`; P/PI/PID Kp=2, Ki=1.5/s, Kd=.15 s, τf=.1 s.
  All states start zero. Reference 0→.5 at source time 1 s, opposing load
  0→.2 at 8 s. Applied commands are clipped to ±1; tracking weight zero.
  PID differentiates filtered measurement, not reference. The primary edit
  Kd→0 makes PID and PI exactly equivalent over the full sampled run.
  The P lane additionally matches an independent geometric closed form,
  with equilibria 1/3 before load and 4/15 afterward. PI/PID final output
  is within .002 of .5. A 4 Hz variant deliberately destabilizes the
  explicit filter (`1−h/τf=−1.5`); clipping does not establish internal
  stability. Removing P-lane plant memory is rejected as an algebraic loop.
- **CT05:** 12 s/100 Hz, normalized unit 1, two first-order plants τ=.8 s,
  zero state, Kp=1.5, Ki=2/s, actuator ±1. Reference 0→1.5 at 1 s→.4 at
  4 s. Tracking is 6/s in aw and zero in free. At onset, demand 2.25 is
  limited to 1; aw's next integral is −.045 and free's .03. This explicitly
  checks the correction sign and old-state publication. The primary nested
  instance edit `aw/i_rate.weights [1,6]→[1,0]` makes both lanes identical
  in output, applied/unlimited command and integral, without editing the library.

Every scalar reference uses independent long-double equations, not another
S7 graph/controller implementation. The exact saved files pass the production
loader, validator, compiler and read-only snapshots. All sources, intermediate
rates, errors, states, commands and exposed controller outputs are checked at
100 and 200 Hz for baseline/edit; fresh executions repeat within this process/
build. The F64 budget is `1e−10×max(1,|reference|)` in signal units, time
1e−12 s plus checked nanosecond rounding. There is no cross-platform bitwise
or arbitrary continuous-time accuracy claim.

Largest ordinary-rate recurrence residual: **6.53362e−15**, in normalized
signal units. The deliberately growing coarse-filter variant has absolute
residual **5.14984e−5** on very large internal values, while satisfying the
same declared relative budget; it is an expected numerical failure example,
not counted as a physically accurate stable controller. CT05 fixed recovery
IAE over source indices [4Fs,8Fs) is **.273188 versus 1.97572** at 100 Hz,
and **.271969 versus 1.97478** at 200 Hz (aw versus free, normalized-unit·s).
The test requires aw below 60% of free for these frozen parameters.

### Student workflow and viewer review

The Studio test opens each guide through its actual visible description-frame
link and keeps the saved run settings. Literal expected bindings are checked
against all 10 CT04 / 9 CT05 recorded channels; every one of the 16,000 /
10,800 journal values and full Scope recording values is checked against the
independent reference. Every rendered-series point is an ordered exact member
of the recording, with first/last timestamps and extrema retained under display
reduction. Names, unit 1, model-time axes and full-run extents are checked.
The primary edit is performed through F4, saved, reopened and rerun. CT05
navigates into the actual embedded controller and back to the root tab.

Visual review found that dock Time previews always used the generic label
`Value (supplied units)`, even when their legends and full Scopes had a known
unit. They now show the common selected unit (including dimensionless 1),
explicitly distinguish mixed units, and preserve an unspecified-unit label.
The correction is limited to scalar Time preview labeling. Tests exercise
both Scope and LiveOscilloscope, same/mixed/missing units and selected subsets.
The original diagnosis of “unsupplied units” was a reading error; the actual
problem was a generic label rather than the available specific unit.

The new worksheets were compacted after visual review while preserving node
bodies, port rows and separate output/control/demand/state plots. Artifacts:
`build-release/s7studio/gui-artifacts/education-control/`. Native Windows,
installed/relocated packages, final-candidate and collection-wide viewer audit
remain open; status remains diagram-ready with recorded development checks.
Four core labs remain: CT06/CT07 next, then CT08 and SP14.

Visual review also identified a labeling limitation in dock Time previews:
the old axis always read **Value (supplied units)**, even when all selected
channels declared the same unit. It now shows the actual shared unit,
including **Value (1)** for these labs. Mixed and unspecified units remain
explicit; changing the channel subset changes the label. Focused presentation
checks cover Scope/Live types, 1, V, V/A, V/missing and all-missing cases.
The full Scope axes and recorded unit metadata already agreed; no runtime
unit conversion or numerical computation was changed. Worksheet geometry was
compacted after visual review, with all three description links retained.

All **20 education diagrams** reproduced byte-for-byte from **nine generators**.
The static geometry/index check covered **118 diagrams, zero issues**; all
**187 relative links** in the top-level README and twenty guides resolved.
The closing collection-wide viewer audit, installed/relocated packages and
native Windows remain open. Four core lessons remain: CT06–CT08 and SP14.

The nested PI instance now has its own original compact eight-block drawing,
so the correction path and i_rate fit on its page. Screenshots were reviewed
for root graphs, guides, baseline/edited output/control/demand/state plots,
preview labels and the actual nested edit. Artifacts:
`build-release/s7studio/gui-artifacts/education-control/`.

Executed commands:

```bash
cmake -S . -B build-release
cmake --build build-release --target s7tests_education_control s7studio s7studio_gui_workflows_test s7studio_observer_presentations_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_control|s7studio_gui_education_control|s7tests_control_examples_smoke|s7studio_scope_view|s7studio_window_lifecycle|s7studio_observer_presentations|s7studio_gui_workflows)$' --output-on-failure -V
cmake --build build-release --target s7studio_scope_view_test s7studio_window_lifecycle_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_control|s7studio_gui_education_control|s7studio_scope_view|s7studio_window_lifecycle|s7studio_observer_presentations)$' --output-on-failure -V
python3 -B examples/education/generate_control_labs.py
python3 -B tools/check_example_diagrams.py
git diff --check
```

**7/7 selected CTest entries passed**, 203.42 s; ordinary GUI **117 passed,
0 failed, 118 skipped**. Those skips are the separately sharded bundled-example
render matrix, not successful renders. After the final nested drawing and
relinking the Scope/lifecycle tests against the current viewer library,
**5/5 focused entries passed**, 10.43 s. The dedicated controller GUI suite
reports **4 passed, 0 failed, 0 skipped** (two labs plus setup/cleanup).
Logs: `/tmp/s7-control-final-regression.log` and
`/tmp/s7-control-final-focused.log`.

All 20 diagrams reproduced byte-for-byte from nine generators, the static
check covered **118 examples with zero issues**, and **187 relative links**
in the top-level README and twenty student guides resolved. No vendor code
or runtime/compiler/validator semantics changed. Native Windows, installed/
relocated packages, the complete bundled-example release-render matrix and
final collection-wide viewer review remain open. Four core labs remain:
CT06–CT08 and SP14. Status remains diagram-ready with development checks.

Final artifact SHA-256 values:

```text
efe7dbb6e12aade4087fbbb5b2b55fb29eb8fd94833a6cc179b8ff8e837b85aa examples/education/ct04_controller_comparison.s7diagram
0a6e21027c98ca9dc14578071ff64cc8f4ad9611f688f48e2db8b7b069842351 examples/education/ct05_actuator_windup.s7diagram
29717dab29b9e4801cb9113cb4be61b91ddb851fe5d3a75fe688465530fbe6de examples/education/generate_control_labs.py
c3153618b6d885c04ab7e52e444ffc4c35e779e6b34259106ed479a17c51d084 s7tests/education_control_reference.hpp
4a75a940c311ec66c5f692193c6ab16ec45f46c91fa4dedef977e4ac329a1ddf s7tests/education_control_smoke.cpp
e2951961c3f00c2ba2917c2b50110d10deeaffd160e18c8ffc8274ad6338b635 s7studio/src/viewers/observer_presentation.cpp
e2ad9a7ba280653f864daa2f4106cdf246d05f672820e9b67bed4e73e8300c5f s7studio/src/viewers/observer_presentation.hpp
970a3a68fc9a2e58f516da37098334e05da0316c9cf0c5ea431365dd83fed0cb s7studio/tests/observer_presentations_test.cpp
2910da3ce564222ca6fdd11576b604a5d2bb45fdd4f453c168f1f0470098a634 s7studio/tests/gui/workflows_test.cpp
```

## CT06/CT07 — sampled poles and stability margins, 2026-11-19

Baseline: `3c4a52a`; original new two-lag graphs, with ideal unlimited
commands explicitly distinguished from CT05's bounded actuator. At h=.1 s,
x_next=.9x+.1u and y_next=.9y+.1x use the same old states. Closed feedback
u=K(r−y) has poles .9±.1i√K. The independent oracle evaluates complex mode
powers, not an S7 reference graph or second invocation of the runtime.
Every source, command, error, intermediate rate and state is checked against
that closed form, with exact transition and rounded nanosecond conventions.
CT06 covers K=0,1,4,16,19,25, checks the actual complex mode's per-step
rotation/growth, and keeps marginal/growing cases explicit. The primary
F4 edit K=4→16 is saved/reopened/rerun.

CT07 contains separately observed open-loop pulse and closed-loop step paths
with the same gain/plant. Its independent pulse formula is
h[m]=.01K(m−1).9^(m−2), m≥2, and rational transfer L(z)=.01K/(z−.9)^2.
The production transform of the exact captured graph's output is checked at
every frequency, including DC/Nyquist. The 640-sample capture has its pulse
at n=8, 632 retained response samples and a 1024-point evaluation grid.
Analytic phase crossover is acos(.9) rad/sample, f_pc=.717831 Hz, with
L(f_pc)=−K/19. Independently calculated values:

| K | GM factor | PM degrees | f_gc Hz |
| --- | --- | --- | --- |
| 4 | 4.75 | 49.2486 | .290981 |
| 16 | 1.1875 | 4.77603 | .654346 |
| 25 | .76 | −7.64511 | .831290 |

The margins agree with actual closed-loop stability for this declared stable
open-loop plant. The 24-sample truncated capture deliberately fails the
complete-tail DC estimate. No automatic generic margin/stability certification
was added to the viewer. The guide distinguishes sampled and continuous
models, Hz and log10 Hz, dB reference one, wrapped versus unwrapped phase,
positive-frequency Nyquist branch versus full contour, and nearest-bin
inspection versus an analytic continuous crossover.

Ordinary stable-mode residuals are at most 1.15463e−14; marginal K=19 gives
1.73639e−13, and the growing 64 s K=25 variant approximately 1.9e−5 absolute
on very large states, all within the frozen `1e−10×max(1,|reference|)` budget.
The largest complex transfer residual is 7.32411e−15 against 1e−10. Coordinates
use 1e−12 s/Hz and displayed phase uses 1e−8 degrees. Fresh CT06 runs repeat
within this process/build; no cross-platform bitwise guarantee is claimed.

Studio checks literal bindings, every recorded/Scope value, all displayed
points/extrema, units and axes. The response result and each Bode/Nyquist
series point are compared with the independent rational function. The
inspector is checked against its actual nearest bin and result save/reopen
retains the complex matrix. Root graphs, guides, Bode/Nyquist and baseline/
edited Time captures are reviewed under Xvfb/Mesa, not a physical-display
student study. An initial round-trip fixture waited for a hidden view's
deferred display; it was corrected to show the view before waiting, with no
production change to its intentional visibility/cancellation policy.

Executed commands:

```bash
cmake -S . -B build-release
cmake --build build-release --target s7tests_education_loop --parallel 2
cmake --build build-release --target s7studio s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_loop|s7studio_gui_education_loop|s7studio_response|s7tests_education_control|s7studio_observer_presentations)$' --output-on-failure -V
python3 -B examples/education/generate_loop_labs.py
python3 -B tools/check_example_diagrams.py
git diff --check
```

The final focused selection passed **5/5 CTest entries**; the dedicated GUI
selection passed **4 Qt checks, zero failures/skips** (two labs plus setup/
cleanup). Log: `/tmp/s7-loop-final-focused.log`. All **22 education diagrams**
reproduced byte-for-byte from ten generators; **120 examples** passed the
static check with zero issues. **199 relative links** in the top-level README
and twenty-two student guides resolved. No production runtime or viewer
algorithm changed in this batch. Native Windows, installed/relocated packages,
the complete release-render matrix and collection-wide viewer audit remain
open. Two core labs remain: CT08 and SP14. Status remains diagram-ready
with development checks.

Final SHA-256 values:

```text
bd2f4c7f69ed7c996ace452672dd6293fcdef3c97de34b672f5e423fd0fa7a61 examples/education/ct06_closed_loop_poles.s7diagram
c34bf633f018bb521c22e3876e9fcebde9137bb27958510fdd9e1d92a5f5c32e examples/education/ct07_stability_margins.s7diagram
66aaf9534eda82cdcedd795cb6f9d18bf888406052b5b0e8fbd5786041ca2ed9 examples/education/generate_loop_labs.py
b70214385fd41eae94524752f30606e59ff860bd610325f5ceea372c0cfeee32 s7tests/education_loop_reference.hpp
de9a45dcc92dca545908296f4b3de17be6d0e69fed37629ca461bdc5d2b0da60 s7tests/education_loop_smoke.cpp
1a169c0878044bf4f376abbaf5e17703b9aa40a1c9ba190f77e6c8e6514b30e1 s7studio/tests/gui/workflows_test.cpp
```
