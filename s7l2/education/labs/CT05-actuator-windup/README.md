# CT05 — Why does a clipped actuator keep pushing after release?

**60 minutes · intermediate · prerequisite: [CT04](../CT04-controller-comparison/README.md).**
Original normalized S7 saturation experiment, aligned with MIT feedback-control
teaching. Readings checked 2026-11-19. Current 0.2.0 development source on
Linux, Studio or headless C++; no hardware, runtime plugin or network required.
Status: diagram-ready; [development evidence](../../EVIDENCE.md) distinguishes
executed development checks from final release qualification.

You will distinguish limited command from unlimited demand, measure accumulated
integral state, explain recovery after an unreachable command, and disable
back-calculation in an instance without changing the reusable library.

## Predict: a command the actuator cannot meet

Two independent normalized first-order plants have time constant **0.8 s**,
initial output 0, no load, and actuator range **−1…1**. Both PI controllers
have Kp=1.5 and Ki=2/s, initial integral zero. All displayed signals have
normalized dimensionless unit **1**; integral state I already has command
units, because Ki is applied before integration.

Run **12 s at 100 Hz**, h=0.01 s, 1200 samples. Source reference:

| Source/state time | Reference | Meaning |
| --- | --- | --- |
| 0≤t<1 s | 0 | Initially at rest |
| 1≤t<4 s | 1.5 | Deliberately unreachable: plant equilibrium cannot exceed 1 |
| 4≤t<12 s | 0.4 | Feasible again: compare recovery |

Index n is observed after step n+1 at Scope time **(n+1)/100 s**. The changes
appear at 1.01 and 4.01 s. At the first changed-reference sample, y=I=0,
unlimited demand is **2.25**, applied control is **1**. The output does not
jump; in the next observed sample it is **0.0125**.

- **free:** tracking gain 0/s. Error continues to accumulate while the
  actuator is clipped, although extra demand cannot increase its output.
- **aw:** back-calculation gain 6/s. The difference between actual and
  requested command feeds the integral rate. This does not make 1.5 feasible.

Predict why the plant can still receive +1 after reference falls to 0.4,
even though the tracking error is negative. A limiter bounds the actuator;
it does not automatically bound or reset integral memory.

## Run and inspect four different views

Open [ct05_actuator_windup.s7diagram](../../../examples/education/ct05_actuator_windup.s7diagram)
through **File → Example Diagrams… → education**. Run with the saved settings.
Open:

1. **Tracking** (`scope`): reference and both outputs. Both remain at or below
   1 during the unreachable command; tracking failure there is expected.
2. **Applied actuator commands** (`actuator_scope`): each remains in −1…1.
3. **Unlimited demand** (`raw_scope`): can exceed those limits. A wider
   vertical range here is correct, not inconsistent scaling.
4. **Integral states** (`state_scope`): compare memory at release and during
   recovery. `aw` can initially be negative; back-calculation is not clamping
   I to zero or to the actuator interval.

Keep raw demand and integral memory separate from applied control so large
values cannot hide the bounded traces. Each plot labels model time (s) and
value (1). Inspect 3.5–8 s, keeping both traces visible. The line segments
connect sampled values; event timing is determined by the sample timestamps.

## Exact state and limit equations

For each lane, with its own y and I and common reference:

```text
e[n] = r[n] − y[n]
v[n] = 1.5 e[n] + I[n]
u[n] = clamp(v[n], −1, 1)
y[n+1] = y[n] + (h/0.8) (u[n] − y[n])
I[n+1] = I[n] + h (2 e[n] + Kt (u[n] − v[n]))
Kt = 6/s for aw; 0/s for free
```

All right-hand sides use the same old states. At onset the corrected
integral rate is `2×1.5 + 6×(1−2.25)=−4.5/s`, so its next value is **−0.045**;
the uncorrected integral becomes **0.03**. This early comparison detects
wrong correction sign and state-publication order.

Back-calculation corrects the *difference* between desired and achievable
command. It is distinct from integral clamping, conditional integration or
reset-on-release. The actual PI network is an embedded instance of S7's
reusable schema definition, with one extra read-only integral output for
observation. No observer changes the controller state.

For a repeatable recovery metric, use the fixed source-index interval
**400≤n<800**, plotted times **4.01…8.00 s**:
`IAE = h Σ |y[n]−0.4|`. The development check requires aw's IAE to be less
than 60% of free's for these parameters. This is a declared experiment,
not a universal anti-windup performance guarantee. Final y_aw is within
0.005 of 0.4; steady error during the 1.5 command is intentionally nonzero.

## Disable the correction in this instance

1. Double-click **aw** to open its nested schema tab.
2. Locate **i_rate** (pan/zoom the nested canvas if needed), select it, press **F4**, change **weights [1,6] → [1,0]**.
   The first weight multiplies Ki×error; the second multiplies u−v.
3. Return to the **root diagram tab** before Run. Running the child alone
   does not provide the outer loop's reference, gains and plant connections.
4. Save a copy, reopen it and rerun. Predict that aw and free now match at
   **every sample**: output, actual command, unlimited demand and integral.
   Overlapping traces are expected. Inspect the actual saved weight rather
   than the unchanged descriptive title.

The edit belongs to this embedded controller instance. It does not rewrite
the library or the other lane. Restore [1,6] to compare again.
As an extension, shorten the overload by moving `release.start_time` from
4 to 2 s; predict less stored integral in free before rerunning.

## Independent verification and limitations

The [headless test](../../../s7tests/education_control_smoke.cpp) executes the
exact saved graph through the production loader/validator/compiler and
read-only observation. A [separate scalar recurrence](../../../s7tests/education_control_reference.hpp)
checks every exposed source, state, error, applied command and unlimited
demand, including the nested edit, at 100 and 200 Hz. It verifies bounded
actuation, infeasible tracking, excess stored integral and fixed-window IAE.

F64 recurrence budget: `1e−10 × max(1, |reference|)` per channel; time budget
1e−12 s plus separately checked nanosecond rounding. The Studio workflow
checks actual channel bindings, every journal value, plotted sample subsets
and extrema, axes/units, nested F4 edit and save/reopen/rerun persistence.
These checks establish the specified sampled model, not a physical actuator
or arbitrary continuous closed-loop accuracy. Native Windows, installed
packages and the final collection-wide viewer audit remain open.

```bash
cmake --build build-release --target s7tests_education_control s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_control|s7studio_gui_education_control)$' --output-on-failure -V
```

## MIT reading and rights

[MIT 2.017J, Fall 2009: Feedback Control](https://ocw.mit.edu/courses/2-017j-design-of-electromechanical-robotic-systems-fall-2009/MIT2_017JF09_feedback.pdf),
PDF page 20, describes accumulation during constrained response and recovery.
It suggests constraining the integral; this S7 lab instead explicitly tests
back-calculation. No claim that the notes prescribe this exact algorithm.
[MIT 16.06, Fall 2012: PID Control, Lecture 10](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_10/)
provides the underlying proportional/integral context.

This is original S7 topic alignment, not an MIT assignment, worked solution
or endorsement. No MIT PDF or figure is bundled. Diagram/generator:
CC0-1.0; guide: CC-BY-4.0. See [sources](../../SOURCES.md).
[Return to the learning route](../../../examples/education/README.md).
