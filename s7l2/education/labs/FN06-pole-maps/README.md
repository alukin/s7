# FN06 — Stable does not mean accurate

**35 minutes · Prerequisite: [FN05](../FN05-integration-rules/README.md).**

**Your question:** does a stable numerical pole guarantee a faithful transient?
Use the same physical decay `dx/dt=−x/(1 s)`, now with an intentionally
large **h=3 s**. Its exact solution stays positive and tends to zero.

## Predict the poles

For a continuous pole s, the four sampled poles are
`1+h s`, `1/(1−h s)`, `(1+h s/2)/(1−h s/2)` and `exp(h s)`.
Here s=−1 per second:

| Mode | Pole | x[0], x[1], x[2] (V) | Prediction |
| --- | --- | --- | --- |
| Forward Euler | −2 | 1, −2, 4 | Alternating growth |
| Backward Euler | 0.25 | 1, 0.25, 0.0625 | Positive, but too slow |
| Trapezoidal | −0.2 | 1, −0.2, 0.04 | Decays with invented sign changes |
| Exact | exp(−3) | 1, 0.049787, 0.002479 | Positive physical decay |

Open **education → fn06_pole_maps.s7diagram**. Click **Run**, keep
**30 s at 0.333333… Hz**, and confirm the intentional 10-sample run.
Open **Forward Euler: growing error** and **Stable methods: inspect first 12 s**.
Use **0–12 s** on the second Scope to compare early samples. Separate plots
prevent forward Euler's large values from hiding the stable modes.
Each Delay holds x[0]=1 V. State x[n] represents physical time 3n s,
while Scope labels it at 3(n+1) s. Subtract **3 s** for an exact-curve comparison.

All four loops contain explicit Delay/Gain elements. Backward and trapezoidal
coefficients are algebraically solved for this scalar linear model, as in
FN05. No implicit nonlinear or global ODE solver is being invoked.

## Separate initial value from pole

Select the backward row's **Backward state: starts at 1 V** (`backward`),
press **F4**, change **initial_value 1 → 2**, save a copy, reopen and rerun.
Predict **2, 0.5, 0.125 V**. Doubling initial state doubles the response;
the pole remains 0.25. F4 shows the actual value; the teaching title does
not update automatically. This distinguishes amplitude from stability.

## Locate the forward-Euler boundary

For s=−1, decay requires `|1−h|<1`, hence **0<h<2 s**.
At h=2, p=−1 gives persistent alternation; at h=2.1, p=−1.1 grows.
The headless test constructs h=1.9, 2 and 2.1 variants and recomputes all
coefficients from the stated maps. If exploring manually, change all Gain
coefficients and Run frequency together, as explained in FN05.
Changing frequency alone merely changes the clock attached to fixed loops.

Trapezoidal remains stable for this negative real pole at h=3, yet its
negative discrete pole produces the wrong transient shape. Stability and
accuracy are separate questions. No blanket claim about all circuits,
nonlinear systems or complex modes follows from this scalar experiment.

## Independent reference

The test compares all saved channels with `p^n`, using an initial multiplier
of 2 only for the edited backward row. It checks growing forward magnitude,
alternating trapezoidal sign, the positive exact mode and the three boundary
variants. There is deliberately no small continuous-error claim at h=3.
The exact sampled mode is an analytic benchmark, not a runtime ODE solver.

## Run the independent checks

Use a current Linux S7/Studio development build; no hardware or plugin is
needed. The [saved graph](../../../examples/education/fn06_pole_maps.s7diagram)
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

Previous: [FN05](../FN05-integration-rules/README.md).
Next: [CT01](../CT01-first-order/README.md).
[Return to the learning route](../../../examples/education/README.md).
