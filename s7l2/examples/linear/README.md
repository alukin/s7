# Linear response laboratory

Explicit analog-computer graphs for first- and second-order dynamics. Open a
diagram in Studio, select **1 kHz**, run **10 s** (resonance: **30 s**), and open
the `scope` observer. All integrators default to `dt = 0.001 s`.

| Exercise | Forward Euler | Symplectic block | Gauss–Legendre |
|---|---|---|---|
| RC charging | [diagram](rc_charge_euler.s7diagram) | [diagram](rc_charge_symplectic.s7diagram) | [diagram](rc_charge_gauss.s7diagram) |
| RC discharging | [diagram](rc_discharge_euler.s7diagram) | [diagram](rc_discharge_symplectic.s7diagram) | [diagram](rc_discharge_gauss.s7diagram) |
| Series RLC step | [diagram](rlc_step_euler.s7diagram) | [diagram](rlc_step_symplectic.s7diagram) | [diagram](rlc_step_gauss.s7diagram) |
| Series RLC resonant drive | [diagram](rlc_resonance_euler.s7diagram) | [diagram](rlc_resonance_symplectic.s7diagram) | — |
| Mass–spring–damper release | [diagram](mass_spring_damper_euler.s7diagram) | [diagram](mass_spring_damper_symplectic.s7diagram) | — |

To try coupled Euler substeps, edit `substeps` on each Euler integrator in
the relevant Euler diagram (for example, set both RLC integrators to 8),
and keep the Studio Run rate and each `dt` at the **outer** rate. Only the
internal state solve uses `dt/substeps`; ordinary Scope observations remain
one per outer step. The [two-integrator oscillator Gauss diagram](../2int_osc_gauss.s7diagram)
uses the same principle of coupled states without an inserted Delay. The
[production numerical contract](../../docs/COUPLED_INTEGRATION.md) gives
eligibility, held-input and solver limits. The resonant-drive example is
excluded from the new Gauss diagrams because its source varies in time and
the production policy holds each sampled source value across the outer step.

The original Euler and symplectic diagrams are **numerically validated within
the [stated matrix](../../docs/NUMERICAL_VALIDATION_MATRIX.md)**. The new
Gauss diagrams are loaded and checked against closed-form RC/RLC responses
by `s7tests_linear_examples_smoke`; the oscillator variant is checked there
against `cos(1)` after one simulated second.
Analytical state checks and exact headless/Studio worker-recording parity cover
the default parameters at 1 and 2 kHz. Manual Studio open → run → observe → edit →
rerun usability has not yet been qualified. There are no nested components in these
small graphs; edit the primitive parameters directly.

## RC: first-order response

An ideal voltage source drives R = 1 ohm in series with C = 1 farad:

```
dVc/dt = (Vs - Vc)/(R*C), tau = R*C = 1 s
charging:    Vs = 1 V, Vc(0) = 0 V; Vc(t) = 1 - exp(-t/tau)
discharging: Vs = 0 V, Vc(0) = 1 V; Vc(t) = exp(-t/tau)
```

The constant input represents the post-switch voltage at t = 0. Probe `voltage`
and `input`, both in volts. At one time constant charging reaches about
0.632 V and discharging falls to about 0.368 V. After five time constants the
remaining error is below 0.7%.

Experiments: change the derivative Adder weights from `[1,-1]` to
`[1/tau,-1/tau]` for tau = 0.5 or 2 s. Change the source voltage and predict
the new final voltage. The source current is `(Vs-Vc)/R`; this is not the
same as the capacitor voltage.

Euler uses one scalar integrator. The SymplecticIntegrator is a second-order
block: **output 1 (velocity) represents Vc**, its acceleration input represents
dVc/dt, and output 0 is an unused auxiliary integral of voltage in V s.
The voltage recurrence is identical to Euler, so these RC versions should
agree; this construction does not give a first-order dissipative circuit a
symplectic advantage. The auxiliary integral does not feed back into the model.

## RLC: step response and resonance

Series R = 1 ohm, L = 1 henry, C = 0.25 farad, initially uncharged and at rest:

```
q' = i
i' = (Vs - R*i - q/C)/L = Vs - i - 4*q
Vc = q/C = 4*q
q(0) = 0 C, i(0) = 0 A
omega0 = 1/sqrt(L*C) = 2 rad/s
zeta = R/2 * sqrt(C/L) = 0.25
```

Probe `charge` (C), `current` (A), `capacitor_voltage` (V) and `input` (V).
The step diagram applies 1 V from t = 0: expect overshoot and decaying ringing,
settling to q = 0.25 C, Vc = 1 V, i = 0 A.

The resonance diagram applies `Vs = sin(2*t)` V, frequency `1/pi` Hz.
After the startup transient, current amplitude approaches 1 A and capacitor
voltage amplitude approaches 2 V. This is series-current resonance; the peak
of capacitor voltage versus drive frequency is slightly below omega0 when
damping is present. Sweep the oscillator frequency through 0.2, `1/pi`, and
0.5 Hz, keeping duration and source amplitude fixed. Wait for startup to decay
before comparing amplitudes.

Experiments: change the derivative weights to `[1/L,-R/L,-1/(L*C)]`.
Change the capacitor-voltage Gain to `1/C` whenever C changes. For fixed L and
C, use R = 1, 4, 6 ohm to compare underdamped, critically damped and overdamped
step responses. Remove resistance and forcing, set initial charge to 0.25 C,
and compare energy `E = L*i^2/2 + q^2/(2*C)` between methods. Forward Euler
grows oscillator energy; Euler–Cromer has bounded oscillatory energy error
for the undamped linear oscillator when `dt*omega0 < 2`.

## Mass–spring–damper: mechanical analogy and impulse response

Mass m = 1 kg, damping b = 1 N s/m, spring k = 4 N/m:

```
x' = v
v' = (F - b*v - k*x)/m = F - v - 4*x
x(0) = 1 m, v(0) = 0 m/s, F = 0 N
omega0 = sqrt(k/m) = 2 rad/s, zeta = b/(2*sqrt(k*m)) = 0.25
```

Probe `position` (m), `velocity` (m/s), `input` (N). Expect decaying motion
around x = 0. Stored energy is `E = m*v^2/2 + k*x^2/2` and the continuous
balance is `dE/dt = F*v - b*v^2`.

Experiments: set derivative weights to `[1/m,-b/m,-k/m]`; compare b = 1, 4,
6 N s/m for the three damping regimes. For a **step response**, set initial
position to zero and source to 1 N: the final displacement is F/k = 0.25 m.
For a **unit impulse response**, leave F = 0, set x(0) = 0 and v(0) = 1 m/s.
This represents the state immediately after a 1 N s impulse (`delta v = J/m`),
without approximating a Dirac impulse by an unresolved source pulse.

The electrical/mechanical correspondence is q ↔ x, i ↔ v, Vs ↔ F,
L ↔ m, R ↔ b, 1/C ↔ k. Thus the RLC charge step response and the mechanical
displacement step response have the same numerical values for these parameters,
despite different units. Compare them after making the mechanical step edits.

## Numerical method, time and limits

Forward Euler uses separate causal-memory integrators:
`v[n+1]=v[n]+dt*a[n]`, `x[n+1]=x[n]+dt*v[n]`.
SymplecticIntegrator uses Euler–Cromer ordering:
`v[n+1]=v[n]+dt*a[n]`, `x[n+1]=x[n]+dt*v[n+1]`.
With velocity-dependent damping this update is no longer mathematically
symplectic. Both methods are first order. Neither guarantees accuracy or
stability for arbitrary parameter changes; resolve the fastest time constant
and oscillation, then check timestep refinement.

These graphs publish their initial state at runtime step 1. At step k the
state represents physical time `(k-1)*dt`, while the observation timestamp is
`k*dt`. The first sinusoidal input sample is zero; subsequent samples advance
by dt. Do not compare a state against an analytical reference shifted by one
sample. A 10 s run has its last published state at `10-dt` seconds.

Repeat at 2 kHz with the same duration. Studio Run Configuration supplies the
effective rate; when editing saved JSON directly, update top-level dt, every
integrator dt, and the oscillator sample_rate_hz together. At sufficiently
small dt, halving dt should approximately halve state error. Parameter edits,
undamped long runs, and extreme rates are outside the default checked envelope.

## Reproduce headless reference checks

From the repository root:

```bash
python3 examples/linear/generate_diagrams.py
cmake -S . -B build
cmake --build build --target s7tests_linear_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_linear_examples_smoke$' --output-on-failure
```

The test loads all ten saved diagrams, validates and compiles their actual
connections, and checks every published state at 1 and 2 kHz. Durations are
10 s, or 30 s for resonance. It checks initial states, timestamps, finite values,
and maximum absolute error below 0.05 in each state's stated units (including
capacitor voltage). The coarse/fine maximum-error ratio must be 1.8–2.2.
This is a teaching envelope, not a general integrator qualification.

References are closed-form solutions, independent of the S7 execution path.
With `w=sqrt(3.75)` and `h=exp(-t/2)*(cos(w*t)+sin(w*t)/(2*w))`, mechanical
release is `x=h`, RLC step is `q=(1-h)/4`, and resonant drive is
`q=-cos(2*t)/2+h/2`. Velocity/current references are their analytical
derivatives. RC references are the exponentials above.

## Predict → Construct → Simulate → Measure → Explain: discrete structure

**Predict.** A continuous pole `s` becomes `exp(s*dt)` under exact sampling.
Forward Euler instead uses `1+s*dt`; its RC pole becomes negative for
`dt>RC` and unstable for `dt>2RC`. For the LC oscillator, its complex poles
have magnitude greater than one for every positive `dt`, so energy grows.
Euler–Cromer keeps its undamped oscillator poles on the unit circle for
`0<dt*omega0<2`, but changes phase. These predictions are about the chosen
discrete transitions, not about drawing order in Studio.

**Construct.** Open the existing editable RC, RLC and mechanical diagrams
above. For RLC and the mechanical analogue, map `q↔x`, `i↔v`, `L↔m`,
`R↔b`, and `1/C↔stiffness`; map capacitor voltage `q/C` to spring force
`stiffness*x` if comparing output waveforms. The original Euler and symplectic
diagrams use production Euler and Euler–Cromer elements. The finite
feedback-correction methods in
[Phase 2 research](../../docs/NUMERICAL_FEEDBACK_RESEARCH_PHASE2.md) are
separate research-runner experiments, not editable S7 elements.
The coupled Euler substeps and Gauss–Legendre stages in the
[Phase 3 real-graph study](../../docs/NUMERICAL_INTEGRATION_PHASE3_REPORT.md)
use a headless research execution policy. The separate
[production integration PR](https://github.com/alukin/s7/pull/8) adds opt-in
runtime methods and editable diagrams; the Phase 3 runner remains a research
tool. That study uses the same RC and RLC equations, plus the two-integrator
oscillator, and compares its Euler substeps with ordinary finer-step S7
execution.

**Simulate.** Compare the supplied 1 and 2 kHz runs, then deliberately enlarge
`dt` in a copy of a diagram. Keep each element's `dt` consistent with the run
rate. For oscillators, observe many periods; a short trajectory can hide
amplitude growth or a small frequency error.

**Measure.** Record charge/discharge time constants, RLC overshoot and settling,
and oscillator phase and energy. A transfer-function zero describes how the
sampled input appears at a chosen output; it does not by itself add a state.
The research plots show that the same two-state RLC system has different
discrete zeros under different methods and output timing conventions.

**Explain.** Additional fixed-point corrections approach the backward-Euler
equation only when that solver converges. Solving it accurately can still
produce a damped oscillator where the continuous system conserves energy.
Equivalent continuous state coordinates give equivalent discrete poles for
matrix-function methods in exact arithmetic. Sequential block updates, extra
delays, mixed methods or distinct input holds can change the transition itself;
mere regrouping of pure arithmetic blocks does not.
