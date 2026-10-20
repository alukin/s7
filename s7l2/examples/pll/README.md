# Phase-locked loop: acquisition, tracking and measurement

Run each diagram for **6 seconds at 1 kHz**, dt = 0.001 s:

| Diagram | Disturbance at t = 2 s |
|---|---|
| [Frequency-step tracking](pll_frequency_step.s7diagram) | Reference 20 -> 22 Hz |
| [Phase-step recovery](pll_phase_step.s7diagram) | Reference phase jumps +1 rad; frequency stays 20 Hz |
| [Out-of-range reference](pll_out_of_range.s7diagram) | Reference 20 -> 35 Hz |

**Experimental teaching models.** Headless checks cover the envelope below.
Manual Studio open → run → observe → nested edit → rerun remains unqualified.

## Explicit causal loop and units

This is an **ideal phase-domain PLL**: the detector has access to both phases.
It is not a waveform-only receiver that extracts unknown phase from noisy input.
The two waveform signals are generated from those phases for observation and
frequency measurement; they do not secretly provide an additional phase estimator.

```
reference_phase' = 2*pi*f_ref
reference_angle = reference_phase + phase_step
error = wrap(reference_angle - vco_phase) in [-pi,pi)
raw = 0.1*error + integral
u = clamp(raw,-1,1)
integral' = error + 10*(u-raw)
f_vco = 18 + 10*u                  Hz
vco_phase' = 2*pi*f_vco            rad/s
```

Reference phase starts at 0.8 rad, VCO phase and PI integral at zero. Reference
frequency starts at 20 Hz. The free-running VCO frequency is 18 Hz; its tuning
range is 8–28 Hz. PI kp = 0.1/rad, ki = 1/(rad s), and anti-windup tracking
gain = 10/s. The control is dimensionless.

Reference and VCO phases are explicit EulerIntegrator states. `PhaseDifference`
and `PIController` are embedded editable copies of the bundled schema elements,
not new C++ controllers. The outer feedback cycle closes through `vco_phase`;
the controller's internal memory is not promoted into an outer causal boundary.
`PhaseWrap` maps each waveform phase into [-pi,pi), then Trig computes its sine.
Phase integrators themselves retain unwrapped radians over this finite run.

## Views and expected behavior

- `frequency_scope`: commanded reference frequency, tuned VCO frequency, and
  waveform-measured VCO frequency, in Hz.
- `phase_scope`: wrapped phase error (rad), limited and unlimited PI outputs.
- `waveform_scope`: reference and VCO sines, dimensionless.
- `measurement_scope`: measured reference frequency and both meter-valid flags.

The loop first acquires the 20 Hz reference from its frequency/phase offset.
For the 22 Hz step, expect a transient phase error and recovery toward zero,
with steady control near 0.4. The +1 rad phase-step case also reacquires lock;
its reference crossing interval is disturbed even though the commanded carrier
frequency remains 20 Hz. A phase jump is not a sustained frequency step.

The 35 Hz reference is outside the tuning range, so no fixed phase lock is
possible. Expect repeated phase wraps/cycle slips and persistent frequency
error. Loss of lock can occur before an ideal tuning-range boundary in other
configurations; the control need not remain pinned at its limit while slipping.
These three cases do not establish a general capture/hold-in range.

FrequencyMeter estimates frequency from consecutive interpolated rising-zero
crossings of the sine. It stays zero/invalid until two crossings, then holds its
latest estimate between crossings. **Valid means an estimate has been obtained**,
not that the loop is locked or the signal is still present. There is no timeout.
Do not interpret the startup zero as a measured stopped oscillator.

## Experiments and numerical limits

Change kp or ki, compare acquisition time and overshoot, and watch for slips.
Double-click `controller` to edit saturation or tracking gain in its nested graph,
then save/rerun the root document. Change the reference initial phase or frequency
step, keeping the desired frequency inside 8–28 Hz when studying successful lock.
Compare waveform crossing estimates with commanded frequency after transients.

Repeat at **2 kHz**, dt = 0.0005 s, keeping duration and physical event time:
double both StepGenerator `start_sample` values from 2000 to 4000. Update all
integrator dt, sample-rate and embedded-schema rate values consistently. Studio
Run Configuration updates rates recursively but does not rescale sample counts.
Forward Euler is first order; larger gains or dt require fresh numerical checks.
No noise, waveform phase extraction, propagation delay or clock jitter is modeled.

At runtime step k phase/integral states represent `(k-1)*dt`, while observations
are labeled `k*dt`. Direct detector/control values act on those published states.
The initial states are published at step 1; source steps apply at zero-based
sample 2000. A six-second run ends with state time `6-dt`.

## Headless checks

From the repository root:

```bash
python3 tools/generate_measurement_examples.py
cmake -S . -B build
cmake --build build --target s7tests_measurement_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_measurement_examples_smoke$' --output-on-failure
```

All three saved graphs run at 1/2 kHz, with unchanged schemas at native rate.
Independent scalar recurrences check phase states, PI outputs, source steps,
wrapped waveforms and timestamps; mismatch must stay below 1e-8. An independent
absolute-crossing calculation checks both meters within 1e-7 Hz and checks validity.

Before the event, an independent RK4 reference integrates the unsaturated error
and integral equations. One full step versus two half-steps must agree within
1e-7; maximum two-state error must stay below 0.04 and reduce by a factor of
1.7–2.3 when dt halves. This reference checks initial acquisition, not a global
solver inside the runtime.

All cases must acquire phase error below 0.02 rad over t = 1.5–1.9 s. In the
two successful tracking cases, phase error stays below 0.02 rad and measured
frequency error below 0.1 Hz over t >= 4.5 s. The out-of-range case instead
must show repeated slips, phase error above 1 rad and frequency error above
5 Hz; this expected failure to lock is a passing demonstration, not a successful PLL.
