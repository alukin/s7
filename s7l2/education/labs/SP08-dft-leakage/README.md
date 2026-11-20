# SP08 — Where did the tone amplitude go?

**40 minutes · Intermediate · Prerequisites: [SP01](../SP01-fourier-synthesis/README.md),
[SP03](../SP03-aliasing/README.md).** Current Linux S7/Studio development build;
no hardware or plugin required. Status: `diagram-ready`; see
[development evidence](../../EVIDENCE.md) for checks and remaining qualification.

**Your question:** does the tallest FFT bin equal an off-bin tone's amplitude?
You will compare coherent and off-bin records, predict the effect of changing
the record duration, and distinguish finite-record mean from a physical offset.

## Read the new signal graph

Open **Help → Education: start here → sp08_dft_leakage.s7diagram**.
It contains two independent cosine sources and a known DC offset:

```text
coherent reference[n] = cos(2π × 16 n/128) V
test tone[n]          = cos(2π × 16.5 n/128) V
offset mixture[n]     = test tone[n] + 0.25 V
DC reference[n]       = 0.25 V
```

AnalogOscillator is a sine source; phase=π/2 radians makes each source a
cosine. All signals are F64 volts. The Adder combines test and DC; there is
no filter, resampling, integration or FFT computation in the runtime graph.
The Spectrum tab analyzes a read-only copy of recorded data.

Click **Run**, keep **2 s at 128 Hz**, then double-click **Record, then choose
the analysis interval**. There are **256 samples per channel**. At n=0 both
tones equal 1 V and the mixture equals 1.25 V, shown at Scope time **1/128 s**.
Sample n is evaluated at signal time n/128 and displayed at (n+1)/128.
The line segments between samples are drawing aids, not continuous reconstruction.

## Predict a one-second transform

In Scope's analysis controls choose **Current run**, **From 0 s**, **To 1 s**,
then click **Analyze** and open **Spectrum**. Choose:

- **Real (one-sided)**, **All inputs**, **Rectangular** window.
- **N=128 / full selected interval**, **Remove mean off**, **Peak amplitude**.
- **Linear**, frequency offset **0 Hz**, **Full frequency band**.

This selects steps 1…128, source indices n=0…127. Bin spacing is **1 Hz**;
Nyquist is **64 Hz**. The bin spacing is F_s/N, not 1/(last timestamp−first
timestamp): 128 sample positions represent the one-second DFT record.
The default analysis interval is only part of a run; enter the bounds explicitly.

Predict the 16 Hz coherent reference's peak: **1 V at bin 16**. For the
16.5 Hz test tone, the neighboring bins are only about **0.629159 V at 16 Hz**
and **0.644035 V at 17 Hz**, with energy spread into other bins. Its actual
amplitude is still 1 V. Neither bin measures that amplitude exactly.
This is finite-record leakage and scalloping, not attenuation by a runtime filter.
Use the input/legend controls to inspect one curve when overlays obscure it.

## Derive the finite result

Use the unnormalized DFT `X[k]=sum x[n] exp(−j2πkn/N)`. For a unit cosine
at normalized angle θ=2πf/F_s, let `S(a)=sum exp(j a n)` over n=0…N−1:

```text
X[k] = [S(θ−2πk/N) + S(−θ−2πk/N)] / 2
S(a) = exp(j a(N−1)/2) × sin(Na/2)/sin(a/2)
S(0 mod 2π) = N
```

Both positive- and negative-frequency terms matter. Approximating the peak
with one sinc lobe alone need not give the exact two bin values above.
For real data, peak amplitude is **2|X[k]|/N** in interior bins; DC/Nyquist
are not doubled. This is a sampled finite-record result, not a list of new
physical tones in the original signal.

## Change the record, keep the same signal

Set **From 0 s**, **To 2 s**, click **Analyze** again, and use **N=256**.
The same test source now completes **33 cycles**. Its 16.5 Hz peak is **1 V
at bin 33**, spacing **0.5 Hz**. The coherent reference is at bin 32.
The sample rate and Nyquist bound stay unchanged; the longer actual record
changed the grid. Increasing a zero-padding factor would not add observations.
This Scope path uses the selected recorded samples; it does not invent missing
samples when an FFT size exceeds the available interval.

As an interval check, select **From 1.0078125 s to 2 s**, Analyze, N=128.
These are the last 128 source values, n=128…255. Spacing returns to 1 Hz;
the off-bin magnitudes match the first half, but phase and finite mean differ.
Starting From 1 s instead includes the boundary sample and yields **129
positions**, spacing **128/129 ≈ 0.992248 Hz**. There is no Nyquist bin for
this odd-length transform: its final bin is **8192/129 ≈ 63.503876 Hz**,
although the rate's Nyquist frequency remains 64 Hz. The viewer tests cover
this boundary choice as well. Recorded timestamps are part of selecting a transform.

## Understand Remove mean

Return to **0–1 s**, Rectangular, N=128. The test tone's finite mean is
**1/128 V = 0.0078125 V**, so the mixture's DC bin is **0.2578125 V**,
not merely the declared 0.25 V offset. In the last half its mean becomes
−1/128 V and the mixture mean is **0.2421875 V**. Over the full two seconds
the cosine mean is zero and the mixture mean is 0.25 V.

Enable **Remove mean** for the first half. It subtracts **0.2578125 V** from
the mixture before transforming; its rectangular-window DC bin becomes zero.
It does not identify which part of that mean was a physical offset. With a
tapered window, subtracting the raw mean before windowing does not generally
force the windowed DC bin to zero. SP09 makes that distinction explicit.

## Align the test tone with the grid

Select **Test tone: edit frequency_hz** (`test`), press **F4**, change
**frequency_hz 16.5 → 18**, save a copy, reopen and rerun. With **0–1 s**, N=128,
Rectangular, Remove mean off, predict a **1 V peak at 18 Hz** and no off-bin
leakage apart from floating-point rounding. The mixture keeps its 0.25 V offset.
The reference remains at 16 Hz. Role-based channel labels stay accurate.

For a second view, select **Power / bin** or **PSD**. Sum power bins, or
integrate PSD with Δf, to obtain mean square. Do not square and sum the peak
amplitudes without accounting for real-signal doubling. In dB mode, enter an
explicit reference in the selected quantity's units; amplitude uses 20 log10,
power and PSD use 10 log10. The display floor affects drawing, not raw results.

## Independent checks, sources and limits

The [headless test](../../../s7tests/education_spectral_smoke.cpp) executes the
[actual saved graph](sp08_dft_leakage.s7diagram)
through production loading, validation, compilation and observation. It checks
every sample, a fresh repeat, the edit, first/full/last intervals, and compares
an independent direct complex DFT with the finite geometric formula above.
The GUI test separately checks the journal and plotted samples, selected
steps, all frequency bins, scaling, units, dB reference/floor, mean removal,
and save/reopen/rerun. Numerical budgets: **1e−10 V** for samples/normalized
complex coefficients; **1e−9** in linear viewer units; **1e−4 dB** for plotted
dB values; **1e−12 s** for timestamps, plus nanosecond rounding.

```bash
cmake --build build-release \
  --target s7tests_education_spectral --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_spectral$' --output-on-failure -V
```

[MIT 6.003 lecture 18 — Discrete-time Fourier representations](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec18/)
provides public teaching material on DT Fourier representations and FFT use
(2011 notes, checked 2026-11-19). The S7 signal choices, derivations and
experiments are original topic illustrations, not reproduced MIT assignments.
No external PDF or solution key is bundled. Source-code links require the
checkout; this guide is bundled offline. Installed packages, native Windows
and final release qualification remain open. Diagrams/generator: CC0-1.0;
guide: CC-BY-4.0.

Extension: predict which other frequencies are coherent for a 1 s versus
2 s record, preserving the rate. Next: [SP09 — window normalization](../SP09-window-normalization/README.md).
[Return to the learning route](../../index.md).
