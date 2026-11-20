# CT02 — How damping changes a spring

**45 minutes · Prerequisite: [CT01](../CT01-first-order/README.md).**

**Your question:** how does damping change an oscillation?
A mass starts displaced and at rest: m=1 kg, k=4 N/m, b=1 N·s/m,
x(0)=1 m and v(0)=0 m/s. There is no external force.

## Follow two stored states

The new diagram explicitly builds
`dx/dt=v`, `dv/dt=−4x−v` with two EulerIntegrators, two Gains and an Adder.
The spring gain is k/m (per s²); damping gain is b/m (per s).
Each integrator owns its own initial state. Both stage their next value
from the current pair; this is forward Euler, not sequential Euler–Cromer.

Predict the first update at h=0.01 s: **x[1]=1 m, v[1]=−0.04 m/s**.
Open **Help → Education: start here → ct02_damped_modes.s7diagram**, click **Run**, keep
**8 s at 100 Hz**, then open **Displacement (m)** and **Velocity (m/s)**.
Separate plots preserve the units instead of implying that metres and
metres/second share a scale. Expect damped oscillation about zero.
The Scope timestamps are `(n+1)h`; each displayed stored pair is at
physical state time n h. Subtract **0.01 s** for an analytic comparison.

## Predict a damping edit

Select **Damping / mass: 1 per s** (`damping`), press **F4**, change
**factor 1 → 3**, save a copy, reopen and rerun. Predict less overshoot and
fewer visible oscillations. The gain is b/m, **not the damping ratio**:
ζ=b/(2√(mk)), initially 0.25 and then 0.75. Both cases are underdamped.
Explanatory labels stay unchanged; F4 shows the actual gain.

## Compare with a physical mode

For either tested positive b, define `w=√(4−b²/4)` radians/second:

```text
x(t) = exp(−bt/2) [cos(wt) + b sin(wt)/(2w)] m
v(t) = −(4/w) exp(−bt/2) sin(wt) m/s
```

The test also uses an independent discrete formula. Let
`z = 1−bh/2 + jwh`, `q=z^n`. Then
`x[n]=Re(q)+b Im(q)/(2w)` and `v[n]=−4 Im(q)/w`.
It compares complex powers with every runtime sample, rather than
implementing the same pair of update loops as the graph.

Restore b=1. Change Run frequency **100 → 200 Hz**, retain 8 s, and compare
against the physical formulas at each run's own state times. The baseline
maximum errors must stay below **0.04 m** and **0.04 m/s**; coarse/fine
error ratios must be 1.8–2.2. These are finite-run bounds, not universal
accuracy guarantees. Forward Euler does not guarantee monotonically
decreasing mechanical energy at each discrete sample.

## Distinguish physical growth from numerical growth

The headless negative case sets damping to **−1 per s** at the same small h.
Its response grows because the model now supplies energy; this is active
negative damping. Growth alone does not prove numerical instability.
The test checks the new sequence against its own discrete complex modes.
Contrast with CT03, which retains a stable physical model and changes h.

## Run the independent checks

Use a current Linux S7/Studio development build; no hardware or plugin is
needed. The [saved graph](ct02_damped_modes.s7diagram)
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

Previous: [CT01](../CT01-first-order/README.md).
Next: [CT03](../CT03-sample-time/README.md).
[Return to the learning route](../README.md).
