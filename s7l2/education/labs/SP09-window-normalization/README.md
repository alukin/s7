# SP09 — Change the window, preserve the units

**50 minutes · Intermediate · Prerequisite: [SP08](../SP08-dft-leakage/README.md).**
Current Linux development build; no hardware or plugin required.
Status: `diagram-ready`; see [development evidence](../../EVIDENCE.md).

**Your question:** which normalization preserves amplitude, and which preserves
power? You will compare three periodic windows, recover a known coherent
amplitude and explain why a weak-tone bin can contain strong-tone leakage.

## A calibration and a demanding mixture

Open **File → Example Diagrams… → education → sp09_window_normalization.s7diagram**.
The original graph contains three cosine sources and a summer:

```text
calibration[n]   = 2 cos(2π × 20 n/128) V
strong tone[n]   = 2 cos(2π × 20.5 n/128) V
weak reference[n]= 0.05 cos(2π × 24 n/128) V
mixture[n]       = strong tone[n] + weak reference[n]
```

The sine oscillators use phase π/2. All signals are F64 volts. At n=0,
calibration and strong equal 2 V, weak equals 0.05 V and mixture equals
2.05 V, displayed at Scope time 1/128 s. At step k the signal index is k−1.
No window is applied in the runtime graph. Changing the analysis window
must preserve the recorded source data and the saved model.

Click **Run**, keep **2 s at 128 Hz**, then open **Record, then choose the analysis
interval**. The full recording has 256 samples/channel. Set analysis **From 0 s
to 1 s**, click **Analyze**, open **Spectrum**, use **N=128/full selected interval**,
**Real (one-sided)**, **All inputs**, **Remove mean off**, **Peak amplitude**,
**Linear**, frequency offset **0 Hz**, **Full frequency band**.
The grid spacing is 1 Hz and Nyquist is 64 Hz. The first selected interval
contains n=0…127 even though its first plotted timestamp is 1/128 s.

## Predict the window moments

For n=0…N−1, use these **periodic** windows:

```text
Rectangular:      w[n] = 1
Periodic Hann:    w[n] = sin²(πn/N)
Periodic Hamming: w[n] = 0.08 + 0.92 sin²(πn/N)
S1 = sum w[n]; S2 = sum w[n]²
coherent gain CG = S1/N
equivalent noise bandwidth ENBW = F_s S2/S1²
```

| Window | CG | S2/N | ENBW at N=128, F_s=128 |
| --- | --- | --- | --- |
| Rectangular | 1 | 1 | 1 Hz |
| Periodic Hann | 0.5 | 0.375 | 1.5 Hz |
| Periodic Hamming | 0.54 | 0.3974 | 1.362826 Hz |

Choose each Window in the Transform controls. The calibration's **20 Hz
peak remains 2 V** after coherent-gain correction. Without that correction,
Hann would misleadingly report 1 V. Its coherent tone spreads into the
neighboring bins; correcting amplitude does not turn the tapered main lobe
back into one rectangular-window bin. A symmetric Hann with denominator
N−1 is a different window and does not have exactly these moments.

## Inspect the weak-tone neighborhood

Predict the mixture's **24 Hz peak-amplitude bin** for the first half:

| Window | Mixture bin at 24 Hz (V peak) |
| --- | --- |
| Rectangular | 0.206230 |
| Periodic Hann | 0.052551 |
| Periodic Hamming | 0.056471 |

The weak source itself is only 0.05 V. Its bin in the mixture also contains
the strong off-bin tone's complex leakage. A lower sidelobe can make the weak
tone easier to distinguish, while a wider main lobe can make closely spaced
tones harder to resolve. No single window is best for every measurement.
Inspect the separate strong and weak channels to explain the mixture.
Magnitudes cannot generally be added: add the complex coefficients first.

For more dynamic range, in Display choose **dB**, reference **1** in the
selected linear units, display offset **0**, floor **−120 dB**. The strong
2 V coherent calibration is about **+6.0206 dB re 1 V**. The weak 0.05 V
coherent tone is about **−26.0206 dB re 1 V**. A display floor clips drawing;
it does not replace small or zero raw values in the analysis result.

## Keep amplitude, power and PSD distinct

For the unnormalized transform `X[k]=sum w[n]x[n]exp(−j2πkn/N)`:

```text
Peak amplitude = a[k] |X[k]| / S1                 (V)
Power / bin   = a[k] |X[k]|² / (N S2)            (V²)
PSD           = a[k] |X[k]|² / (F_s S2)          (V²/Hz)
a[k]=2 at interior real bins, 1 at DC/Nyquist
```

Change Quantity through all three modes. The coherent calibration's **total
power is 2 V²** for all three windows; its *peak power bin* changes as the
power spreads. Sum Power/bin, or integrate PSD using Δf=1 Hz. For a general
finite signal this yields **sum(w² x²)/sum(w²)**, not necessarily the raw
unwindowed mean square. With Remove mean on, replace x by x−mean(x) in that
weighted expression. Subtracting the raw mean before tapering does not in
general make the windowed DC bin zero.

Peak-amplitude correction uses S1; power normalization uses S2. Replacing one
with the other gives a plausible-looking but incorrectly calibrated result.
In dB mode, amplitude uses **20 log10(A/A_ref)**; power/PSD use **10 log10(P/P_ref)**.
The reference's units follow the selected quantity. Plot legends likewise
use V, V² or V²/Hz; the source selector retains the supplied input unit V.
In dB mode those legend units identify the underlying linear quantity used
by the reference, while the vertical axis explicitly says dB. Changing reference from
1 to 2 lowers amplitude by 6.0206 dB, power/PSD by 3.0103 dB. Linear values
are unchanged. The frequency axis stays in Hz.

## Change the weak tone

Select **Weak reference: edit amplitude** (`weak`), press **F4**, change
**amplitude 0.05 → 0.1**, save your copy, reopen and rerun. Predict: the weak
reference peak doubles, the strong channel and calibration are unchanged,
and the mixture changes. The mixture bin need not double because its strong-
tone leakage remains. All channel names describe roles and remain accurate.

As an extension, analyze **0–2 s**, N=256. The strong 20.5 Hz source becomes
coherent at bin 41, spacing 0.5 Hz. The periodic window gains stay the same,
but their ENBW in **Hz halves**. This uses twice as many actual samples,
not zero padding. Keep the interval and FFT size explicit when comparing.

## Independent verification and limits

The [headless test](../../../s7tests/education_spectral_smoke.cpp) executes the
[actual saved graph](../../../examples/education/sp09_window_normalization.s7diagram)
with production loading, validation, compilation and observation. It checks
every sample and fresh-repeat equality. Independent sine-squared window
weights, direct complex DFTs and finite sums verify CG, window energy, ENBW,
coherent amplitude, weighted Parseval, the weak-tone edit and the misleading
uncorrected-Hann result. No production FFT or window helper supplies the oracle.

```bash
cmake --build build-release \
  --target s7tests_education_spectral --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_spectral$' --output-on-failure -V
```

The GUI workflow separately checks the journal, actual plotted points,
selected interval, every spectral bin in all three quantities/windows,
units, dB references/floor and save/reopen/rerun. Budgets: **1e−10 V** for
source/normalized coefficients, **1e−9** in linear viewer units,
**1e−4 dB** for plotted dB, **1e−12** for window moments/ENBW, and **1e−12 s**
for timestamps plus nanosecond rounding. These are finite F64 Linux checks;
installed packages, native Windows and final release qualification remain open.
Source-code links require the checkout; this guide is bundled offline.

## MIT material and next steps

[MIT 2.161 lecture 12 — FFT, leakage and windowing](https://ocw.mit.edu/courses/2-161-signal-processing-continuous-and-discrete-fall-2008/resources/lecture_12/)
provides public teaching material on these topics (Derek Rowell, Fall 2008;
checked 2026-11-19). The precise periodic-window conventions and normalization
above are this S7 lesson's independently derived contract. No particular MIT
problem is reproduced, and no handout or solution key is bundled. This is
an original topic illustration, not an endorsed MIT resource.
Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [SP08](../SP08-dft-leakage/README.md).
Next planned: SP10 (linear/circular convolution), then SP11 (time-frequency).
[Return to the learning route](../../../examples/education/README.md).
