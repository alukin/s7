# FN02 — Three ways to excite a system

**30 minutes · Beginner · Prerequisite: [FN01](../FN01-unit-delay/README.md).**
Current Linux S7/Studio development build; no hardware or plugin required.
Status: `diagram-ready`; [development evidence](../../EVIDENCE.md#fn01fn03-foundations)
is distinct from release qualification.

**Your question:** can a system produce an output when its input is zero?
You will distinguish a natural response from two forced responses, predict
when each begins and check that changing one input leaves the others alone.

## Open, predict, then run

Open **Help → Education: start here → fn02_natural_forced.s7diagram**.
The three rows have the same rule, `y[n]=u[n]+0.5 y[n−1]`, with different
inputs or stored past. All inputs and outputs are F64 voltages; the gain is
dimensionless. Each loop is a new explicit Delay/Gain/Adder network.

| Scope channel | Input | Delay's stored past y[−1] | What it isolates |
| --- | --- | --- | --- |
| `natural` | Always 0 V | 2 V | Effect of the initial state alone |
| `impulse response` | One 1 V sample at n=5 | 0 V | Effect of one input sample |
| `step response` | 1 V from n=5 onward | 0 V | Effect of a sustained input |

The natural response exists without an external input. Here "stored past"
means a signal value in the Delay, not a claim about physical stored energy.
The forced cases begin with zero memory, so all their output comes from input.

Before reading the table, calculate n=0, n=5 and n=6. Then check your results:

| n | Scope time (s) | Natural (V) | Impulse response (V) | Step response (V) |
| --- | --- | --- | --- | --- |
| 0 | 0.1 | 1 | 0 | 0 |
| 1 | 0.2 | 0.5 | 0 | 0 |
| 4 | 0.5 | 0.0625 | 0 | 0 |
| 5 | 0.6 | 0.03125 | 1 | 1 |
| 6 | 0.7 | 0.015625 | 0.5 | 1.5 |
| 7 | 0.8 | 0.0078125 | 0.25 | 1.75 |

Click **Run**, keep the saved **2 seconds, 10 Hz** and confirm. The notice
about 20 samples is intentional. Double-click **Compare the named signals**
to open Scope. Inspect all three channels over the full two seconds. Why do
the forced curves agree at n=5 but differ afterward?

The stimulus setting **start_time = 0.5 s** selects source sample n=5.
Scope timestamps completed steps: sample n appears at `(n+1)/10 s`, so the
first forced response is displayed at **0.6 s**. This is a labeling convention,
not an extra Delay. Every channel contains exactly 20 samples.

## Change the sustained input

Select **Sustained: 1 V from n=5** (`step`), press **F4**, and change
**final_value** from **1 to 2**. Save a copy, reopen it and rerun at the
same settings. Predict which channels change before looking.

Only `step response` should double. `natural` and `impulse response` should
remain exactly as before because each loop owns its own state. The step
response now approaches 4 V. A parameter edit changes the parameter, not
the saved explanatory label; consult F4 when comparing an edited copy.
Restore 1 or reopen the original before the next experiment.

## A useful mistake: height versus area

The one-sample input has **area = 0.1 V·s**, not 1 V·s. At a sample interval
of 0.1 s, its height is `area / 0.1 s = 1 V`.

Select **One sample: 1 V at n=5** (`impulse`) and set **area** to **1**.
Predict the new peak. It becomes **10 V**, and `impulse response` is ten
times its original value. A successful run does not make it a unit-height
input. This deliberately fails the intended 1 V input specification.
Restore area 0.1 afterward. Do not change sample rate to fix an area mistake.

As an extension, move only the step's **start_time** to 0.8 s and predict
its new first displayed timestamp. Leave the other two sources unchanged.
That extension is outside the recorded numerical acceptance envelope.

## Independent verification and limits

For a=0.5 and initial memory b, the independent closed-form/finite-sum rule is

```text
y[n] = a^(n+1) b + sum from j=0 to n of a^(n−j) u[j]
natural[n] = 0.5^n V
impulse_response[n] = 0 before n=5; 0.5^(n−5) V afterward
step_response[n] = 0 before n=5; 2(1−0.5^(n−4)) V afterward
```

The [test](../../../s7tests/education_foundations_smoke.cpp) executes the
[saved graph](fn02_natural_forced.s7diagram),
checks every response and source sample against these independent sums,
checks timestamps, repeats from fresh initial state, doubles the step and
tests the wrong impulse area. It uses the production loader and normal
runtime, not a replacement reference graph.

```bash
cmake --build build-release \
  --target s7tests_education_foundations --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_foundations$' \
  --output-on-failure -V
```

The tested envelope is the stated local F64 model at 10 Hz for 20 samples
and the specified variations; absolute error budget is 1e−12 V. This is an
exact discrete model, not an approximation of a continuous RC circuit.
Other rates change the physical duration of a unit sample. No continuous
physics accuracy or cross-platform bitwise identity is claimed. If a curve
starts early or has a wrong height, check source timing, area and Delay
initial values. Package and native Windows qualification remain open.

## Readings and next lesson

Original S7 exercise aligned by topic with Mahajan–Freeman's MIT 6.003
materials; not a copied assignment. Public links checked 2026-11-19:

- [Difference equations](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap1/).
- [Block diagrams and operators](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap3/).

Use the yellow frame for offline help or to open those readings in your
browser. No MIT material is redistributed. S7 diagrams/generators are CC0-1.0;
this guide is CC-BY-4.0.

Next: [FN03 — Where does the response come from?](../FN03-initial-conditions/README.md)
Previous: [FN01](../FN01-unit-delay/README.md) · [Learning route](../README.md).
