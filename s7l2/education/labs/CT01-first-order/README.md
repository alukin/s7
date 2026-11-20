# CT01 — A capacitor takes time to charge

**35 minutes · Prerequisite: [FN05](../FN05-integration-rules/README.md).**

**Your question:** what does a one-second time constant mean?
For a 1 V supply charging a capacitor through a resistor, let τ=RC=1 s:
`dv/dt=(u−v)/τ`, v(0)=0 V. The supply is a constant, not an impulse.

## Read the units through the graph

Open **Help → Education: start here → ct01_first_order.s7diagram**. Trace the new network:
Constant supply (V) → subtract voltage (V) → divide by τ (s) →
derivative (V/s) → EulerIntegrator → voltage (V). The feedback returns
voltage to the subtraction. The integrator is the causal memory; no algebraic
loop or special C++ plant is hidden in the diagram.

At h=0.1 s, the physical and discrete references differ:

| Physical state time (s) | Scope time (s) | Euler voltage (V) | Exact voltage (V) |
| --- | --- | --- | --- |
| 0 | 0.1 | 0 | 0 |
| 0.1 | 0.2 | 0.1 | 0.095163 |
| 1 | 1.1 | 0.651322 | 0.632121 |

Predict the second update. Click **Run**, keep **5 s at 10 Hz**,
then open **Compare the named signals**. The supply remains 1 V; voltage
rises toward it. The first visible voltage is the initial state, not the
first updated state. For comparison use `t_state = t_Scope − 0.1 s`.
Fifty samples cover state times 0 through 4.9 s; Scope timestamps 0.1–5 s.
One time constant means about 63.2% of the physical charging step, not full charge.

## Make charging slower

Select **Time constant RC: 1 s** (`tau`), press **F4**, change **value 1 → 2**,
save a copy, reopen and rerun. Predict a lower voltage at the same elapsed
state time. The edited discrete pole is 0.95, so `v[n]=1−0.95^n V`.
The physical reference becomes `1−exp(−t/2) V`. The teaching title stays
unchanged; check the actual parameter in F4.

## Check the numerical model

For a constant supply and initial zero voltage:

```text
Euler: v[n] = 1 − (1−h/τ)^n V
Physical: v(t) = 1 − exp(−t/τ) V
```

Restore τ=1. Halve h by changing Run frequency **10 → 20 Hz**, retaining
5 s. This actual integrator uses the selected h automatically; there are
no stored pole gains to redesign. Maximum error should roughly halve.
The test bounds baseline physical error by 0.02 V and checks a coarse/fine
ratio between 1.8 and 2.2. It also checks every edited discrete sample.

A negative numerical case uses h=2.5 s (0.4 Hz): the first update becomes
**2.5 V**, above a passive 1 V supply. The graph can execute while its
coarse numerical approximation is physically misleading. This variant is
checked headlessly. Finite-run sample counts use floor(duration/h), so
keep a duration long enough to see several updates when exploring it.

## Run the independent checks

Use a current Linux S7/Studio development build; no hardware or plugin is
needed. The [saved graph](ct01_first_order.s7diagram)
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

[MIT 6.003 lecture 4: Continuous-time systems](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec04/)
provides public teaching material for this topic (resource page checked
2026-11-19). This original S7 experiment does not reproduce an MIT problem
or imply MIT endorsement. Its mathematical references are derived above;
no MIT handout or solution key is bundled.

The yellow diagram frame opens this offline guide; its MIT link opens the
online material. MIT links can also be opened in the external browser
through Help. Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [FN06](../FN06-pole-maps/README.md).
Next: [CT02](../CT02-damped-modes/README.md).
[Return to the learning route](../../index.md).
