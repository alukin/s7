# FN01 — One sample of memory

**30 minutes · Beginner · Prerequisites: fractions and addition.**
Use the current Linux S7/Studio development build. No hardware, plugin or
internet connection is needed to run. Status: `diagram-ready`, with automated
development evidence recorded [separately](../../EVIDENCE.md#fn01fn03-foundations).

**Your question:** how can a block use yesterday's answer without using
tomorrow's answer? You will predict four values, follow the feedback path,
recognize a one-sample delay and explain when this system stops settling.

## Start here

1. **File → Example Diagrams… → education → fn01_unit_delay.s7diagram**.
2. Read the rule in **Predict four samples** below. Calculate the first four
   outputs before checking the table's last column.
3. Click **Run**. The saved settings are **2 seconds, 10 Hz**. Keep them and
   confirm. The short-run notice is expected: this lesson needs only 20 samples.
4. Double-click **Compare the named signals** to open Scope. Read the
   `input`, `output` and `previous` channels, all in volts (V).

Use **Open FN01 student lab** in the yellow frame to return here. Ctrl+mouse wheel zooms the
diagram; **View → Fit Diagram to Window** shows the whole graph again.

## Predict four samples

The input is a constant 1 V. The system adds that input to half of the previous
output. The Delay initially stores 0 V:

```text
y[n] = 1 + 0.5 y[n−1],  n = 0, 1, 2, ...
y[−1] = 0 V
```

The index n counts samples, starting with zero. It does not count seconds.

| n | Scope time (s) | Input (V) | Previous output (V) | Predicted output (V) |
| --- | --- | --- | --- | --- |
| 0 | 0.1 | 1 | 0 | 1 |
| 1 | 0.2 | 1 | 1 | 1.5 |
| 2 | 0.3 | 1 | 1.5 | 1.75 |
| 3 | 0.4 | 1 | 1.75 | 1.875 |

Work out the four outputs before checking the final column. What value should the output
approach? If it settles at L, then `L=1+0.5L`, so L=2 V.

## Follow the blocks and measure

The [new graph](../../../examples/education/fn01_unit_delay.s7diagram) contains
one Constant, one Adder, one Delay and one Gain. Follow the loop:
**output → Delay → Gain → Adder → output**. The gain 0.5 has no units.
Only the Delay supplies previous-sample memory; the Adder uses the current input.
The Scope observes the signals and does not participate in this calculation.

Inspect the first 0.4 s in Scope, then the full 2 s. `previous` should match
`output` one sample earlier. Use the Scope channel controls if one line hides
another. There are exactly **20 samples per channel**, ending at Scope time 2 s.
Joining those samples with lines does not create additional computed values.

S7 labels the result of runtime step k at `k/10` seconds. Our mathematical
index is `n=k−1`. Thus the first displayed sample is n=0 at 0.1 s, not an
unrecorded sample at time zero. At that first sample the Delay publishes 0 V;
the current Adder output is already 1 V.

## Change one thing

Select the triangle **Remember half: a = 0.5** (`output_gain`) and press **F4**.
Change **factor** to **1.25**. Predict the first two outputs before rerunning:
they are 1 V and 2.25 V. Save a copy with **File → Save As**, reopen it and run.

The model still executes, but it no longer meets the prediction that the
output settles at 2 V. Its 20th output is about **342.94 V**. This is a
deliberate unstable-model experiment, not a numerical overflow or an S7 fault.
The visible name is a label; after an edit, the F4 **factor** is authoritative.
Restore 0.5 afterward, or reopen the original example.

**Explain:** why does multiplying the remembered value by more than one
change the behavior? As an extension, try 0.25 and predict the new limit
`1/(1−0.25) V`. This extension is not part of the recorded acceptance envelope.

Removing the Delay is a different mistake. It creates a direct feedback cycle.
S7 rejects that graph before execution; it does not secretly solve the
algebraic equation `y=1+0.5y`. The automated negative check exercises this
case. Do not remove the Delay just to make the picture simpler.

## Check independently

The closed-form reference is
`y[n]=(1−a^(n+1))/(1−a)` V for this input and zero stored past.
For a=0.5 this is `2−0.5^n` V. The
[headless test](../../../s7tests/education_foundations_smoke.cpp) loads and
executes the saved graph using the production schema loader, samples all
20 values and checks the previous-output channel and timestamps. It also
checks the edited gain, missing-memory rejection and repeat/reset behavior.

```bash
# From s7l2/; add -DS7_BUILD_STUDIO=OFF when configuring a headless build.
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release \
  --target s7tests_education_foundations --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_foundations$' \
  --output-on-failure -V
```

The frozen local envelope is F64, 10 Hz, 20 samples, the saved parameters
and the named test variations. Absolute sample tolerance is 1e−12 V for the
baseline and 1e−10 V for the growing response: these cover short floating-point
sums, not continuous-model approximation. No global solver is involved.
If a result differs, check the rate, gain and Delay's **initial_value** before
changing tolerances. Release-package and native Windows qualification remain open.

## MIT readings and next lesson

This is an original S7 topic illustration, not a reproduced MIT problem.
Public readings checked 2026-11-19:

- [Mahajan–Freeman, MIT 6.003: difference equations](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap1/).
- [MIT 6.003: block diagrams and operators](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap3/).

The yellow frame links to the same MIT teaching materials in your external
browser; this guide is available offline. No MIT handout is bundled.
S7 diagrams/generators: CC0-1.0; this guide: CC-BY-4.0.

Continue to [FN02 — Three ways to excite a system](../FN02-natural-forced/README.md),
or return to the [learning route](../../../examples/education/README.md).
