# SP11 — When did that frequency happen?

**45 minutes · intermediate · prerequisites: SP03, SP08/SP09.**
Original S7 burst/chirp experiment, aligned with MIT short-time Fourier
analysis. Readings checked 2026-11-19. Development profile: current 0.2.0
source on Linux, Studio or headless C++; no runtime plugin, network or
hardware needed. Status: diagram-ready; see the [executed evidence](../../EVIDENCE.md)
for development checks and remaining release qualification.

You will locate a short tone and a rising chirp, predict STFT time/frequency
coordinates, and distinguish window length from hop size. You will also
explain why a map can show energy around an event's edges and why squared
magnitude, power per bin and PSD are different quantities.

## Predict

The graph runs for **4 s at 128 Hz**: 512 real F64 samples. Source index n=0
is observed after step 1 at model time **1/128 s**. Source time is n/128;
the Scope timestamp of that same value is (n+1)/128.

- **Burst:** a 1 V peak, 32 Hz cosine multiplied by a dimensionless gate.
  The gate is 1 for **128≤n<256**, zero otherwise. Its first active sample
  is observed at **1.0078125 s**; at n=256, observed at 2.0078125 s, it is off.
- **Chirp:** a quieter 0.5 V peak signal whose frequency increment rises
  from 8 toward 40 Hz across the capture. This is the actual discrete source:
  `c[n]=0.5 cos(2π(8n/128 + 32n(n−1)/(2·512·128))) V`.
  It sums left-endpoint frequency increments. Substituting the continuous
  chirp's n² phase gives a different sampled signal.
- **Mixture:** burst + chirp. Expect a horizontal feature near 32 Hz while
  the gate is open, plus a weaker rising feature. Outside the burst, the
  chirp is easier to identify. Nyquist is 64 Hz.

The STFT analyzes windows starting at s=mH, with N samples per window and
hop H. It returns **C=1+floor((512−N)/H)** full windows. Its time coordinate
is the midpoint of the first and last sample timestamps:

`t[m]=(1 + mH + (N−1)/2)/128 s`; `f[k]=128k/N Hz`, k=0…N/2.

| N | Hop H | Columns C | Δf (Hz) | Center spacing (s) | First center (s) |
| --- | --- | --- | --- | --- | --- |
| 64 | 32 | 15 | 2 | 0.25 | 0.25390625 |
| 128 | 64 | 7 | 1 | 0.5 | 0.50390625 |
| 64 | 16 | 29 | 2 | 0.125 | 0.25390625 |

Increasing N improves bin spacing but spans a longer changing signal.
Reducing H adds overlapping columns. It does **not** shorten the window,
sharpen frequency resolution or create independent observations.

## Construct, run and inspect

1. Open [the new SP11 diagram](sp11_short_time_fourier.s7diagram)
   through **Help → Education: start here → sp11_short_time_fourier**.
   The yellow frame links this guide, MIT's STFT lecture and S7's map guide.
2. Follow the gate: **start** minus **stop** multiplies the carrier. The
   mixture adds the chirp. These are causal scalar sources and arithmetic
   blocks; observation does not feed back into the graph.
3. Run with the saved **4 s / 128 Hz** settings. Open the Time Scope and
   compare **burst**, **chirp**, **mixture** in V against model time in seconds.
   Connecting segments are a drawing aid, not continuous reconstruction.
4. Open **Burst STFT** or **Mixture STFT**. Small completed captures analyze
   automatically. Choose **STFT spectrogram**, **FFT size 64**, then
   **Hop (samples) 32**, **Magnitude**, **Display up to 64 Hz**, **Heatmap**.
   Changing FFT size resets hop to half that size; set a custom hop afterward.
   If reanalysis is pending, the preceding valid result stays visible until
   its replacement is ready. Use **Analyze / freeze** to capture a fresh run.
5. Inspect the **Frequency row** and **Time column** fields, the linked
   **Input trace**, and **Frequency slice** tabs. With N=64/H=32, column 4
   starts at n=128; row 16 is 32 Hz. The pure burst's full-window coefficient
   there has magnitude **1 V**. The inspector gives the complex coefficient,
   not just a color. Phase is relative to the first sample of each window.
6. Try N=128/H=64, then N=64/H=16. Confirm the table's dimensions and spacing.
   When a window falls entirely within the original burst its coherent
   32 Hz magnitude stays 1 V. Windows crossing an edge give a smaller result.

## Understand edges and units

STFT uses the **periodic Hann** `w[j]=sin²(πj/N)`. For
`X[m,k]=Σ_j w[j] x[mH+j] exp(−i2πkj/N)` and a=1 at DC/Nyquist, 2 elsewhere,
the stored coefficient is `C[m,k]=a X[m,k]/Σw`, in V. The taper's
`Σw=N/2`, `Σw²=3N/8`.

| Display | Value | Unit |
| --- | --- | --- |
| Magnitude | abs(C) | V |
| Squared magnitude | abs(C)² | V² |
| Phase | arg(C), only if abs(C)>1e−12 | rad |
| STFT power per bin | abs(C)² (Σw)² / (a N Σw²) | V² |
| STFT PSD | power per bin / Δf | V²/Hz |

Summing power bins or integrating PSD gives the **window-weighted** mean
square, `Σ(w²x²)/Σw²`. Merely squaring the peak-tone coefficient does not
perform that normalization. The selected display changes presentation;
saved complex coefficients keep their original amplitude convention.

Only **complete windows** are returned. There is no STFT zero extension at
the capture ends, no centered window around every raw sample and no promise
of a column at time 0 or 4 s. An incomplete final window is discarded.
All returned STFT coefficients have full capture support. CWT has a different
edge mask; it is outside this lesson.

A window crossing the gate boundary contains samples on both sides. Its
value is assigned to the window's center, so apparent energy can begin before
or persist after the event itself. That is time localization, not a causal
prediction by the runtime. At N=64/H=32, burst column 3 overlaps the onset;
its 32 Hz magnitude is roughly half the fully supported plateau. Inspect the
raw trace before interpreting that color as an event time.

Zero energy is valid data and appears at zero magnitude. In **Phase** mode,
silent/near-zero cells are gray because phase is undefined below the threshold.
Do not interpret those gray cells as lost samples. The map draws at most
513×513 selected cells; this lab's matrices fit without reduction. Inspector
and export retain full coefficients.
Heatmap cells use the numeric coordinate centers, with boundaries halfway
between neighboring centers. Axes and cursor selection therefore refer to
the same cells. Cell width reflects nonuniform coordinates; reducing the
drawing averages color coverage so narrow events remain visible.

## Change duration, save and verify

Select **stop**, press **F4**, change **start_time 2 → 1.5**, save a copy,
reopen it and rerun. The gate is now active for **128≤n<192**, 64 samples.
The chirp is unchanged. Predict how many windows overlap the shorter burst.
A 64-sample window beginning at n=128 still fits; a 128-sample window there
now crosses the closing edge and loses its 1 V full-tone plateau.

Save a map as `.s7analysis` and reopen it. The saved result retains input,
complex matrix, rate, origin, exact window/hop settings and run/channel identity.
Changing hop on a frozen result reuses that original input; it does not rerun
the model. Opening a result with inconsistent hop or coordinates is rejected
before replacing the valid view.

Failure/extension questions:

- Choose a window longer than the captured record. No full STFT exists;
  the error must retain the previous valid result, not fabricate a padded map.
- Compare H=16 and H=32 at N=64. Every other dense column analyzes exactly
  the same samples as its sparse counterpart. Why does the frequency spacing
  remain 2 Hz?
- Move the gate slightly between hop positions and compare input timestamps
  with map centers. Explain the time uncertainty without calling it a delay.

## Independent implementation checks

The exact saved graph runs through production loading, validation, compilation
and read-only observation. An independent closed-form source oracle checks
every sample. A long-double direct DFT with sine-squared weights checks the
production STFT's complex coefficients, full-window counts, center times,
frequency bins, DC/Nyquist factors and weighted Parseval. It shares no FFT or
window routine with production. Source/coefficient budgets are 1e−10 V;
coordinates use 1e−12 s/Hz. These are numerical, not perceptual color tolerances.

The real Studio workflow checks every recorded/plotted Time point, every
returned coefficient and applied map cell, the phase mask, quantity units and
frequency-slice points for both maps and all three profiles. It also covers
the primary edit, diagram save/reopen, frozen-result hop restoration and
invalid-import rollback. Development checks are distinct from native Windows,
installed-package and final collection-wide viewer qualification.

From `s7l2/`:

```bash
cmake --build build-release --target s7tests_education_stft s7studio_gui_workflows_test s7studio_time_frequency_test
ctest --test-dir build-release -R '^(s7tests_education_stft|s7studio_gui_education_stft|s7studio_time_frequency)$' --output-on-failure -V
```

## MIT reading and source rights

[MIT 6.300, Fall 2025, Week 9 Lecture A: Short-Time Fourier Transform](https://sigproc.mit.edu/_static/fall25/lectures/Short-Time_Fourier_Transform.pdf),
especially PDF pages 3–7, explains local windowed DFTs, step size and map
orientation. The public [Computing Spectrograms, Spring 2025](https://sigproc.mit.edu/spring25/labs/Computing_Spectrograms)
page provides additional topic context. Its assignments/code are not copied
or solved here. MIT's raw-transform conventions differ from S7's explicitly
defined peak-amplitude coefficients.

Signals, graph, questions and numerical references are original S7 work.
No MIT PDF, image, audio or solution is bundled. This is topic alignment,
not an official MIT exercise or endorsement. See [sources](../../SOURCES.md)
and the [S7 time–frequency guide](../../../examples/time_frequency/README.md).
