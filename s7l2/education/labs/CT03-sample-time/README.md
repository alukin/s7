# CT03 — Can a slow sample rate invent growth?

**30 minutes · Prerequisites: [FN06](../FN06-pole-maps/README.md),
[CT01](../CT01-first-order/README.md).**

**Your question:** can changing only the sample interval turn decay into growth?
The physical model remains `dx/dt=−x/(1 s)`, x(0)=1 V, with exact positive
decay exp(−t). A new explicit Gain/Integrator network represents it.

## Predict the saved run

Open **education → ct03_sample_time.s7diagram**, click **Run**, retain
**10 s at 2 Hz**, then open **Compare the named signals**. At h=0.5 s,
forward Euler has pole p=1−h=0.5 and `x[n]=0.5^n V`.

| n | Physical state time (s) | Scope time (s) | State (V) |
| --- | --- | --- | --- |
| 0 | 0 | 0.5 | 1 |
| 1 | 0.5 | 1 | 0.5 |
| 2 | 1 | 1.5 | 0.25 |

Calculate the next value before running. Compare with exp(−n h), not
exp(−t_Scope): the first plotted sample is the stored initial state.
Subtract the current h from Scope time for a physical-time comparison.

## Change the run rate, keep the physical model

Click **Run** again. Change **frequency 2 → 0.4 Hz**, keep **10 s**.
Confirm the intentional **four-sample run**. Predict first:

| n | Physical state time (s) | Scope time (s) | Euler state (V) |
| --- | --- | --- | --- |
| 0 | 0 | 2.5 | 1 |
| 1 | 2.5 | 5 | −1.5 |
| 2 | 5 | 7.5 | 2.25 |
| 3 | 7.5 | 10 | −3.375 |

Save your own copy, reopen it and rerun with its saved **0.4 Hz** setting.
The derivative gain remains −1. The EulerIntegrator uses the selected h,
so its pole becomes −1.5 automatically. Unlike FN05's fixed Delay/Gain
realizations, no pole coefficient needs a manual redesign here.
Negative alternating values contradict the physical positive decay;
coarse forward Euler has caused numerical instability.

For this model decay requires 0<h<2 s, or frequency >0.5 Hz.
Exactly 0.5 Hz is the marginal alternating boundary, not decaying stability.
More elapsed time at 0.4 Hz will magnify the problem, not fix it.
Finite runs contain floor(duration/h) samples, including the initial state.

## Restore and refine

Restore **2 Hz**. Then try **4 Hz** (h=0.25 s), retaining 10 s.
Compare with exp(−n h) at matching physical times. Smaller h improves
accuracy: the test bounds saved-run physical error by 0.12 V and the
refined error by 0.06 V. The unstable edit is instead checked against its
exact **discrete** sequence; no small physical-error claim applies there.

The headless test verifies all 20 baseline samples, four unstable samples,
40 refined samples and a fresh repeat. The GUI test changes the actual
Run Configuration, saves, reopens, reruns and checks recorded values.

## Run the independent checks

Use a current Linux S7/Studio development build; no hardware or plugin is
needed. The [saved graph](../../../examples/education/ct03_sample_time.s7diagram)
is loaded by the production schema loader, validated, compiled and sampled
through the read-only observation API in the
[headless test](../../../s7tests/education_linear_smoke.cpp).
The [reference formulas](../../../s7tests/education_linear_reference.hpp)
are test-only; the runtime executes the drawn causal graph.

```bash
cmake --build build-release \
  --target s7tests_education_linear --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_linear$' --output-on-failure -V
```

The finite-run F64 sample budget is `1e−10 × max(1, |reference|)` in each
channel's units. Timestamps must agree within 1e−12 s, with separately
checked integer-nanosecond rounding. Physical-reference envelopes are the
lesson-specific bounds stated above; discrete agreement alone does not
establish physical accuracy. Studio additionally checks guide links, the
primary edit, units, samples and save/reopen/rerun.
Status: `diagram-ready`; [development evidence](../../EVIDENCE.md) is
separate from release qualification. Installed packages and native Windows
remain unqualified.

## MIT material and the next lesson

[MIT 6.003 lecture 7: Discrete approximation of continuous-time systems](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec07/)
provides public teaching material for this topic (resource page checked
2026-11-19). This original S7 experiment does not reproduce an MIT problem
or imply MIT endorsement. Its mathematical references are derived above;
no MIT handout or solution key is bundled.

The yellow diagram frame opens this offline guide; its MIT link opens the
online material. MIT links can also be opened in the external browser
through Help. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [CT02](../CT02-damped-modes/README.md).
Next: [SP06](../SP06-three-tap-fir/README.md).
[Return to the learning route](../../../examples/education/README.md).
