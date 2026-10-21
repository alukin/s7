# Bundled numerical validation matrix

This matrix defines a bounded numerical claim. Automated Studio coverage uses
its production document parser, run controller, worker and authoritative
recording source; it does not establish manual GUI usability or platform support.

## Linear response envelope

All ten `examples/linear/*.s7diagram` files are checked with their saved
parameters, initial conditions and connections. Each runs at exactly 1 kHz and
2 kHz (`dt=0.001` and `0.0005 s`); no interpolation to other rates is claimed.
Methods are forward Euler and the existing SymplecticIntegrator's Euler–Cromer
update. RC uses the latter's velocity output as a first-order state; this does
not confer a symplectic advantage. Damped Euler–Cromer is not symplectic.

| Model (both methods) | Parameters and initial state | Duration | Independent reference |
|---|---|---|---|
| RC charge | R=C=1, source=1, V(0)=0 | 10 s | 1-exp(-t) |
| RC discharge | R=C=1, source=0, V(0)=1 | 10 s | exp(-t) |
| RLC step | R=L=1, C=0.25, source=1, q(0)=i(0)=0 | 10 s | q=(1-h)/4, i=exp(-t/2)sin(wt)/w |
| RLC resonant drive | Same circuit, source=sin(2t), zero state | 30 s | q=-cos(2t)/2+h/2, i=sin(2t)-2exp(-t/2)sin(wt)/w |
| Mass–spring–damper | m=b=1, k=4, F=0, x(0)=1, v(0)=0 | 10 s | x=h, v=-4exp(-t/2)sin(wt)/w |

Here `w=sqrt(3.75)` and
`h=exp(-t/2)[cos(wt)+sin(wt)/(2w)]`. RLC capacitor voltage is `4q`.
These closed forms solve the continuous equations documented in the
[linear laboratory](../examples/linear/README.md); they do not call an S7
integrator or derive reference values from S7 samples.

Acceptance for every configuration:

- Every state sample is finite and maximum absolute error is below 0.05 in
  its channel's stated units. Halving dt reduces maximum error by a factor
  strictly between 1.8 and 2.2. Initial-state error is below 1e-12.
- Each saved, configured file is loaded independently by the headless harness
  and Studio document parser. Every recorded channel, value, step and timestamp
  agrees exactly between the two execution paths. Source signals are also
  checked against their prescribed continuous values.
- There are exactly duration*rate steps, no missing or extra samples and no
  incomplete recording. Step 1 publishes the initial state with timestamp dt;
  step N publishes state at physical time (N-1)*dt with timestamp N*dt.
  Both first and final publication are checked. Nanoseconds agree with rounded
  absolute model time; seconds are derived from rational rate.

Tests: `s7tests_linear_examples_smoke` and `s7studio_model_time` (ten
`linearMatrix` rows, each checking both rates). Their output records coarse/fine
maximum errors and refinement factors. Run:

```bash
cmake --build build --target s7tests_linear_examples_smoke s7studio_model_time_test --parallel 4
ctest --test-dir build -R '^(s7tests_linear_examples_smoke|s7studio_model_time)$' -V
```

## Classification of bundled diagrams

| Files / family | Numerical classification and limit |
|---|---|
| `linear/*.s7diagram` | Validated within the matrix above, subject to recorded passing evidence below |
| `pendulum/nonlinear_pendulum_{symplectic,trapezoid}.s7diagram` | Previously validated single-pendulum baseline at 1/10 kHz for 100 s; see family README and N1 evidence in DEVELOPMENT_PLAN |
| `pendulum/nonlinear_pendulum_euler.s7diagram` | Experimental for that accuracy envelope: known phase/energy drift fails the baseline |
| `double_pendulum/*.s7diagram` | Existing coupled-pendulum reference checks cover selected rates; experimental outside their documented budgets; this increment adds no qualification |
| `control/`, `buck/`, `relay/`, `van_der_pol/`, `lorenz/`, `lotka_volterra/`, `pll/`, `signal_measurement/` | Experimental teaching models; existing smoke checks are not full Studio/reference qualification |
| All top-level `examples/*.s7diagram` | Experimental demonstrations; selected oscillator regressions do not qualify every saved diagram/configuration |

Arbitrary parameter edits, other dt/duration combinations, undamped long runs,
chaotic trajectories, external input histories and cross-platform deterministic
reproduction remain outside this increment. No new runtime solver is introduced.

## Measured evidence — 2026-10-21

Linux, GCC 16.2.1, Qt 6.11.2, existing Debug build. Both targeted CTest
entries passed (17.35 s total; 18 Qt test cases including lifecycle and existing
fractional-rate parity checks). No full-suite or native Windows run is claimed.
Maximum absolute errors across physical-state channels were:

| Model | Method | 1 kHz | 2 kHz | Coarse/fine |
|---|---|---:|---:|---:|
| RC charge | Euler | 0.0001840164 | 0.0000919890 | 2.000417 |
| RC charge | Euler–Cromer block | 0.0001840164 | 0.0000919890 | 2.000417 |
| RC discharge | Euler | 0.0001840164 | 0.0000919890 | 2.000417 |
| RC discharge | Euler–Cromer block | 0.0001840164 | 0.0000919890 | 2.000417 |
| RLC step | Euler | 0.0014691448 | 0.0007339413 | 2.001720 |
| RLC step | Euler–Cromer | 0.0009306359 | 0.0004652511 | 2.000287 |
| RLC resonance | Euler | 0.0082783437 | 0.0041311222 | 2.003897 |
| RLC resonance | Euler–Cromer | 0.0020013331 | 0.0010003336 | 2.000666 |
| Mass–spring–damper | Euler | 0.0030362201 | 0.0015164235 | 2.002224 |
| Mass–spring–damper | Euler–Cromer | 0.0009306359 | 0.0004652511 | 2.000287 |

Studio's prescribed-source checks did not increase these maxima. All channel
values, steps and times matched the headless traces exactly, including first
and final samples. Errors above combine differently dimensioned channels for a
compact envelope summary; the 0.05 threshold is applied in each channel's own
units, not interpreted as one physical quantity across channels.
