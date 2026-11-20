# FN05 — Three rules for the same decay

**40 minutes · Prerequisite: [FN04](../FN04-impulse-transfer/README.md).**

**Your question:** which discrete approximation best follows `x(t)=exp(−t) V`?
The physical model is `dx/dt = −x/(1 s)`, x(0)=1 V.

## Derive, then run

With h measured in seconds, solve each *linear* update algebraically:

| Rule | Update x[n+1] = p x[n] | p at h=0.5 s | x[1] (V) |
| --- | --- | --- | --- |
| Forward Euler | p=1−h | 0.5 | 0.5 |
| Backward Euler | p=1/(1+h) | 2/3 | 0.666667 |
| Trapezoidal | p=(1−h/2)/(1+h/2) | 0.6 | 0.6 |
| Exact sampled decay | p=exp(−h) | 0.606531 | 0.606531 |

For example, backward Euler gives `x[n+1]=x[n]−h x[n+1]`;
move the last term to the left and divide by 1+h. The new drawing implements
these solved scalar formulas as **three independent Delay/Gain loops**.
It does not solve an arbitrary implicit feedback graph. Each Delay contains
x[0]=1 V and each Gain calculates the next value. There is no new solver.

Open **Help → Education: start here → fn05_integration_rules.s7diagram** from its guide in Studio Help.
Predict x[2] for each rule. Click **Run**, retain **6 s at 2 Hz**, and
confirm the intentional 12-sample run. Open **Compare the named signals**.
First you see the shared initial value 1 V, then the values in the table.
The mathematical state is x[n] at physical time n h; Scope labels that
stored state at `(n+1)h`. Subtract **0.5 s** from Scope time before comparing
with exp(−t). Curves can overlap; use channel controls to inspect each.

## Make a deliberately inconsistent coefficient

Select the forward row's **Next = 0.5 × state** (`forward_pole`), press
**F4**, change **factor 0.5 → 0.75**, save a copy, reopen and rerun at **2 Hz**.
Its first update is now 0.75 V, but forward Euler at h=0.5 requires 0.5 V.
The runtime follows the stored graph correctly; the coefficient no longer
represents the declared physical model and method. Labels remain explanatory;
F4 shows the actual value. Restore 0.5 before the refinement exercise.

## Refine the same physical model

For h=0.25 s, **both** the run frequency and all three coefficients must change:

| Setting | Original | Refined |
| --- | --- | --- |
| Run frequency | 2 Hz | 4 Hz |
| forward_pole.factor | 0.5 | 0.75 |
| backward_pole.factor | 0.6666666666666666 | 0.8 |
| trapezoid_pole.factor | 0.6 | 0.7777777777777778 |

Keep duration 6 s. These gains are fixed numbers; changing Run frequency
alone does not redesign them. Compare with exp(−n h) using each run's own h.
The test verifies first-order error reduction for forward/backward Euler
and second-order reduction for trapezoidal on this smooth scalar problem.
This does not imply the same accuracy for discontinuous or nonlinear systems.

## Independent reference

Each discrete oracle is `x[n]=p^n V`, evaluated directly, rather than by
copying the loop's recurrence. The test checks every saved sample, the wrong
coefficient and correctly redesigned h=0.25 versions. At h=0.5 the maximum
continuous-reference errors are bounded by 0.12 V (forward), 0.08 V (backward)
and 0.009 V (trapezoid). The coarse/fine error ratios must be 1.7–2.5 for
the first two and 3.8–4.5 for trapezoidal. These envelopes apply only here.

## Run the independent checks

Use a current Linux S7/Studio development build; no hardware or plugin is
needed. The [saved graph](fn05_integration_rules.s7diagram)
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

[MIT 6.003 lecture 7: Discrete approximation of continuous-time systems](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec07/)
provides public teaching material for this topic (resource page checked
2026-11-19). This original S7 experiment does not reproduce an MIT problem
or imply MIT endorsement. Its mathematical references are derived above;
no MIT handout or solution key is bundled.

The yellow diagram frame opens this offline guide; its MIT link opens the
online material. MIT links can also be opened in the external browser
through Help. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [FN04](../FN04-impulse-transfer/README.md).
Next: [FN06](../FN06-pole-maps/README.md).
[Return to the learning route](../README.md).
