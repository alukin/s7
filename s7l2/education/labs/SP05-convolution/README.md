# SP05 — Add shifted impulse responses

**40 minutes · Beginner · Prerequisites: [FN03](../FN03-initial-conditions/README.md),
[FN04](../FN04-impulse-transfer/README.md).** Current Linux development build;
no hardware, plugin or network needed to run. Status: `diagram-ready` with
[development evidence](../../EVIDENCE.md); release qualification remains open.

**Your question:** does filtering a sum equal adding the shifted responses?
Predict the overlap, check the sum sample by sample, then discover why
remembered input history matters even before the first pulse.

## Three rows, one rule

Open **File → Example Diagrams… → education → sp05_convolution.s7diagram**.
This original graph has three explicit copies of a short causal filter:

```text
h[0..2] = [1, 0.5, −0.25]; h[k]=0 outside these three entries
y[n] = x[n] + 0.5 x[n−1] − 0.25 x[n−2]
```

Each row contains two unit Delays and an Adder whose weights are the three
coefficients. All **six Delay initial values are zero**. The top row filters
a unit sample at n=4. The middle row filters a weighted unit sample at n=6.
The bottom row filters their combined input. Zoom into a row to inspect its
weights, ports and connections. There is no special C++ convolution element.

The sources each have area **0.05 V·s**, giving **1 V height at 20 Hz**.
The middle row's Gain `weight` sets the second pulse's amplitude b, initially
−0.5. Its output feeds both the middle filter and the combined input:

```text
x[n] = δ[n−4] + b δ[n−6] V; b=−0.5
first contribution[n]  = h[n−4] V
second contribution[n] = b h[n−6] V
shifted sum[n] = first contribution[n] + second contribution[n]
difference[n] = actual[n] − shifted sum[n]
```

Here δ is a unit-height **discrete sample**, not a unit-area continuous
impulse. The weights are dimensionless; all displayed signals are F64 volts.
The two separately executed rows illustrate superposition. They are not
the independent test oracle: the headless test uses a separate finite sum.

## Predict the overlap

| n | Scope time (s) | First contribution (V) | Second contribution (V) | Actual / shifted sum (V) |
| --- | --- | --- | --- | --- |
| 4 | 0.25 | 1 | 0 | 1 |
| 5 | 0.30 | 0.5 | 0 | 0.5 |
| 6 | 0.35 | −0.25 | −0.5 | −0.75 |
| 7 | 0.40 | 0 | −0.25 | −0.25 |
| 8 | 0.45 | 0 | 0.125 | 0.125 |

Compute n=6 yourself. Click **Run**, keep **2 s at 20 Hz**, and confirm the
intentional 40-sample run. Open **Convolution comparison** and **Shifted
contributions**. Inspect **0–0.6 s**. The actual and shifted-sum curves overlap;
hide one with the legend to inspect the other. Difference should stay at zero.
Outside n=4…8, all zero-state outputs are zero.

Sample n is the calculation at signal time n/20 s; Scope labels it at
(n+1)/20 s. Thus the first pulse's start_time=0.2 s appears at **0.25 s**.
This common observation convention does not add a delay to one path.
Straight segments between samples are drawing aids, not a continuous-time
impulse response. To verify a value, inspect its sample timestamp.

## Change one pulse, preserve linearity

Select **Second pulse weight: edit factor** (`weight`), press **F4**, change
**factor −0.5 → 0.75**, save your own copy, reopen and rerun. Predict the last
three nonzero values: **0.5, 0.375, −0.1875 V**. The first contribution stays
unchanged; the second contribution and actual output change. Difference stays
zero. The shared Gain ensures both paths use the same edited input.

Changing the input does not change h. As an extension, predict a different
second amplitude before running. Do not change the run rate alone if you
intend the same discrete pulse indices: source event times and areas are
physical quantities, so their sample indices/heights depend on the rate.

## Break the zero-state assumption

Restore b=−0.5. In the **bottom row**, select **Two samples of input history**
(`output_d2`), press **F4**, change **initial_value 0 → 1** and rerun.
Before either pulse arrives, predict **−0.25 V at n=0 / Scope time 0.05 s**.
The difference is also −0.25 V there, then returns to zero. This represents
x[−2]=1 V in the bottom filter only. The graph is executing correctly; the
zero-input-history comparison is now false. Restore that Delay to zero
before using the zero-state convolution formula. F4 shows the actual values;
explanatory node labels are not automatically rewritten by parameter edits.

## Independent verification

The [headless test](../../../s7tests/education_lti_smoke.cpp) loads the
[actual saved diagram](../../../examples/education/sp05_convolution.s7diagram)
with the production loader, validates and compiles it, and observes every
sample. It independently computes `sum h[j] x[n−j]`, checks the two shifted
finite responses, repeats a fresh run, tests the amplitude edit and checks
the declared nonzero-history case. The reference does not execute another S7
model to obtain an expected answer.

```bash
cmake --build build-release \
  --target s7tests_education_lti --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_lti$' --output-on-failure -V
```

F64 sample budget: `1e−10 × max(1, |reference|)` V; timestamp budget 1e−12 s,
with separately checked integer-nanosecond rounding. The named small binary
weights also permit exact equality checks in this build. These are finite
local contracts, not a claim of cross-platform bitwise equality. Studio
separately checks the journal, every plotted point, units, names, time axes,
guide links, F4 edit and save/reopen/rerun. Source links require the checkout;
the guide is available offline. Installed packages and native Windows remain
unqualified.

## Reading and next steps

[MIT 6.003 lecture 8 — Convolution](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec08/)
provides original teaching material on LTI responses and convolution
(2011 notes, checked 2026-11-19). The S7 graph, pulse choices and explanation
are original; no named MIT assignment is reproduced. No handout or solution
key is bundled. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [SP03 — aliasing](../SP03-aliasing/README.md).
Next: [SP06 — three-tap FIR](../SP06-three-tap-fir/README.md), then
[SP07 — filter transfer](../SP07-filter-transfer/README.md).
[Return to the learning route](../../../examples/education/README.md).
