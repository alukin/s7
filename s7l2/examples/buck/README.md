# PWM synchronous buck converter

Open [PID load-step regulation](buck_pid_load_step.s7diagram) or
[fixed-duty comparison](buck_open_loop_load_step.s7diagram). Run **1 second at
100 kHz**, dt = 10 microseconds. Open `voltage_scope`, `current_scope`,
`switching_scope` and `load_scope`. Zoom to a few milliseconds near 0.4 s
and 0.9 s to see ripple, and around 0.5 s for load-step recovery.

**Experimental ideal converter model.** Headless numerical checks cover the
default parameters at 100/200 kHz. Manual Studio workflow remains unqualified.
This extends the existing [PWM/PID/RLC example](../pwm_pid_rlc.md): the power
stage is reused, the varying load is replaced by a timed resistor step, and
the PID is an editable bundled schema instance. The original example is kept
as the sinusoidal-load exercise.

## Topology and equations

An ideal complementary switch pair connects the switch node to either the
24 V source (gate = 1) or return (gate = 0). An inductor L = 10 mH with
winding resistance rw = 0.2 ohm connects that node to the output. A capacitor
C = 1000 microfarads and load resistor R are in parallel from output to return.
There is no diode-only current path; **negative inductor current is allowed**.

```
di/dt = (24*gate - v - rw*i)/L
dv/dt = (i - v/R)/C
R = 8 ohm before 0.5 s, 4 ohm from 0.5 s onward
i(0) = 0 A; v(0) = 0 V
```

Separate Euler integrators provide the electrical causal memory. All right-hand
sides use the currently published i and v. The parallel resistor is a load,
not a series RLC resistor. The switch voltage enters only the inductor branch.
No clipping at zero current or implicit extra delay is inserted.

Ideal circuit power balance:

```
E = L*i²/2 + C*v²/2
dE/dt = 24*gate*i - rw*i² - v²/R
```

The switch can return energy to the ideal source during reverse current.
Changing R changes dissipation; it does not reset capacitor voltage or inductor
current. Forward Euler has a positive discrete energy defect of order dt
integrated over a fixed duration; checking its reduction distinguishes
discretization error from incorrect topology/signs.

This model omits switch/diode drops, dead time, switching loss, inductor
saturation, capacitor ESR, current limiting and discontinuous-conduction diode
behavior. Do not present it as an asynchronous diode buck or as a hardware design.

## PWM and controller

The PWM carrier is 1 kHz: 100 samples per period at 100 kHz. Duty is latched
at the beginning of each carrier period; the trailing-edge gate is high when
`sample_in_period/period_samples < latched_duty`. Intermediate control changes
take effect at the next carrier boundary. Edges are quantized to model samples.
The fixed-duty example uses d = 0.5. Its steady mean output approximately follows
`v = 24*d*R/(R+rw)`: about 11.707 V before the load step and 11.429 V after it.
It has no soft start: expect startup ringing above the final mean voltage.

The PID uses the bundled `PIDController` graph, saved as an embedded editable
instance. Its saturation is customized to [0,1] and tracking gain to 100/s:

```
error = 12 - v
filter_rate = (v-filter_state)/0.002
raw_duty = 0.04*error + integral - 0.0004*filter_rate
duty = clamp(raw_duty,0,1)
integral' = 8*error + 100*(duty-raw_duty)
filter_state' = filter_rate
```

Integral and filter states start at zero and use forward Euler. Unlike the
older example's IIR derivative realization, this is the bundled controller's
explicit measurement-filter graph; no fixed-rate transfer coefficients are
hidden in the example. The reference is 12 V from startup.

Probes: output/reference in V, inductor/load currents in A, load resistance in
ohm, duty/unlimited duty dimensionless, gate numeric 0/1. Expect startup,
switching ripple, a voltage dip after the heavier load is applied, and recovery
of the regulated mean toward 12 V. Compare the fixed-duty droop. Inspect raw
duty during startup to see saturation and anti-windup.

## Experiments and numerical checks

Change the final load to 6 or 3 ohm; measure voltage dip, recovery time, current
increase and post-recovery ripple. Change C or L and update inverse_C/inverse_L
consistently. Double-click the PID to change tracking gain or the output limits;
compare startup with tracking disabled. The controller's outer feedback cycle
closes through the explicit power-stage integrators.

For timestep refinement, keep 1 kHz switching and the load step at 0.5 s:
at **200 kHz**, set `period_samples=200`, load `start_sample=100000`, all
sample_rate_hz fields to 200000, and all integrator/embedded-schema dt fields
to 0.000005. Run Configuration updates rates recursively but does not rescale
carrier/event sample counts. Changing only the rate changes the experiment.
PWM quantization also changes with rate; pointwise closed-loop switching
trajectories are not expected to match exactly across rates.

From the repository root, run the shared test:

```bash
cmake -S . -B build
cmake --build build --target s7tests_control_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_control_examples_smoke$' --output-on-failure
```

The test loads both saved diagrams at 100 and 200 kHz. It verifies load timing,
zero startup, finite values, duty bounds and exact carrier latching. Independent
RK4 references replay the actual sampled gate and resistance, held over each
step; this tests the switched plant without claiming the same discontinuous
closed-loop gate sequence for a continuous controller. One RK4 step versus two
half-steps must agree below 1e-8; Euler state error must be below 0.25 (V/A)
and decrease by at least 30% on refinement.

Acceptance also requires regulated peak voltage below 16 V (fixed-duty startup:
below 24 V), final 0.1 s ripple below
1.2 V peak-to-peak, regulated means within 0.2 V of 12 V over 0.3–0.45 s and
0.8–1 s, and fixed-duty means within 0.15 V of the DC estimates above.
The integrated energy defect must be positive, below 1% of input energy, and
decrease by a factor of 1.7–2.3 when dt is halved. These bounds describe this
configuration, not universal converter accuracy.

Regenerate using `python3 tools/generate_control_examples.py` from the root.
