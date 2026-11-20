# SP14 — Which parts of an AM signal survive coherent detection?

**60 minutes · intermediate · prerequisites: SP07–SP09, FN05.**
Original synthetic S7 AM experiment aligned with MIT modulation teaching.
Readings checked 2026-11-19. Current 0.2.0 development source on Linux,
Studio or headless C++; no audio file, hardware, runtime plugin or network
required. Status: diagram-ready; [development evidence](../../EVIDENCE.md)
records executed checks separately from release/platform qualification.

Predict RF sidebands, distinguish raw mixing from recovered baseband, measure
phase-error attenuation, and explain finite low-pass gain, phase and settling.

## Predict before running

At **256 Hz**, h=1/256 s, let source/state index n represent time n/Fs:

```text
m[n] = 0.6 cos(2π 4 n/256) V
a[n] = 1 V + m[n]                       target envelope, 0.4…1.6 V
c[n] = sin(2π 32 n/256)                 dimensionless carrier
s[n] = a[n] c[n]                       AM signal, V
l[n] = 2 sin(2π 32 n/256 + φ)           dimensionless local reference
v[n] = s[n] l[n]
```

Initially φ=0. The factor 2 compensates the 1/2 in sine multiplication.
This is a low-frequency simulated carrier chosen for inspection; no radio
hardware or communication channel is modeled.

| Signal | Expected nonnegative-frequency components, peak units |
| --- | --- |
| RF s | 28 Hz: 0.3 V; 32 Hz: 1 V; 36 Hz: 0.3 V |
| Raw mixer v, φ=0 | DC: 1 V; 4 Hz: 0.6 V; 60/68 Hz: 0.3 V each; 64 Hz: 1 V |
| Recovered, steady φ=0 | DC: 1 V; 4 Hz: ≈0.498691 V; 64 Hz image: ≈0.0231542 V |

The signs/phases of components matter even when their magnitudes match.
Since the carrier is a sine, its positive-frequency peak phasor is −i V;
the RF sideband phasors are −0.3i V. All frequencies, including mixer
images at 60/64/68 Hz, are below Nyquist **128 Hz**.

## Construct, run and inspect

Open [sp14_coherent_am.s7diagram](sp14_coherent_am.s7diagram)
through **Help → Education: start here**. Keep **4 s at 256 Hz**,
1024 samples. Follow message+bias → multiply carrier → multiply local →
two low-pass schema elements. Each FirstOrderLag contains an explicit
difference/divider/EulerIntegrator network, not an ideal frequency cutoff.
Both filter states start at zero; both tau inputs are **0.02 s**.

Open the four Scopes:

1. **RF** (`rf_scope`): transmitted signal in V.
2. **Baseband** (`baseband_scope`): target envelope and recovered voltage in V.
3. **Unfiltered mixer** (`mixer_scope`): raw product in V; keep it separate
   so its stronger images do not obscure the recovered trace.
4. **Carrier / local reference** (`carrier_scope`): dimensionless unit **1**,
   not V. Compare their phase directly.

For startup, uncheck **Fit full run** and wheel-zoom near the first samples
to about **0–0.25 s**. Recheck it to restore the full capture. Time navigation
is separate from the From/To analysis interval fields.

Every source/state value at index n is observed after step n+1 at Scope time
**(n+1)/256 s**. The first observation is at 0.00390625 s. The two filters
update using old states; there is no hidden same-step propagation or extra
feedback delay. Recovered samples n=0,1,2 are zero; the first nonzero value
at n=3 equals β²v[1], plotted at **0.015625 s**.

## Understand the actual low-pass response

Let β=h/0.02=25/128 and p=1−β=103/128. The exact drawn recurrence is

```text
x[n+1] = p x[n] + β v[n]
y[n+1] = p y[n] + β x[n]
x[0] = y[0] = 0
H(z) = (β/(z−p))²
```

The two states supply two causal sample delays. At 4 Hz, |H|≈0.831151 and
phase≈−54.4546°, so the 0.6 V message becomes about **0.498691 V peak**.
It is attenuated and phase shifted relative to the target; it is not a
perfect reconstruction. The 64 Hz image is reduced by |H|≈0.0231542 but
remains finite. A settling transient is also present near the start.

An independent finite convolution uses
`h_filter[k]=β²(k−1)p^(k−2)` for k≥2, zero earlier. It checks every recorded
filter output from startup. A separate steady harmonic calculation multiplies
each complex mixer component by H at its frequency.

The intended continuous two-RC filter is `(1+i2πfτ)^−2`. Its response differs
from this forward-Euler implementation. The test also executes at **512 Hz**,
retaining τ and physical frequencies: the complex 4 Hz discretization error
roughly halves (measured ratio ≈2.03). That checks approximation order;
agreement with the discrete recurrence alone would not establish it.

## Inspect a steady spectrum with the correct units

For RF, baseband and mixer Scopes:

1. Set analysis **From 2.002 s**, **To 4 s**, then **Analyze** and select
   **Spectrum**. This interval excludes the sample stamped exactly 2 s and
   includes **512 samples**, source indices **512…1023**, observed at
   **2.00390625…4 s**. The filter's startup transient has decayed.
2. Choose **Rectangular**, **Resolution 512**, **Remove mean off**,
   **Linear**, **Full frequency band**, **Peak amplitude**. For baseband,
   use **All inputs** to compare target and recovered spectra.
3. Check **Δf=0.5 Hz**, DC through 128 Hz. RF bins 56/64/72 are 28/32/36 Hz.
   Message bin 8 is 4 Hz; mixer images occupy bins 120/128/136.
4. Try **Power / bin** (V²), then **PSD** (V²/Hz). For an isolated coherent
   real tone of peak A, power is A²/2; DC power is its value squared.
   PSD divides each bin's power by 0.5 Hz. Sum power bins or integrate PSD
   to recover mean square; peak amplitude and PSD are not interchangeable.

Connecting spectral bins is a display aid. The finite rectangular DFT is
coherent here because the chosen interval contains integer numbers of cycles;
that does not make arbitrary intervals or transients leakage-free.

## Change local phase, save and rerun

Select **local**, press **F4**, change **phase 0 → 1.0471975511965976** (π/3),
save a copy, reopen it and rerun. The RF source is unchanged. From

`v[n]=a[n]{cos φ−cos(2·2π32n/256+φ)}`,

predict **DC and the 4 Hz message halve**: recovered DC≈0.5 V, message
peak≈0.249345 V. The high-frequency image amplitudes do **not** all halve;
their phases change. Compare the same steady analysis interval after rerunning.
Do not compensate the phase error by changing the transmitted message.

For the declared limit experiment, try **phase=1.5707963267948966** (π/2).
The ideal baseband DC/message vanish, but this finite filter still passes
some 60/64/68 Hz image energy. A nonzero recovered ripple is expected;
it is not evidence that quadrature detection recovered the message.
Restore phase zero afterward. The test separately verifies low-frequency
suppression and a surviving 64 Hz component, rather than assuming zero
at every output sample.

## Independent verification and limitations

The [headless test](../../../s7tests/education_am_smoke.cpp) loads the exact
saved graph with production validation/compilation and observes every source
and filter state. A [separate long-double finite-convolution reference](../../../s7tests/education_am_reference.hpp)
checks baseline, π/3 edit, π/2 limit and 512 Hz refinement. Independent direct
DFTs check the production transform, all steady complex harmonics, image
phases and one-sided Parseval. Scalar/complex budgets are 1e−10 V, with
separately checked model-time/nanosecond coordinates.

The real Studio workflow verifies literal channel names/units/bindings,
every journal and full Scope value, displayed sample subsets/extrema,
primary edit/save/reopen/rerun and the actual selected 512-sample interval.
All Spectrum bins and plotted values are checked against independent direct
DFTs in amplitude, power/bin and PSD, with a 1e−9 display budget in each
quantity's units and run/step provenance checked. Native Windows, installed
packages and the final collection-wide viewer review remain separate.

```bash
cmake --build build-release --target s7tests_education_am s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_am|s7studio_gui_education_am)$' --output-on-failure -V
```

## MIT readings and source rights

[MIT 6.003, Fall 2011, Lecture 23: Modulation](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/08c6a7bc7e56eda548248c354886dc37_MIT6_003F11_lec23.pdf),
PDF pages 13–18 and 25–28, supplies topic context for sidebands and coherent
phase error. [MIT RES.6-007, Spring 2011, Lecture 13: Continuous-Time Modulation](https://ocw.mit.edu/courses/res-6-007-signals-and-systems-spring-2011/a774c49855ba6bfe7266943eb8b5bc41_MITRES_6_007S11_lec13.pdf),
PDF pages 1–2, 7–8 and 10, discusses synchronous mixing/filtering and phase
synchronization. Its ideal continuous filters differ from this declared
finite sampled two-state network.

This is original S7 topic alignment, not an MIT assignment or worked solution.
No PDF, figure, audio or solution is bundled. Diagram/generator: CC0-1.0;
guide: CC-BY-4.0. See [sources](../../SOURCES.md).
[Return to the learning route](../../index.md).
