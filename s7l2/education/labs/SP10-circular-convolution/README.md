# SP10 — Where does the convolution tail go?

**45 minutes · intermediate · prerequisites: SP05/SP06 and SP08.**
Original S7 experiment, aligned with circular convolution and zero-padding.
Readings checked 2026-11-19. Development profile: current 0.2.0 source,
Linux Studio and headless C++; no plugins, network or hardware required.
Status: diagram-ready; executed development checks are recorded in the
[evidence](../../EVIDENCE.md), separately from release qualification.

You will predict a six-sample convolution, explain why its tail changes the
first samples of a four-point circular result, and choose sufficient padding.
You will also distinguish startup from a periodic steady-state calculation.

## Predict before running

Use the finite input **x=[1, 2, −1, 1] V** and dimensionless FIR taps
**h=[1, 0.5, −0.25]**. All signals are F64. Outside its four entries, the
finite input is zero. Linear convolution is

`y[l] = Σ_i Σ_j x[i] h[j]`, summing pairs with `i+j=l`.

It has **L+M−1=4+3−1=6** entries. For a period N, put each product at
`(i+j) mod N` instead. That is circular convolution, equivalent to folding
the full linear result modulo N. It is not simply its first N samples.

| l | Scope t (s) | Linear (V) | Circular 4 (V) | Padded 8 (V) |
| --- | --- | --- | --- | --- |
| 0 | 1.125 | 1 | 1.75 | 1 |
| 1 | 1.250 | 2.5 | 2.25 | 2.5 |
| 2 | 1.375 | −0.25 | −0.25 | −0.25 |
| 3 | 1.500 | 0 | 0 | 0 |
| 4 | 1.625 | 0.75 | 1.75 | 0.75 |
| 5 | 1.750 | −0.25 | 2.25 | −0.25 |
| 6 | 1.875 | 0 | −0.25 | 0 |
| 7 | 2.000 | 0 | 0 | 0 |

Why is the first circular value 1.75? The linear tail value at l=4 wraps
onto l=0. The tail at l=5 wraps onto l=1. The four circular values repeat
in the table; they are not an eight-point circular calculation.

Padding requires **N ≥ L+M−1**, here N≥6. N=8 is convenient for a radix-2
FFT; six is the mathematical minimum. Both x and h must be represented with
zeros through that N-point transform. Extra padding does not add information.

## Construct and run

1. Open [the new SP10 diagram](../../../examples/education/sp10_circular_convolution.s7diagram)
   through **File → Example Diagrams… → education → sp10_circular_convolution**.
   The yellow frame links this offline guide and the online MIT reading.
2. Inspect the three rows; zoom into a row to read the element titles.
   The left-hand word builders sum a pulse and three delayed copies with
   weights `[1,2,−1,1]`. The top emits one word beginning at sample n=8;
   the middle repeats every four samples; the bottom inserts four zeros
   and repeats every eight samples.
3. The right-hand FIR in every row is
   `output[n]=input[n]+0.5 input[n−1]+c input[n−2]`.
   The single `last_tap` Constant supplies **c=−0.25** to all three rows.
   Its values are dimensionless; input/output Scopes use V.
4. **Every Delay starts at 0.** The first eight samples warm up the periodic
   paths. No future samples are read and no global solver is involved.
5. Run with the saved **2 s at 8 Hz** profile: h=0.125 s and 16 ticks.
   Source index n=0 is observed after step 1 at Scope time 0.125 s.
   The comparison block is **n=8…15**, Scope times **1.125…2 s**.
6. Double-click **Inputs: compare the repetition**, then
   **Outputs: read the second block**. Turn off **Fit full run** and zoom
   the Time plot to 1…2 s. Compare the eight recorded sample positions
   with the table. Straight connecting segments are a drawing aid, not
   continuous-time interpolation.

The **padded minus linear** trace is zero throughout this comparison block.
Before it, the padded path has already emitted a word while the isolated path
is waiting; their difference is intentionally nonzero. Looking at the entire
run without the input schedule can lead to a wrong conclusion.

Optional spectral check: in the output Scope, set **From=1.125 s, To=2 s**,
click **Analyze**, and choose **Spectrum → Rectangular → Peak amplitude →
Linear**, **All inputs**, **Remove mean off**. This is exactly N=8 samples,
Δf=1 Hz, Nyquist=4 Hz. Linear and padded spectra coincide; the difference
trace is zero. This display shows magnitudes; it does not by itself prove
the complex convolution theorem. The automated test checks that separately.
The **From/To** fields select the analysis interval; zooming the Time plot
alone does not change them.

## Change one tap, then verify

Select **last_tap**, press **F4**, change **value −0.25 → 0.5**, and rerun.
The input words stay the same. Predict the new linear result:

`[1, 2.5, 0.5, 1.5, 0, 0.5] V`.

The period-4 result becomes `[1, 3, 0.5, 1.5] V`; period 8 again recovers
the full linear result with two terminal zeros. Notice that wrap-around
now affects l=1 while the tail at l=4 is zero. Save a copy, reopen it and
rerun to check that the shared coefficient and named channels are preserved.

## Failure experiments and explanations

- **Insufficient padding:** N=4 wraps two tail entries. N=5 still wraps the
  last entry onto l=0. It is a different mathematical operation, not an
  arithmetic bug. N=6 is sufficient even though it is not a power of two.
- **Missing warm-up:** the first four samples of the middle row are
  `[1,2.5,−0.25,0] V`, not its periodic steady-state result. The causal FIR
  starts with zero history. Explain why the second four-sample period works.
- **Too-short capture:** stop before 1.75 s and you miss the isolated word's
  final tap contribution. An apparently short output is not the full
  convolution. Do not discard the tail when comparing algorithms.

Extension: choose another signed four-entry word, calculate its linear and
modulo-4 sums on paper, and explain the minimum padding. Editing the saved
graph's source word requires updating all three word builders consistently.
Changing the sample rate also changes the pulse-to-sample schedule; this
lab's frozen profile is 8 Hz.

## What this verifies in S7

The saved graph uses scalar sources, Delay, Adders and Multiply. It does not
pretend to provide frame-valued graph ports. Separately, the headless test
passes its **actual recorded input word** to the production
`s7::FirConvolution` and `s7::FftConvolution` APIs. Both promise **linear**
convolution: full output in independent-frame mode, or sample-aligned blocks
with retained history in continuous-stream mode. The FFT API already pads
internally; it has no under-padded/circular mode.

Independent long-double pair-product sums and a short direct DFT product
check the graph, full tail, circular folding, N=4/5/6/8, streaming boundaries,
zero-frame tail flushing, reset and repeatability. The module test also
sweeps complex signals, frame partitions and kernels longer than a frame.
The references call neither production convolution nor its FFT routines.
Graph values use a 1e−12 V budget; processor/DFT checks use
`1e−10 max(1,|reference|)` to allow floating-point operation-order differences.

From `s7l2/`:

```bash
cmake --build build-release --target s7tests_education_convolution s7core_convolution_smoke s7studio_gui_workflows_test
ctest --test-dir build-release -R '^(s7tests_education_convolution|s7core_convolution_smoke|s7studio_gui_education_convolution)$' --output-on-failure -V
```

The GUI check compares every journal value and plotted time point, the
selected block's spectrum, channel bindings/names/units and save/reopen/edit
behavior. These are development checks; installed packages, native Windows
and the collection-wide viewer audit remain open.

## MIT reading and source rights

[MIT RES.6-008 Digital Signal Processing, lecture 10: Circular Convolution](https://ocw.mit.edu/courses/res-6-008-digital-signal-processing-spring-2011/resources/lecture-10-circular-convolution/),
Alan V. Oppenheim; OCW edition Spring 2011. The resource page explicitly
covers finite circular convolution, aliasing of the linear result and
recovering linear convolution with padding. For the prerequisite, see
[MIT 6.003 lecture 8: Convolution, Fall 2011](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec08/).

This is an original S7 topic illustration, not a reproduced MIT problem or
solution. Data, graph and explanations were authored for S7; no external
PDF or figure is bundled. See the [source record](../../SOURCES.md) and
[implementation contract](../../../docs/CONVOLUTION.md). MIT does not endorse
this collection. Continue to SP11 in the [curriculum](../../COURSE_MAP.md).
