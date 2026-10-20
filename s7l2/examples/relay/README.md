# Relay thermostat with heater inertia

Open [thermostat](thermostat.s7diagram) or
[narrow hysteresis band](thermostat_narrow_band.s7diagram). Run **1200 seconds
at 10 Hz**, dt = 0.1 s. Open `temperature_scope`, `heater_scope` and
`relay_scope`. The model is intentionally slow; model duration is not a
wall-clock execution requirement.

**Experimental teaching models.** Headless checks cover the default equations
at 10/20 Hz. Manual Studio open/run/observe/edit/rerun remains unqualified.
No new plant/controller C++ element is used: all states and local transitions
are visible in the graph.

## Physical model and relay

A room or thermal mass is heated by a 2000 W heater with a 10 s power-response
lag. Thermal capacitance Cth = 10000 J/K, heat loss G = 100 W/K, room time
constant Cth/G = 100 s. Setpoint is 20 degC. Initial room temperature is
18 degC and heater power is zero. Ambient temperature is 15 degC until
600 s, then drops to 10 degC.

```
P' = (2000*on - P)/10
T' = (P - G*(T-Ta))/Cth
error = 20 - T
```

Temperature differences are kelvins; absolute plotted values are degrees
Celsius. The effective power lag models heater warm-up/cool-down, not a full
two-body thermal energy model. Its stored state has units W; do not interpret
it as stored heat in joules.

The HysteresisRelay sees error, not temperature. For the normal band:

```
on  when error >= 1 K  (T <= 19 degC)
off when error <= -1 K (T >= 21 degC)
hold previous on/off state inside the band
```

The initial relay flag is false, but T(0)=18 triggers on in the first sample.
Threshold equality switches. The narrow-band diagram uses +/-0.1 K, so it
switches at 19.9 and 20.1 degC. The relay has direct feedthrough; the two
EulerIntegrator states provide causal memory. This is sampled switching, with
no hidden solver locating events between samples.

## What to observe

`temperature_scope`: temperature, setpoint and ambient, all degC.
`heater_scope`: delivered heater power in W. `relay_scope`: commanded on/off
(numeric 0/1) and error in K. Separate scopes keep W, degC and relay values
from sharing one vertical scale.

Expect initial warming followed by repeated limit cycles. Temperature continues
to rise after switch-off while heater power decays, and continues to fall just
after switch-on while the heater warms. Thus temperature crosses beyond the
relay thresholds: hysteresis width is not the full temperature oscillation
range. The colder ambient changes heating demand and the cycle's timing.
At 20 degC, steady heater demand is 500 W before the disturbance and 1000 W
after it; both are below the 2000 W maximum.

## Suggested experiments

1. Compare normal and narrow hysteresis. Record cycle period, temperature
   range and switching count after startup. A narrower band does not eliminate
   overshoot caused by heater lag.
2. Reduce heater lag from 10 to 2 s: change heater_rate weights from
   `[200,-0.1]` to `[1000,-0.5]`. Reduce dt as needed and compare overshoot.
   Increasing lag makes the distinction between command and delivered heat
   more visible.
3. Change relay lower/upper thresholds together, preserving lower < upper.
   Remember that they apply to error: reversing their signs changes the
   controller direction. A heater must turn on when cold.
4. Change ambient or heater power and calculate whether the setpoint is
   reachable. If maximum heater power cannot balance G*(Tset-Ta), continuous
   on operation is expected rather than a limit cycle.
5. Change initial temperature to 20 degC and compare initial_on true/false.
   Inside the band the initial flag matters; outside it thresholds override it.

## Validation, refinement and timing

From the repository root:

```bash
cmake -S . -B build
cmake --build build --target s7tests_control_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_control_examples_smoke$' --output-on-failure
```

The test runs both saved graphs at 10 and 20 Hz for 1200 s. Independent scalar
recurrences check temperature (1e-10), heater power (1e-8), relay state,
threshold direction/equality and ambient event timing at every sample.
Initial-state publication follows the causal contract: state at runtime step
k is at physical time `(k-1)*dt`; the observation timestamp is `k*dt`.
Both derivatives use the same published P and T, not the freshly updated P.

Before the first switch-off the relay stays on, providing an independent
closed-form startup reference:

```
P(t) = 2000*(1-exp(-t/10))
T(t) = 35 + (-17-20/9)*exp(-t/100) + (20/9)*exp(-t/10)
```

Temperature startup error must stay below 0.02 K and approximately halve
(ratio 1.8–2.2) when dt halves. Tests require repeated cycles, both overshoot
and undershoot beyond the thresholds by at least 0.02 K, bounded temperature
17–23 degC after the ambient step, and period change below 1 s between rates.
Cycle periods are measured between switch-off events during 200–590 s.
The analytical check qualifies startup; it does not establish exact continuous
switch times or long-term trajectory agreement for a discontinuous relay.

For a Studio refinement run at **20 Hz**, also double ambient start_sample
from 6000 to 12000, keeping the disturbance at 600 s. Set integrator dt to
0.05 s and source rate to 20 Hz. Sample-count event parameters do not rescale
automatically with Run Configuration. Threshold timing is quantized to dt;
use a smaller dt when changing the thermal time constants.

Regenerate using `python3 tools/generate_control_examples.py` from the root.
