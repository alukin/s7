# SP03 — Three tones, one sampled sequence

**35 minutes · Beginner · Prerequisites: [FN01](../FN01-unit-delay/README.md),
[SP01 spectrum instructions](../SP01-fourier-synthesis/README.md).**
Current Linux S7/Studio development build; no hardware or plugin required.
Status: `diagram-ready`; see [development evidence](../../EVIDENCE.md).

**Your question:** can a 10 Hz spectral peak identify the original tone?
Predict sample equality, break it with one frequency edit, then increase
the sample rate while keeping the original frequencies.

## Three different continuous functions

The new graph has three independent AnalogOscillators, each 1 V peak.
They represent hypothetical samples of these distinct functions:

```text
low(t)       = sin(2π × 10 t + φ) V
shifted(t)   = sin(2π × 74 t + φ) V
reflected(t) = sin(2π × 54 t + π−φ) V
φ = 0.375 radians; all three start at signal time t=0
```

The reflected source's phase is **π−φ**, not φ. At rate **F_s=64 Hz**,
74=64+10 and 54=64−10. At integer sample n:

```text
sin(2π(64+10)n/64+φ) = sin(2π10n/64+φ)
sin(2π(64−10)n/64+π−φ) = sin(2π10n/64+φ)
```

These identities prove equal sampled values, although the continuous
functions differ between sample instants. S7 generates the sequences;
there is no physical ADC or reconstruction filter in this experiment.
The Scope names these roles **reference**, **shifted**, **reflected** and
**difference**. These labels stay accurate when you edit a frequency.
An Adder explicitly subtracts low from shifted to expose the residual.

## Predict the overlay

Open **Help → Education: start here → sp03_aliasing.s7diagram**.

| n | Signal time (s) | Scope time (s) | Each source (V) | Difference (V) |
| --- | --- | --- | --- | --- |
| 0 | 0 | 0.015625 | 0.366273 | 0 |
| 1 | 0.015625 | 0.03125 | 0.977179 | 0 |
| 2 | 0.03125 | 0.046875 | 0.719511 | 0 |

Calculate n=0 yourself. Click **Run**, retain **2 s at 64 Hz**, and
open **Compare the named signals**. There are **128 samples per channel**.
All three source traces overlap intentionally; use the legend to hide
channels and confirm each is present. Difference stays near zero within
floating-point rounding. A single visible curve does not mean data is missing.
For a detailed view, zoom to **0–0.25 s**. The straight lines between samples
are drawing aids; they do not reconstruct any of the three continuous tones.
Scope displays source sample n at `(n+1)/F_s`, so subtract **1/F_s**
when comparing against the functions above. This is observation timing,
not an extra delay in one path.

## Inspect the frequency evidence

Select **Current run**, analysis **From 0 s to 2 s**, click **Analyze**, open
**Spectrum**. Choose **Rectangular**, **N=128/full interval**, **All inputs**,
**Peak amplitude**, **Linear**, **Remove mean off**, frequency offset **0 Hz**.
Bin spacing is **0.5 Hz**, and the one-sided axis ends at **32 Hz**.
All three source channels have a **1 V peak at 10 Hz**; difference is near zero.
Their mean-square values are **0.5 V²**. Summing Power/bin, or integrating
PSD with Δf=0.5 Hz, gives this power.

At this rate, the plot cannot distinguish the original 10, 74 and 54 Hz
functions. The 10 Hz peak describes the sampled data. Zooming or increasing
FFT size cannot restore information lost by sampling. This is an important
limit on interpreting an otherwise correct viewer.

## Break the equality with one edit

Select **Shifted tone: edit frequency_hz** (`shifted`), press **F4**, change **frequency_hz
74 → 70**, save your copy, reopen and rerun at the same 64 Hz.
Predict a **6 Hz** alias, since 70=64+6. The reflected source remains identical
to low, while shifted and the difference change. The difference contains
both 6 Hz and 10 Hz components, each 1 V peak. F4 shows the actual frequency;
the teaching title does not rewrite itself.

This is a deliberate failure of the original equal-sample assumption,
not a runtime failure. The independent test checks the edited sequence,
the 6 Hz coefficient and the now nonzero residual.

## Resolve the tones by changing the rate

Restore shifted to **74 Hz**. Change Run frequency **64 → 256 Hz**, keep **2 s**.
All three frequencies now lie below Nyquist **128 Hz**; the sources follow
the new rate automatically. There are **512 samples**, with the first at
Scope time **1/256 s**. Analyze the full interval again, **N=512**.
Expect separate peaks at **10, 74 and 54 Hz**, each 1 V peak. Bin spacing
stays 0.5 Hz because the record duration stays 2 s.

The source values at n=1 now differ: the new run samples between the old
64 Hz instants. Compare functions at `t=n/256`, not at the old sample index.
This higher rate distinguishes these three known tones; it is not a guarantee
for signals with unknown higher-frequency content. In real acquisition,
an appropriate analog anti-alias filter acts **before** sampling. Adding
a digital filter afterward cannot identify which original alias was present.

## Independent checks and viewer correctness

The [headless test](../../../s7tests/education_signals_smoke.cpp) loads the
[actual graph](sp03_aliasing.s7diagram) with the
production loader, validates, compiles and samples through the read-only
observation API. Independent long-double evaluations check every value,
a fresh repeat, the edited frequency and the 256 Hz contrast. A direct complex
DFT checks the expected frequencies. Budgets: **1e−10 V** for samples and
normalized coefficients, **1e−12 s** for timestamps, plus nanosecond rounding.

```bash
cmake --build build-release \
  --target s7tests_education_signals --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_signals$' --output-on-failure -V
```

The Studio workflow independently checks journal values, visible time-series
points, units and names, full/selected interval bounds, all spectral bins,
amplitude/power/PSD normalization and save/reopen/rerun. A correct-looking
image alone is not the reference. Source links require the checkout;
the guide is bundled offline. Native Windows and installed packages remain
unqualified. No real-time or hardware acquisition claim is made.

## Reading and next steps

[MIT 6.003 lecture 21 — Sampling](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec21/)
explains sampling, spectral aliases and filtering before sampling (2011 notes,
checked 2026-11-19). This original S7 example uses independently chosen tones
and phases; it does not reproduce an MIT assignment or imply endorsement.
No MIT PDF or solution key is bundled. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Extension: choose another integer multiple of 64 to add to 10 Hz and predict
its samples. Preserve amplitude and phase when applying the identity.

Previous: [SP01](../SP01-fourier-synthesis/README.md).
Continue with [SP06 — three-tap FIR](../SP06-three-tap-fir/README.md).
Continue to [SP05 — shifted impulse responses](../SP05-convolution/README.md)
and [SP07 — filter transfer](../SP07-filter-transfer/README.md).
[Return to the learning route](../../index.md).
