# CT08 — Can a lead network improve settling without changing DC gain?

**90 minutes · advanced undergraduate · prerequisites: CT06/CT07, FN01.**
Original S7 sampled compensator experiment, aligned with MIT lead/lag teaching.
Readings checked 2026-11-19. Current 0.2.0 development source on Linux, Studio
or headless C++; no hardware, runtime plugin or network required.
Status: diagram-ready; [development evidence](../../EVIDENCE.md) is separate
from release/platform qualification.

Predict the effect of a discrete pole/zero network, compare transient and
control effort at the same DC gain, verify its frequency response, and explain
why exact zero-state cancellation depends on the initialized memory.

## Predict before running

The plant is the same two-lag sampled model studied in CT06/CT07:

`x[n+1]=0.9x[n]+0.1u[n]`, `y[n+1]=0.9y[n]+0.1x[n]`, x[0]=y[0]=0.

Its zero-state transfer at **10 Hz**, h=0.1 s, is G(z)=0.01/(z−0.9)².
Two separate negative-feedback loops receive reference 0→0.5 at source
index n=10. One uses P gain **4**. The other uses the new reusable
**LeadLagController**, implemented as an ordinary schema network:

```text
e[n] = r[n] − y[n]
u[n] = b u[n−1] + K (e[n] − a e[n−1])
e[−1] = u[−1] = 0
C(z) = K (z−a)/(z−b)
K=16, a=0.8, b=0.2
```

All plotted quantities are normalized, dimensionless **unit 1**. Plant tau
inputs are **1 second**; a and b are **per-sample coefficients**, not physical
pole frequencies in rad/s. Changing rate while retaining these coefficients
changes the physical controller. This lesson's qualified profile stays at 10 Hz.
Both actuators are deliberately **ideal and unlimited**; CT05's limiter is
not included in this linear comparison.

| Quantity | P controller | Lead network |
| --- | --- | --- |
| DC controller gain | 4 | 16(1−0.8)/(1−0.2)=4 |
| Eventual output for r=0.5 | 0.4 | 0.4 |
| First command at n=10 | 2 | 8 |
| Nyquist-frequency controller gain | 4 | 24 |

Lead improves this chosen transient by redistributing gain/phase, not by
removing steady tracking error or limiting control effort. No universal
optimality or hardware-actuator claim is made.

## Construct, run and measure

Open [ct08_lead_lag.s7diagram](ct08_lead_lag.s7diagram)
through **Help → Education: start here**. Keep **64 s at 10 Hz**.

1. Trace the two closed loops and their separate plant states. The shared
   reference/parameter constants do not share plant or controller memory.
2. Open **Same DC loop gain, different transients** (`scope`): reference,
   P_output and lead_output. Both approach 0.4. Uncheck **Fit full run**,
   place the pointer near the first few seconds and wheel-zoom to about
   **0–10 s** (drag to pan if needed) to compare oscillation tails. Recheck **Fit full run**
   to restore the full capture. The From/To fields select an analysis interval;
   they are separate from Time-plot navigation.
3. Open **Control effort** (`command_scope`): inspect the initial demands
   2 versus 8 before calling the lead response an unqualified improvement.
4. Double-click **lead** to inspect its eight primitive blocks, including
   error_memory and control_memory. Return to the root tab before Run.
5. Open **Open-loop pulse and tail** (`pulse_scope`) and **Lead open-loop
   Bode / Nyquist** (`response`). This third, independent path receives a
   single unit-height pulse at n=8 and measures **C×G**, not the closed-loop
   response or the P baseline. Its memory also starts at zero.

Source/state sample n is observed after runtime step n+1 at Scope time
**(n+1)/10 s**. Thus the pulse is at 0.9 s and the step at 1.1 s. All right-hand
state updates use old values. The nested plant integrators already supply
causal memory; adding an outer delay would change the loop.

The lead loop's zero-state closed transfer has denominator
`(z−0.2)(z−0.9)²+0.16(z−0.8)`:

`z³−2z²+1.33z−0.29 = (z−0.5)(z²−1.5z+0.58)`.

Its response poles are **0.5** and **0.75±0.1322876i**, with largest magnitude
√0.58≈0.761577. The P baseline's magnitude is √0.85≈0.921954. This predicts
faster decay for the selected lead. At state time 8 s (Scope time **8.1 s**),
the development check requires lead's error from the equilibrium 0.4 below
1e−6, while the P response still differs by more than 1e−4.

## Inspect the frequency response

Use Bode gain reference **1**, unwrapped phase, optional continuous reference
model **off**. The measured response is

`L(z)=16(z−0.8)/(z−0.2) × 0.01/(z−0.9)²`.

The 640-sample capture has 632 samples from the pulse, evaluated on a
1024-point grid: 513 bins, 0…5 Hz, Δf=10/1024 Hz. Bode excludes DC and uses
**log10(f / 1 Hz)**, dB and degrees; Nyquist uses Re(L), Im(L) and shows the
nonnegative-frequency branch, including DC. Read CT07's discussion before
inferring a complete Nyquist contour or automatic stability certification.
A saved `.s7analysis` preserves the measured complex values and provenance.

An independent open-loop calculation convolves the controller's impulse
response `c[0]=K`, `c[m]=K(b−a)b^(m−1)` for m≥1 with the plant impulse
`g[m]=0.01(m−1)0.9^(m−2)` for m≥2. This separately checks the measured
frequency response and the actual drawn signal path.

## Change the zero, save and explain

Select **zero**, press **F4**, change **value 0.8 → 0.2**, save a copy, reopen
and rerun. With a=b and the specified zero memories, C(z) becomes **16**.
It therefore matches the analytic **P16** loop, not the displayed P4 baseline.
The eventual output rises to **8/17≈0.470588**, while the slower P16 ringing
returns. Initial demand remains 8. Descriptive titles remain unchanged; inspect
actual parameters rather than reading a title as a live equation.

Transfer cancellation does not delete stored state or justify arbitrary
nonzero initial memory. The reusable component exposes its previous error
and command for read-only observation; its internal pole remains b.
Restore zero=0.8 before comparing the original lead experiment.

For a DC-matched **lag** extension, set **scale=1, zero=0.2, pole=0.8**.
C(1) is again 4, but the phase contribution has the opposite sign and the
initial command is only 0.5. Its state and frequency responses are also
checked headlessly; do not assume the lead's settling result applies to it.

A deliberate failure is changing **lead_error** weights [1,−1]→[1,1].
That creates positive feedback. The graph remains causally executable, but
its output grows. This wrong-sign variant is verified independently; a
successful run or automatically rescaled plot is not evidence of stability.

## Independent checks and limits

The [headless test](../../../s7tests/education_compensator_smoke.cpp) loads
this exact saved graph with production loading/validation/compilation and
read-only observation. A [separate scalar recurrence and finite-convolution reference](../../../s7tests/education_compensator_reference.hpp)
checks every source, state and exposed memory for baseline, primary edit,
lag extension and wrong-sign case. The rational transfer independently checks
every production response bin. Module smoke tests additionally exercise the
actual registered reusable component, cancellation, unstable pole and illegal
direct feedback. No special C++ controller or global numerical solver is added.

Budget: `1e−10×max(1,|reference|)` for finite scalar values, 1e−10 for complex
responses, and 1e−12 s for time coordinates. Growing negative cases retain
that relative budget; large absolute residuals there are not reported as
ordinary stable-controller accuracy. GUI checks literal channel mappings,
every journal/Scope value, displayed points/extrema, units, Bode/Nyquist
values, primary save/reopen/rerun and analysis-result round trip.

```bash
cmake --build build-release --target s7core_schema_elements_smoke s7tests_education_compensator s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7core_schema_elements_smoke|s7tests_education_compensator|s7studio_gui_education_compensator)$' --output-on-failure -V
```

## MIT readings and rights

[MIT 16.06, Fall 2012, Lecture 25: Lead Compensation](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_25/),
PDF pages 1–2, explains finite pole/zero lead shaping.
[Lecture 14: Lag Compensation](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_14/),
PDF pages 2–4, discusses low-frequency gain and slow tails. Their continuous
examples and parameters differ from this explicit sampled model. This is
original topic alignment, not reproduction of a worked problem; no handout,
figure or solution is bundled. Diagram/generator: CC0-1.0; guide: CC-BY-4.0.

[Return to the learning route](../README.md).
