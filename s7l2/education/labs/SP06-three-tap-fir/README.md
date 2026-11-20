# SP06 — Build and test a three-tap FIR filter

**Time:** 60 minutes. **Level:** introductory discrete-time signal processing.
Prerequisites: sampled sinusoids, complex numbers and one-sample delay (FN01,
SP03, SP05 concepts). Development target: current C++ S7/Studio source,
Linux; no hardware, plugins, private data or network required to run.
Status: `diagram-ready`, with development verification tracked in the
[evidence record](../../EVIDENCE.md). Final release qualification is open.

This is an **original S7 exercise**, illustrating unit-sample response,
convolution and filtering topics listed in the
[MIT 6.3000 Fall 2026 calendar](https://sigproc.mit.edu/fall26/)
(source checked 2026-11-19). It is not an MIT assignment, a reproduced book
problem or an endorsed resource. Equations and synthetic signals are original
derivations. S7 example diagrams/generators are CC0-1.0; this guide is CC-BY-4.0.

## Read or watch alongside the lab

- [MIT 6.3000 Fall 2026: unit-sample response and convolution, lecture handout (PDF)](https://sigproc.mit.edu/_static/fall26/lectures/Unit-Sample_Response_and_Convolution-handout.pdf).
  Start with the discrete-time portion: a system's response to one sample
  determines its response to a sum of shifted samples.
- [MIT 6.003 Fall 2011: Chapter 3, block diagrams and operators](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap3/).
  Use the public reading/download page to review delay, gain and adder notation.
- [MIT 6.3000 Fall 2026: frequency response and filtering, lecture handout (PDF)](https://sigproc.mit.edu/_static/fall26/lectures/Frequency_Response_and_Filtering-handout.pdf).
  Connect the measured amplitude/phase changes to complex-exponential response.
- [MIT 6.3000 Fall 2026 teaching calendar](https://sigproc.mit.edu/fall26/).
  Use its convolution and filtering entries for the surrounding lessons.

These links open MIT's original materials in your external browser. The
S7 exercise remains available offline in Studio help. Readings were checked
2026-11-19; no MIT handout or solution is bundled with S7.

## Predict

By the end, you should be able to predict the first three output samples,
derive the filter's complex response, measure rejection of a known tone,
and explain why removing a tap changes both DC gain and tone cancellation.

At **Fs = 300 Hz**, connect two zero-initialized unit delays, three gains
of 1/3 and an adder. All signals are F64, in volts. The exact discrete model is

```text
x[n] = 0.25 + sin(2π·10n/300 + 0.2) + 0.5 sin(2π·100n/300 + 0.7), n >= 0
x[n] = 0, n < 0
y[n] = (x[n] + x[n-1] + x[n-2])/3
```

The phases are radians. Zero prehistory describes reset delay storage, not
a sinusoid that was running forever before the experiment. The first samples
are `y[0]=x[0]/3`, `y[1]=(x[1]+x[0])/3`, and
`y[2]=(x[2]+x[1]+x[0])/3`. Calculate these before running.

For a complex exponential after startup,

```text
H(e^jω) = (1 + e^-jω + e^-j2ω)/3 = e^-jω (1 + 2 cos ω)/3
```

| Input component | Predicted output after startup |
| --- | --- |
| DC, 0.25 V | 0.25 V: H(0)=1 |
| 10 Hz, 1 V peak | About 0.98543 V peak, phase shift −12° |
| 100 Hz, 0.5 V peak | Zero: ω=2π/3 is a filter zero |

The second unit-circle zero is at ω=4π/3 (equivalently −2π/3).
Phase at either exact zero is undefined; compare complex coefficients or
magnitude there. At Nyquist H(π)=1/3: this moving average does not remove
all high frequencies. The one-sample linear-phase group delay is a response
property; it does not postpone the first output or add a transport delay.

## Construct and run

Open the new [SP06 diagram](sp06_three_tap_fir.s7diagram)
with **Help → Education: start here**, or **File → Open…**.
The source adder combines DC and the two independent oscillators. Follow its
three branches: direct to `tap0`, one delay to `tap1`, two delays to `tap2`.
The `filtered` adder combines those branches. The Scope is an observer.

The diagram's yellow instruction frame has clickable **Open student lab**
and **MIT teaching** links. The first opens this guide in Studio's offline
help; the MIT links open the original online teaching materials in your browser.
Zoom with Ctrl+mouse wheel when you want to read a block or follow one branch.

1. Set **Run Configuration** to **4 seconds, 300 Hz** and run locally.
2. Double-click **Input and filtered output** (`scope`). The `input` and
   `filtered` channels are both in V. Start by inspecting a short time range,
   for example 0–0.2 s, so individual cycles and startup are visible.
3. Observe 1,200 samples per channel. Sample index n=0 corresponds to
   successful runtime step k=1 and Scope timestamp **1/300 s**. In general,
   step k samples n=k−1 at timestamp k/300 s. Do not compare the first sample
   against the source formula evaluated at timestamp k/300.
4. After n=2, compare the smooth output with the predicted DC plus 10 Hz tone.
   The 100 Hz input ripple should disappear. Initial samples are a transient,
   not evidence of poor steady-state rejection.

This lab performs no continuous-system approximation: the recurrence itself
is the model. A different sample rate changes the physical frequencies of
the filter zeros. Keep 300 Hz for the accepted comparisons.

## Verify independently

From `s7l2/`:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target s7tests_education_sp06 --parallel 2
ctest --test-dir build-release -R '^s7tests_education_sp06$' --output-on-failure -V
./build-release/s7tests/s7tests_education_sp06 --trace /tmp/sp06-trace.dat
```

The [test](../../../s7tests/education_sp06_smoke.cpp) loads this exact saved
schema through the production loader, validates/compiles it, executes every
step and samples outputs through `ObservationService`. It independently
calculates the source formula and finite convolution, checks both delay
histories, first/final samples, rational timestamps and repeated-run equality.
The trace contains full-precision values in V, the reference and residual.

A source-only variant excites the same filter with a unit sample. Because
`ImpulseGenerator.area` is an area, setting it to **1/300 V·s** produces
one sample of height 1 V at this rate. The expected response is
`[1/3,1/3,1/3,0,...]`. A finite transform of those measured samples is checked
at DC, both zeros, Nyquist and two interior frequencies against the factored
formula above. The saved two-tone run separately checks **complex** output/input
coefficients on n=300…899, exactly 20 low-tone and 200 high-tone cycles.

Frozen tolerances: 1e−10 V for source/convolution/edited-graph samples and
1e−10 for measured complex gain; 1e−14 for unit-sample/impulse-transform checks.
These allow F64 phase/summation rounding over 1,200 steps. They are not a
physical-model accuracy budget or a cross-platform bitwise guarantee.
See [evidence](../../EVIDENCE.md) for actual results and exclusions.

## Make it fail, explain, then extend

Select `tap1`, press **F4**, change **factor** from 1/3 to **0**, and rerun.
Save a copy under a new name, reopen it and repeat. Predict the result first:

```text
y_bad[n] = (x[n] + x[n-2])/3
DC output = 1/6 V
100 Hz output amplitude = 1/6 V peak
```

The graph remains valid and executes successfully. It fails the **intended
moving-average specification**: an implementation success is not proof that
the chosen model is correct. The headless negative test checks the altered
recurrence, changed mean and surviving tone, and requires a substantial error
against the intended filter. Studio automation covers this edit/rerun path
separately from the headless checks.

Restore the middle tap. For an extension, predict the effect of moving the
100 Hz source to 90 Hz; calculate H at the new frequency and check the residual
amplitude. This extension has no automatic acceptance claim until tested.

## Limits and troubleshooting

The verified envelope is the stated local F64 graph and variants, not arbitrary
sample rates, long runs or edited coefficients. No global solver is involved.
If the rejection fails, check rate, source frequency, both delay initial
values, tap weights and sample origin before adjusting tolerances. A full-run
Scope plot may compress many cycles; inspect a short interval.

The physical two-host version, installed education-guide navigation, release
artifacts and native Windows usability are pending. This local lab is the
candidate for G2; it does not claim that gate has passed or provide a new
remote-live-observation workflow.

Continue with [SP07 — filter transfer functions](../SP07-filter-transfer/README.md)
for independently checked low-pass and resonant frequency responses.
