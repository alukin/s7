# CT07 — How much gain or phase reserve does this loop have?

**60 minutes · intermediate · prerequisites: CT06, SP07/SP08.**
Original S7 open-/closed-loop comparison aligned with MIT stability-margin
teaching; readings checked 2026-11-19. Current 0.2.0 development source,
Linux Studio or headless C++; no hardware, plugin or network required.
Status: diagram-ready; see [development evidence](../../EVIDENCE.md).

Predict gain/phase crossovers, interpret a measured Bode/Nyquist response,
and cross-check margins against the actual sampled closed-loop poles.

## Predict from the exact discrete model

The plant consists of two zero-initialized, forward-Euler unit-time-constant
lags, h=0.1 s. The **ideal unlimited** proportional controller has gain K.
All signals and the transfer ratio are normalized, dimensionless (unit 1).
The open-loop transfer includes both causal state delays:

**L(z)=0.01K/(z−0.9)²**; on the unit circle `z=exp(iθ)`, **θ=2πf/10**.

This is not the continuous response K/(1+s)², nor the closed-loop transfer
L/(1+L). FrequencyResponse observes the **open-loop** pulse path. A separate
closed-loop step path tests the same K and plant in feedback.

| K | Gain crossover f_gc | Phase margin | Phase crossover f_pc | Gain margin |
| --- | --- | --- | --- | --- |
| 4 | ≈0.2910 Hz | ≈49.2° | ≈0.7178 Hz | 19/4 = 4.75 (≈13.534 dB) |
| 16 | ≈0.6543 Hz | ≈4.8° | ≈0.7178 Hz | 19/16 = 1.1875 (≈1.493 dB) |
| 25 | ≈0.8313 Hz | ≈−7.6° | ≈0.7178 Hz | 19/25 = 0.76 (≈−2.384 dB) |

Rounded values are predictions, not exact grid-bin readings. For the exact
calculation, `θ_gc=acos((1.81−0.01K)/1.8)`,
`PM=180°−2 atan2(sin θ_gc,cos θ_gc−0.9)×180°/π`.
The −180° crossing satisfies `cos θ_pc=0.9`, so
`f_pc=10 acos(0.9)/(2π)` and `L(f_pc)=−K/19`; GM=19/K.
GM is a **multiplicative gain factor**; its dB value is 20 log10(GM).
Positive PM/GM above one agree with the CT06 condition K<19 for this plant.
These formulas assume the stated stable open-loop plant and positive gain.

## Capture the complete pulse response

Open [ct07_stability_margins.s7diagram](../../../examples/education/ct07_stability_margins.s7diagram)
from **File → Example Diagrams… → education** and keep **64 s at 10 Hz**.

- Open **Open-loop pulse and complete tail** (`pulse_scope`). The unit-height
  pulse is at n=8 (source time 0.8 s, Scope time **0.9 s**); it has area
  h=0.1, not unit area. First nonzero second-lag output is n=10, plotted
  at 1.1 s. Its discrete response is `0.01K(m−1)0.9^(m−2)` for m=n−8≥2.
- Open **Closed-loop step** (`scope`): reference is 0.5 from n=10, plotted
  at 1.1 s. Compare its ringing/decay with the pole prediction.
- Open **Open-loop Bode / Nyquist** (`response`). Keep **gain reference 1**,
  **Unwrap phase** enabled, and the optional continuous reference model off.
  A changed display reference shifts the dB curve; 0 dB then no longer
  means |L|=1 unless the reference is one.

The full captured pulse tail is used: 632 samples from n=8, zero-padded to
1024 only for transform evaluation. There are 513 nonnegative-frequency bins,
**Δf=10/1024 Hz**, from DC through Nyquist **5 Hz**. Zero-padding makes a
denser evaluation grid; it does not recover an unrecorded response tail.

## Read Bode and Nyquist without mixing conventions

Bode's x coordinate is **log10(f / 1 Hz)**, not f in Hz and not angular
frequency. DC is omitted because log(0) is undefined. Gain is
**20 log10(|L|/1)** dB; phase is unwrapped in degrees and approaches −360°
at Nyquist. A wrapped +180°/−180° jump is not a physical extra delay.

The **Nyquist** tab plots Re(L), Im(L) for the captured **nonnegative-frequency
branch**, including DC; it is not a complete automatically mirrored contour.
For this real-coefficient loop the other branch is its complex conjugate.
The critical calculation point is (−1,0). Compare the measured negative-real
crossing −K/19 with −1; a phase crossover is not a zero of L.
The viewer does not automatically certify margins or plot closed-loop poles.
Use the formulas/table and CT06's pole check, rather than inferring stability
from the shape of one branch alone.

The frequency inspector accepts Hz and selects the **nearest actual bin**.
Read its reported frequency before comparing with the analytic crossover.
Bin spacing limits a visual estimate; do not report the continuous crossover
as if it had been sampled exactly.

## Change gain, save and rerun

Select **gain**, F4, change **value 4 → 16**, save a copy, reopen and rerun.
The magnitude curve rises by **20 log10(4)≈12.0412 dB**; phase and phase
crossover are unchanged. Gain crossover moves upward, PM shrinks and the
closed step rings longer. Save/reopen a `.s7analysis` result to retain the
measured complex response, units and run provenance.

For the declared unstable contrast K=25, the open-loop pulse response remains
stable while the **closed** loop grows. Negative PM and GM<1 describe that
loss of reserve for this particular loop, not a failure of open-loop analysis.
Restore K=4 after exploring. Truncating the pulse capture at n=23 gives a
strongly biased DC response; a smooth zero-padded plot does not fix it.

## Verify independently

The [headless test](../../../s7tests/education_loop_smoke.cpp) observes the
exact saved graph and checks every state against independent complex mode
powers / finite pulse formulas. The production response transform is checked
at every bin against the independent rational L(z), including DC/Nyquist.
Analytic crossover substitutions and margins must agree with actual closed
pole stability for K=4,16,25. The truncated-tail case is kept as a failure.

The GUI workflow checks literal channel bindings, every recorded value,
Time points/extrema and axes, every saved complex response value, and all
Bode magnitude/phase and Nyquist series points. It performs the primary
edit/save/reopen/rerun and analysis-result round trip. Sample/complex budgets
are `1e−10×max(1,|reference|)` and 1e−10 respectively; displayed phase uses
1e−8 degrees. These are development checks, not native-Windows, installed-
package or final collection-wide viewer qualification.

```bash
cmake --build build-release --target s7tests_education_loop s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_loop|s7studio_gui_education_loop)$' --output-on-failure -V
```

## MIT reading and source rights

[MIT 16.06, Fall 2012, Lecture 23: Stability Margins](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_23/),
PDF pages 1–2, defines gain/phase margins and relates Bode and Nyquist views.
Its continuous-time examples differ from this declared sampled loop.
This is topic alignment; the model, data, equations and graph are original.
No assignment, figure or solution is copied. Diagram/generator: CC0-1.0;
guide: CC-BY-4.0. See [sources](../../SOURCES.md).
[Return to the learning route](../../../examples/education/README.md).
