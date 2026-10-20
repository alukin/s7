# PI/PID control laboratory

Open one of the diagrams below in Studio and run **12 seconds at 1 kHz**.
Open `response_scope` for normalized reference/output/error and
`actuator_scope` for limited/unlimited control and load. The motor diagrams
also have `motor_scope` with physical current and speed.

| Controller | First-order plant | DC motor |
|---|---|---|
| Open loop | [diagram](first_order_open_loop.s7diagram) | [diagram](dc_motor_open_loop.s7diagram) |
| P | [diagram](first_order_p.s7diagram) | [diagram](dc_motor_p.s7diagram) |
| PI | [diagram](first_order_pi.s7diagram) | [diagram](dc_motor_pi.s7diagram) |
| Filtered PID | [diagram](first_order_pid.s7diagram) | [diagram](dc_motor_pid.s7diagram) |
| PI without anti-windup | [diagram](first_order_pi_no_aw.s7diagram) | [diagram](dc_motor_pi_no_aw.s7diagram) |

**Experimental teaching models.** Automated checks cover the saved graphs and
the numerical envelope below. Manual Studio open/run/observe, nested edit,
save/reopen and rerun remain unqualified. No production motor or controller
qualification is implied.

## Experiment schedule and initial conditions

All plant, integral and derivative-filter states start at zero. Applied
reference is dimensionless; the motor reference is speed divided by 5 rad/s.

| Model time | Reference | Disturbance |
|---|---|---|
| 0–0.5 s | 0 | 0 |
| 0.5–2 s | 0.6 | 0 |
| 2–4 s | 2, deliberately unreachable | 0 |
| 4–6 s | 0.6 | 0 |
| 6–12 s | 0.6 | opposing load: 0.2 for first order, 0.1 N m for motor |

The overload is intentional: compare `unlimited` with `control` and watch
recovery after 4 s. An actuator clamped to [-1,1] cannot meet the overload
reference. Initial-state publication is at runtime step 1; the published state
at step k represents `(k-1)*dt`, while Scope labels that observation `k*dt`.
Sources and controls in that sample act on the published state and stage the
next state.

## Plants

First order, normalized input and output, time constant tau = 0.5 s:

```
tau*y' = u - y - load
y(0) = 0
```

With constant inputs its exact response is
`y(t) = u-load + (y(0)-u+load)*exp(-t/tau)`.
The explicit Adder and EulerIntegrator realize the same dynamics as the bundled
FirstOrderLag, with a subtractive load. The plant integrator stays visible at
the outer feedback boundary: a nested schema remains direct feedthrough, so
its private integrator alone would not authorize the outer cycle.

DC motor, ideal armature voltage `Va = 4*u` V:

```
L*i' = Va - R*i - Ke*omega
J*omega' = Kt*i - b*omega - load_torque
y = omega / 5
```

R = 1 ohm, L = 0.1 H, J = 0.1 kg m², b = 0.1 N m s/rad,
Kt = 0.5 N m/A, Ke = 0.5 V s/rad. Initial i = 0 A and omega = 0 rad/s.
Both state integrators use forward Euler with the same old states.
At steady state, `omega = (2*u-load_torque)/0.35` rad/s. The maximum
unloaded normalized speed is about 1.143, so a reference of 2 is unreachable.
There is no current limit, friction deadband, gear train or switching bridge.

## Controllers and reusable components

Open loop applies `clamp(reference,-1,1)` without output feedback. P applies
`clamp(1.2*error,-1,1)` and has **no** hidden integral state.
PI and PID are editable embedded copies of the bundled `PIController` and
`PIDController` schemas from
[`schema_elements.s7library`](../../s7core/resources/schema_elements.s7library).

```
error = reference - y
PI raw = kp*error + integral
filter_rate = (y-filter_state)/tau_filter
PID raw = kp*error + integral - kd*filter_rate
u = clamp(raw,-1,1)
integral' = ki*error + tracking_gain*(u-raw)
filter_state' = filter_rate
```

kp = 1.2, ki = 2/s, kd = 0.02 s and tau_filter = 0.03 s. The filter is
used only by PID; derivative acts on measurement to avoid reference-step kick.
Tracking gain is 10/s, or zero in `pi_no_aw`. Internal integral/filter
updates are causal forward Euler. Gains are signal inputs, so their constants
are editable without changing the component's interface.

Expected behavior: P retains a steady offset and open loop cannot reject the
load. PI/PID recover to 0.6 after the load step. PI without anti-windup
accumulates a large integral during saturation and recovers more slowly;
it need not settle during the default 12 s run. PID is not assumed to outperform
PI on every metric with this illustrative tuning.

## Suggested experiments

1. Compare all five methods using the same schedule. Record final error,
   overshoot, peak control and the integral of absolute tracking error over
   4–6 s. Distinguish unreachable commands from controller defects.
2. Double-click a PI/PID node to open its nested graph. Change `i_rate` tracking
   weight from 10 to zero, then rerun the root diagram. Compare with `pi_no_aw`.
   Save/reopen the root to check that the instance edit survives.
3. Change kp and ki separately. Too much gain can destabilize the loop;
   a smooth trace alone is not an accuracy check.
4. Open the PID's nested graph and inspect the filter state. Reduce tau_filter
   cautiously, keeping it well above dt; compare control transients. Set kd to
   zero to recover the PI equations. Do not differentiate reference steps.
5. Change motor load torque or voltage scale and calculate the new equilibrium.
   Change each coefficient consistently when changing L, J, R, Kt, Ke or b.

## Checks, rate changes and limits

From the repository root:

```bash
python3 tools/generate_control_examples.py
cmake -S . -B build
cmake --build build --target s7tests_control_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_control_examples_smoke$' --output-on-failure
```

The generator recreates these diagrams and the buck/relay exercises. It copies
bundled controller definitions into each file so nested tuning is saved with
that instance; it never changes the registry resource.

The test checks all ten control graphs at 1 and 2 kHz against independent scalar
discrete equations for every published plant state, controller output, unlimited
output, source transition and timestamp. Maximum recurrence mismatch must be
below 1e-9. PI/PID final tracking error must be below 0.015; P final offset must
exceed 0.05 and open-loop offset 0.02. Anti-windup PI must have at least 20% lower recovery
error integral over 4–6 s than PI with tracking disabled. Independent RK4 plant
references also replay the sampled control and load, held over each step.
Reference self-refinement must agree within 1e-8; maximum plant-state error
must be below 0.05 in the stated units and decrease by a factor of 1.7–2.3
when dt halves. This checks plant discretization under the actual applied
control; it does not prove arbitrary continuous-time closed-loop accuracy.

**StepGenerator start_sample is a sample count.** The saved schedule assumes
1 kHz. To repeat at 2 kHz with unchanged physical event times, double all
start_sample values, set all rates to 2 kHz, and set all integrator dt values
(including embedded controller dt) to 0.0005 s. Merely changing the run rate
does not rescale event sample counts. The headless test does this explicitly.
Never compare different rates with different physical input schedules.
The examples are single-rate F64 models; parameter changes and higher gains
require fresh stability and timestep checks.
