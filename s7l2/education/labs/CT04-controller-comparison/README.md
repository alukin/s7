# CT04 — Does integral action remove the load error?

**60 minutes · intermediate · prerequisites: CT01, CT03, FN01.**
Original normalized S7 experiment aligned with MIT feedback-control teaching.
Readings checked 2026-11-19. Current 0.2.0 development source, Linux Studio or
headless C++; no hardware, runtime plugin or network required.
Status: diagram-ready; [development evidence](../../EVIDENCE.md) records
executed checks separately from release/platform qualification.

Predict steady error, locate the effect of a load step, distinguish PI from
filtered derivative-on-measurement PID, and explain the actual sampled model.

## Predict before running

Three independent, identical normalized plants obey `dy/dt=u−y−d`, with
unit time constant **1 s** and initial output zero. The actuator is limited
to **−1≤u≤1**. Reference r, output y, error e, load d and command u are all
normalized, dimensionless quantities (unit **1**); these are not volts or
motor speeds. No physical hardware calibration is claimed.

Run **16 s at 100 Hz** (h=0.01 s, 1600 samples). Reference changes
0→0.5 at source time 1 s; opposing load changes 0→0.2 at 8 s. Source/state
index n is observed at Scope time **(n+1)h**, after runtime step n+1.
That sample uses old states; the staged update appears in the following sample.
Thus the first reference step is plotted at 1.01 s and the first changed load
at 8.01 s. The state cannot jump at either transition.

| Loop | Before load, steady y | After load, steady y | After-load error r−y |
| --- | --- | --- | --- |
| P, Kp=2 | 1/3 | 4/15 ≈ 0.266667 | 7/30 ≈ 0.233333 |
| PI | 0.5 | 0.5 | 0 |
| Filtered PID | 0.5 | 0.5 | 0 |

These are stable unsaturated equilibria, not promises of exact equality at a
finite run time. After the load, integral loops need u=0.7 to hold y=0.5,
which is feasible. The P equilibrium follows `u=2(r−y)=y+d`.
A nonzero P error under a constant load is expected, not a viewer defect.

## Construct and inspect the causal network

Open [ct04_controller_comparison.s7diagram](../../../examples/education/ct04_controller_comparison.s7diagram)
from **File → Example Diagrams… → education**. Trace each feedback lane:
reference minus its own output → controller → bounded command →
plant derivative → **EulerIntegrator** → output. The plant integrator is
causal memory. Adding an outer Delay changes the model; removing plant
memory creates an invalid combinational feedback loop.

PI/PID are editable embedded copies of the reusable schema definitions in
`schema_elements.s7library`. They contain ordinary arithmetic, integrators
and a limiter. They are not special C++ plant/controller solvers. Kp, Ki,
Kd and filter time are signal inputs supplied by visible Constants.

Open the three observers and keep the saved Run profile:

1. **Tracking** (`scope`): reference and P/PI/PID outputs, unit 1.
2. **Applied actuator commands** (`actuator_scope`): the commands actually
   sent to the plants. Do not confuse these with unlimited demands.
3. **Tracking errors** (`error_scope`): expand 7–12 s to see rejection of
   the load. Compare PI and PID without assuming that more terms improve
   every performance measure.

Scope uses model time in seconds, not wall-clock execution time. Connecting
segments aid reading; the system under test is the sampled sequence.

## The exact discrete equations

All plant, integral and derivative-filter states start at zero. Gains:
Kp=2, Ki=1.5/s, Kd=0.15 s, filter τf=0.1 s. For every sample:

```text
e[n] = r[n] − y[n]
P:   v[n] = Kp e[n]
PI:  v[n] = Kp e[n] + I[n]
PID: g[n] = (y[n] − f[n])/τf
     v[n] = Kp e[n] + I[n] − Kd g[n]
u[n] = clamp(v[n], −1, 1)
y[n+1] = y[n] + h (u[n] − y[n] − d[n])
I[n+1] = I[n] + h Ki e[n]       (PI and PID)
f[n+1] = f[n] + h g[n]           (PID only)
```

Back-calculation is disabled in this tracking lesson (internal tracking
weight zero); CT05 isolates saturation and windup. The derivative acts on
**measurement**, not reference. At n=100, y=f=I=0, so all raw commands are
**1**, and the PID derivative term is **0**. At n=101 the plant output is
**0.01**. There is no artificial derivative kick on the reference step.
The filter is itself forward Euler, not a backward-difference approximation.

The continuous plant equation describes the intended physical analogy;
the equations above define the actual discrete experiment. Small recurrence
residuals verify implementation, not arbitrary continuous-time accuracy.

## Change one thing, save and rerun

Select **kd**, press **F4**, change **value 0.15 → 0**, save a copy, reopen
it and rerun. Predict **PID equals PI at every sample**, including the
actuator trace. The filter state still evolves internally, but its output
has zero weight. Overlapping curves after this edit represent equal data.
Restore Kd before comparing the original transient.

Try Run frequency 200 Hz with the same duration. Source `start_time` is in
seconds, and integrator rates follow the selected rate, so physical event
times stay fixed. Do not expect 100 Hz and 200 Hz samples to be identical.

A deliberately poor numerical choice is **4 Hz**, h=0.25 s. The isolated
filter pole is `1−h/τf=−1.5`; its memory grows with alternating sign. Applied
control can remain clipped while internal filter values become very large.
This is a numerical failure experiment, not improved damping or proof of
stability from bounded actuator/output plots. Restore 100 Hz afterward.

## Verify and explain

The [headless test](../../../s7tests/education_control_smoke.cpp) loads the
exact saved graph through the production loader, validator and compiler,
then uses read-only observation. An [independent scalar reference](../../../s7tests/education_control_reference.hpp)
checks every state/command at 100 and 200 Hz, both original and edited.
An additional closed-form geometric response checks the P lane. The coarse
filter and removal of plant memory are explicit failure cases.

Budget: `1e−10 × max(1, |reference|)` in each channel's units; model-time
coordinates within 1e−12 s, with integer-nanosecond rounding checked separately.
This conservative F64 budget detects wrong state order, gains and transitions.
The real Studio workflow checks channel bindings, journal values, displayed
points/extrema, axes/units and the edit/save/reopen/rerun sequence.

```bash
cmake --build build-release --target s7tests_education_control s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_control|s7studio_gui_education_control)$' --output-on-failure -V
```

## MIT readings and continuation

[MIT 16.06, Fall 2012, Lecture 10: PID Control](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_10/),
PDF pages 1–3, introduces the terms and discusses putting derivative action
on measurement. Its example plant and derivative realization differ from
this explicitly defined S7 experiment. This is topic alignment, not a
reproduction of its worked problem.

[MIT 2.017J, Fall 2009, Feedback Control](https://ocw.mit.edu/courses/2-017j-design-of-electromechanical-robotic-systems-fall-2009/MIT2_017JF09_feedback.pdf),
PDF pages 13–14 and 20, provides further integral/derivative/windup context.
No handout, figure or solution is bundled. The signals, graph and references
are original S7 work. Diagram/generator: CC0-1.0; guide: CC-BY-4.0.

Next: [CT05 — actuator windup](../CT05-actuator-windup/README.md).
[Return to the learning route](../../../examples/education/README.md).
