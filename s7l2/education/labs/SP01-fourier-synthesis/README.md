# SP01 — Build a square wave from smooth tones

**45 minutes · Beginner · Prerequisite: [FN01](../FN01-unit-delay/README.md).**
Current Linux S7/Studio development build; no plugin, hardware or network needed
for the experiment. Status: `diagram-ready`; development checks and remaining
release qualification are recorded in [EVIDENCE](../../EVIDENCE.md).

**Your question:** do more harmonics remove the ringing near a jump?
You will predict a partial sum, identify its harmonic amplitudes, check its
mean-square value and explain a deliberate missing-harmonic experiment.

## Read the new graph

Open **Help → Education: start here → sp01_fourier_synthesis.s7diagram**.
Five unit-amplitude sine sources have frequencies **2, 6, 10, 14 and 18 Hz**.
Each passes through an explicit Gain, `4/(mπ)` for odd harmonic m.
Adders expose the first three and first five odd harmonics (the latter is
named **full sum** in Scope); the first weighted
sine is the one-term approximation. All outputs are F64 voltages.
The square target is a separate PulseGenerator, ±1 V, 2 Hz, 50% duty.
There is no integration, stored delay or hidden Fourier synthesis element.

For K=1, 3 or 5 terms, independently define

```text
S_K(t) = sum over odd m=1,3,...,2K−1 of [4/(mπ)] sin(2π m × 2 t) V
sample n: t = n/128 s; Scope timestamp = (n+1)/128 s
```

The sources start at n=0 with phase zero. The target uses the **right-hand
value** at a jump: +1 V at t=0, −1 V at t=0.25 s. Every sine partial sum
is zero at a jump: the midpoint of the two limits. That mismatch at the
jump is expected, not a missing runtime sample.

## Predict before running

| n | Signal time (s) | Scope time (s) | One term (V) | Three terms (V) | Five terms (V) |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 0.0078125 | 0 | 0 | 0 |
| 1 | 0.0078125 | 0.015625 | 0.124799 | 0.368040 | 0.592789 |
| 3 | 0.0234375 | 0.03125 | 0.369602 | 0.951099 | 1.178202 |
| 16 | 0.125 | 0.1328125 | 1.273240 | 1.103474 | 1.063054 |

Calculate n=16 yourself using sin(mπ/2). Click **Run**, keep **2 s at 128 Hz**,
then double-click **Compare the named signals**. There are **256 samples per
channel**, from Scope time 1/128 through 2 s. Zoom the time plot to **0–0.5 s**
for one period. Use the legend to hide channels when they obscure each other.
The Scope time axis is model time; subtract 1/128 s when evaluating S_K(t).
Straight segments between samples are drawing aids, not reconstructed signals.

Near the jump, five terms still exceed 1 V: the sampled peak is about
**1.178202 V**. More terms narrow the ringing region without eliminating
the limiting overshoot (Gibbs behavior). Five terms are a finite illustration,
not a numerical proof of the infinite-series limit. Away from jumps,
the approximation improves; at jumps it approaches the midpoint.

## Measure the spectrum correctly

1. In Scope's analysis controls, select **Current run**, **From 0 s**, **To 2 s**,
   then click **Analyze**. The default analysis interval is only part of the run;
   set the full interval explicitly.
2. Open **Spectrum**. In **Transform**, select **Rectangular** window and
   keep **Remove mean** off. Use the full interval, **N=256**, and **All inputs**.
3. Select **Peak amplitude**, **Linear**, with frequency offset **0 Hz**.
   The x axis is Hz, bin spacing **128/256 = 0.5 Hz**, Nyquist **64 Hz**.

The five-term channel has peaks at 2, 6, 10, 14 and 18 Hz with amplitudes
**1.273240, 0.424413, 0.254648, 0.181891 and 0.141471 V peak**.
The one-term and three-term channels contain their corresponding subsets.
The square target contains many harmonics and is not a bandlimited
reference for those five spectral coefficients. Select/hide inputs to compare.
A default Hann window spreads coherent tones into adjacent bins; that is
window behavior, not an extra harmonic in the graph.

## Check Parseval, then remove one harmonic

For these orthogonal retained tones, the mean-square value is
`P_K = (8/π²) sum 1/m² V²`: **0.810569**, **0.933056**, **0.959605 V²** for
one, three and five terms. It approaches the target's 1 V². In **Power / bin**,
sum the one-sided bins; do not square and sum the peak-amplitude values
without the real-signal factor of two. **PSD** is power per Hz: integrate it
using Δf=0.5 Hz. The automated viewer checks cover both quantities.

Select **Coefficient: 4 / (9π)** (`weight9`), press **F4**, change **factor to 0**,
save your own copy, reopen and rerun. Predict: only the five-term channel
changes; its 18 Hz peak disappears and its mean square decreases by
`8/(81π²) V²`. It is now a four-term sum, still named **full sum** because that channel
collects every branch. F4 shows the actual parameter. The other partial sums are unaffected.

Restore the coefficient to **0.1414710605261292**. As a failure experiment,
run at **32 Hz**: the 18 Hz term aliases to **−14 Hz**, subtracting from the
14 Hz term. The resulting 14 Hz amplitude is **8/(63π) V**, not 4/(7π).
The rate is too low to preserve all retained tones. More samples at that same
rate cannot recover their original frequencies; SP03 investigates this further.

## Independent checks and limits

The [headless test](../../../s7tests/education_signals_smoke.cpp) production-loads
this [saved graph](sp01_fourier_synthesis.s7diagram),
checks every sample, a fresh repeat, the missing ninth term and the 32 Hz
alias case. Independent long-double sine sums and a direct complex DFT
check the Fourier coefficients, signs, empty bins, mean square and Parseval.
Budgets: **1e−10 V** for samples/normalized complex coefficients,
**1e−11 V²** for power, **1e−12 s** for timestamps, plus nanosecond rounding.
These finite F64 bounds cover the named profiles, not arbitrary long runs.

```bash
cmake --build build-release \
  --target s7tests_education_signals --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_signals$' --output-on-failure -V
```

The Studio workflow separately checks recorded and **plotted** samples,
channel names/units, axes, the selected interval, all spectral bins against
a direct DFT, peak/power/PSD scaling, the F4 edit and save/reopen/rerun.
Source-code links require the checkout; the guide itself is bundled offline.
Installed packages and native Windows remain unqualified.

## Reading and extension

[MIT 6.003 lecture 15 — Fourier series](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec15/)
provides original teaching material on harmonic representation and orthogonal
decomposition (2011 notes; checked 2026-11-19). This S7 experiment and its
numbers are independently authored; no particular MIT exercise is reproduced.
No MIT PDF or solution key is bundled. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Extension: predict the effect of reversing the sign of one coefficient.
Its peak-amplitude spectrum alone loses that sign; inspect the time trace
or a complex coefficient. That extension has no separate qualification claim.

Next: [SP03 — sampling and aliasing](../SP03-aliasing/README.md).
[Return to the learning route](../README.md).
