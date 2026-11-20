# FN04 — From one pulse to a transfer function

**35 minutes · Prerequisite: [FN03](../FN03-initial-conditions/README.md).**

**Your question:** can one pulse predict the response at every frequency?
This is a new discrete linear system, not a continuous RC approximation.
Its input and output are voltages; gains are dimensionless.

## Predict the pulse response

The graph implements `y[n] = 0.5 u[n] + 0.5 y[n−1]`, with stored past
`y[−1] = 0 V`. A one-sample input at n=4 has height **1 V**.
The ImpulseGenerator's area is **0.05 V·s**, because h=0.05 s and height=area/h.
It is a unit *discrete sample*, not a unit-area continuous impulse.

| n | Scope time (s) | Input (V) | Output (V) |
| --- | --- | --- | --- |
| 3 | 0.20 | 0 | 0 |
| 4 | 0.25 | 1 | 0.5 |
| 5 | 0.30 | 0 | 0.25 |
| 6 | 0.35 | 0 | 0.125 |

Before running, calculate the next output. Open **Help → Education: start here → fn04_impulse_transfer.s7diagram**. Click **Run**, keep the saved
**10 s at 20 Hz**, then double-click **Compare the named signals**.
Zoom the Scope time axis to **0–1 s** to see the pulse and its tail; the
full 10 s capture gives the frequency calculation a nearly complete decay.
Scope labels sample n at `(n+1)h`; source start_time=0.2 selects n=4,
which appears at 0.25 s. There is no extra input delay in the transfer.

## Connect the two views

Take the z transform with zero initial state:

```text
H(z) = Y(z)/U(z) = 0.5 / (1 − 0.5 z^−1)
H(exp(jω)) = 0.5 / (1 − 0.5 exp(−jω))
ω = 2π f / 20   (radians/sample; f is in Hz)
H(1) = 1; H(−1) = 1/3
```

Double-click **Frequency response**. Its measured impulse-response result
should show DC gain **1 (0 dB)** and a Nyquist limit of **1/3 (about −9.54 dB)**
at 10 Hz. The exact formula above is the reference; a finite capture and
the viewer's frequency grid need not place a marker at every exact frequency.
Input and output are captured together. Their common pulse-time shift
cancels in Y/X. The independent test evaluates complex Y/X at four frequencies.

## Change the memory weight

Select **Feedback weight = 0.5** (`feedback_gain`), press **F4**, change
**factor 0.5 → 0.8**, save a copy, reopen and rerun. Predict first:
output **0.5, 0.4, 0.32 V**, DC gain **2.5**, Nyquist gain **0.5/1.8**.
The input coefficient remains 0.5; increasing feedback changes the steady gain.
Parameter edits do not rewrite explanatory labels; F4 shows actual values.

A useful limit is a=1: the pulse response no longer decays, and the infinite
DC sum does not converge. A finite frequency plot cannot establish a stable
DC gain for that case. The tested edit is a=0.8, not this optional extension.

## Break the zero-state assumption

Restore feedback to 0.5. Select **Previous y: starts at 0 V** (`memory`),
press **F4**, change **initial_value 0 → 2**, then rerun. Predict output
**1 V before the pulse arrives**. This is natural response from stored
past, added to the pulse response. The graph still runs correctly, but
Y/X from this capture no longer measures the zero-state transfer above.
Its DC ratio becomes **3**, instead of 1. A plausible-looking frequency
plot cannot remove the initial-state contribution. Restore memory to 0
before making transfer measurements. The headless test verifies this
negative measurement case against the sum of the two closed forms.

## Independent reference

For feedback a, the exact output is zero before n=4 and
`0.5 a^(n−4) V` thereafter. The headless test compares all 200 samples,
a fresh repeat and a=0.8 against that formula. With stored past b=2,
it independently adds `b a^(n+1) V` and checks the misleading DC ratio. It separately forms finite
complex sums for input/output and compares their ratio with
`0.5/(1−a exp(−jω))` at ω=0, π/3, π/2, π (tolerance 1e−11).
No FFT library supplies that reference. The common sample checks are below.

## Run the independent checks

Use a current Linux S7/Studio development build; no hardware or plugin is
needed. The [saved graph](fn04_impulse_transfer.s7diagram)
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

[MIT 6.003 lecture 9: Frequency response](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec09/)
provides public teaching material for this topic (resource page checked
2026-11-19). This original S7 experiment does not reproduce an MIT problem
or imply MIT endorsement. Its mathematical references are derived above;
no MIT handout or solution key is bundled.

The yellow diagram frame opens this offline guide; its MIT link opens the
online material. MIT links can also be opened in the external browser
through Help. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [FN03](../FN03-initial-conditions/README.md).
Next: [FN05](../FN05-integration-rules/README.md).
[Return to the learning route](../README.md).
