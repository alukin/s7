# Bundled examples

Choose a model below with **File → Example Diagrams…**, or open a saved
`.s7diagram` with **File → Open Diagram…**. Set Run
Configuration as described in its article, and double-click the named observer
after starting the run. The Linux package stores diagrams and their notes in
`usr/share/s7/examples`, relative to its installation directory. All articles
listed here are embedded in Help and remain available offline.

The original S7 design was inspired by William McC. Siebert's
[*Circuits, Signals, and Systems*](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/),
MIT Press. Planned book adaptations will cite the edition, chapter, page and
example/problem identifier in their own notes. Existing diagrams are not
attributed to particular book examples without checking the source text.

Start with the **linear response laboratory** for reference-qualified examples.
Other families demonstrate techniques and have the limits below. Changing a
parameter, rate or duration moves outside a qualification unless its article
explicitly covers that change. There is no global numerical solver: dynamics
are explicit integrator and function/state networks.

## Starter and viewer demonstrations

- `demo1.s7diagram`: an introductory diagram for learning the editor and local
  run workflow, available through **File → Example Diagrams…**. The embedded
  startup demonstration remains the fallback when no recent diagram opens.
- `2int_osc.s7diagram`: a two-integrator oscillator; inspect the Scope and
  observe forward Euler's outward amplitude drift.
- [Gauss–Legendre oscillator](../examples/2int_osc_gauss.s7diagram): the same
  connected ideal-integrator equations with the production coupled
  fourth-order method. Its two temporary stages are not diagram states.
- `2int_osc_live.s7diagram`: the bounded live oscilloscope prototype; change
  acquisition length and inspect exact samples. See [prototype usage](LIVE_OSCILLOSCOPE_PROTOTYPE.md).
- [2D phase portrait](../examples/2int_osc_phase_portrait.md): the same
  oscillator with X/Y state observation. Run 10 seconds at 1, 10 or 100 kHz;
  compare the almost circular orbit with its time trace.
- [3D helix](../examples/3int_helix_phase_trajectory.md): add a steadily rising
  third state. Run 10 seconds at 1 or 10 kHz, then use XY/XZ/YZ and 3D views.
- `double_pendulum_small_angle.s7diagram` and `double_pendulum_full.s7diagram`:
  top-level coupled/fully nonlinear pendulum demonstrations. Use the documented
  family below for bounded reference comparisons; these standalone demos are
  experimental.

## Reference and integrator laboratories

- [Linear response](../examples/linear/README.md): ten diagrams comparing Euler
  and Euler–Cromer blocks for RC charge/discharge, RLC step/resonance and a
  mass–spring–damper release. Default parameters are qualified at 1 and 2 kHz;
  run 10 seconds, or 30 seconds for resonance. Watch settling, overshoot and
  timestep refinement against analytical solutions.
- [Nonlinear single pendulum](../examples/pendulum/README.md): three methods
  for a 1.5-radian initial displacement. Run 100 seconds at 1 or 10 kHz and
  compare angle, angular velocity, phase and energy. Symplectic and Trapezoid
  pass stated budgets; Euler is an intentional accuracy-failure baseline.
- [Small-angle double pendulum](../examples/double_pendulum/README.md): nine
  diagrams combine slow/fast/mixed modes with three methods. Run 20 seconds
  and compare the four state channels. Selected reference budgets are covered;
  other settings remain experimental.

## Control and switched systems — experimental

- [PI/PID control](../examples/control/README.md): ten diagrams compare open
  loop, P, PI, filtered PID and PI without anti-windup on a first-order plant
  and DC motor. Run 12 seconds at 1 kHz. Compare reference tracking, saturation
  recovery and load rejection in response and actuator scopes.
- [Buck converter](../examples/buck/README.md): compare fixed duty and PID
  regulation of an ideal 24 V synchronous converter. Run 1 second at 100 kHz;
  zoom near the 0.5-second load step to inspect voltage recovery and ripple.
- [PWM/PID/RLC](../examples/pwm_pid_rlc.md): a related converter with a smoothly
  varying 4–12 ohm load. Run 1 second at 100 kHz and compare switching,
  current, output voltage and load resistance.
- [Relay thermostat](../examples/relay/README.md): heater inertia, hysteresis
  and a cooler-ambient disturbance. Run 1,200 model seconds at 10 Hz; compare
  the normal and narrow hysteresis bands in temperature/heater/relay scopes.

## Nonlinear dynamics — experimental

- [Van der Pol oscillator](../examples/van_der_pol/README.md): nonlinear
  damping creates a limit cycle. Compare mu=1 Euler/Symplectic diagrams and
  the stronger mu=3 Symplectic case; inspect transient and settled oscillation.
- [Lorenz system](../examples/lorenz/README.md): compare a stable parameter
  set with an attractor. Observe three coupled states; long chaotic trajectory
  agreement is not a numerical accuracy claim.
- [Lotka–Volterra](../examples/lotka_volterra/README.md): predator/prey cycles,
  equilibrium, a nearby initial state and faster prey growth. Watch positive
  scaled populations; the faster-growth case uses a smaller timestep. These
  models are illustrative and are not fitted to a real ecosystem.

## Signal processing and measurement — experimental

- [Phase-locked loop](../examples/pll/README.md): ideal phase-domain PLL
  acquisition, a 20→22 Hz frequency step, a phase step, and an out-of-range
  35 Hz reference. Run 6 seconds at 1 kHz; compare phase error, tuning control
  and frequency estimates. The detector receives phase directly.
- [Live AM/FM oscilloscope examples](../examples/signal_measurement/README.md):
  bounded continuous inspection, duration changes, freeze and raw hover values.
  Choose the `_live.s7diagram` variants for zero full-run recording channels.
- **Live FFT demonstration:** open
  `signal_measurement/am_coherent_detection_live.s7diagram`, run **2 seconds at
  12800 Hz**, and open `carrier_scope`. In Spectrum, select `carrier_scope/carrier [1]`;
  N=4096 and the default Hann window give a **100 Hz**, amplitude **1** carrier.
  See [FFT controls and interpretation](STUDIO_SPECTRUM.md).
- **Live XY and delay demonstration:** open
  `signal_measurement/iq_correlation_live.s7diagram`, run **8 seconds at 1024 Hz**,
  and open `iq_scope`. I/Q gives a unit circle in Constellation / XY. In Correlation,
  select A=I, B=I_delayed_6, N=4096 and maximum |lag|=16: the peak is **+6 samples**
  (**0.005859375 s**). All three channels use bounded live acquisition with no
  full-run recording. See [XY/correlation controls](STUDIO_PAIR_ANALYSIS.md).
  For [cursors and export](STUDIO_WINDOW_INSPECTION.md), pin Time, select indices
  0 and 4095, then measure I/Q RMS near `1/sqrt(2)` V. Export the frozen JSON/CSV;
  display gain/offset and later retention changes do not alter its raw values.
- [AM/FM and RMS](../examples/signal_measurement/README.md): coherent AM
  detection, FM frequency estimation, a sine-amplitude RMS step and a signed
  DC RMS step. Run 2 seconds at 10 kHz. Inspect startup transients, detector
  delay and measurement windows. AM uses a shared coherent carrier.

The [numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md) is the
authoritative classification. The family articles provide filenames,
parameters, initial conditions, observer names and interpretation details.
