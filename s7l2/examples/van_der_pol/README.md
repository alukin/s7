# Van der Pol oscillator: convergence to a limit cycle

| Diagram | Run Configuration |
|---|---|
| [mu = 1, forward Euler](van_der_pol_mu1_euler.s7diagram) | 40 s at 1 kHz |
| [mu = 1, Euler–Cromer](van_der_pol_mu1_symplectic.s7diagram) | 40 s at 1 kHz |
| [mu = 3, Euler–Cromer](van_der_pol_mu3_symplectic.s7diagram) | 60 s at 1 kHz |

Each diagram contains three independent explicit networks, starting from
`(x,v) = (0.1,0), (2,0), (4,0)`. Open `small_scope`, `cycle_scope` and
`large_scope` for time traces, and the corresponding `*_phase` viewers for
phase portraits: horizontal x, vertical v. All variables and time are normalized;
one time unit is represented by one model second. These are not a calibrated
electrical or mechanical device.

**Experimental teaching diagrams.** Headless checks cover the envelope below.
Manual Studio open → run → observe → edit → rerun remains unqualified.

## Equations and explicit graph

```
x' = v
v' = mu*(1-x*x)*v - x
E = (x*x + v*v)/2
E' = mu*(1-x*x)*v*v
```

Multiply forms x²; an Adder forms `mu*(1-x²)`; another Multiply applies v;
an Adder subtracts x. The result feeds the velocity integrator. For positive
mu, the nonlinear term adds energy when |x| < 1 and removes it when |x| > 1.
Small initial oscillations grow and large ones settle toward the same cycle
geometry. Their phases need not agree at equal times.

Euler uses two causal-memory EulerIntegrator blocks with
`v[n+1]=v[n]+dt*a[n]`, `x[n+1]=x[n]+dt*v[n]`.
The SymplecticIntegrator block instead uses
`x[n+1]=x[n]+dt*v[n+1]` after updating v. This is Euler–Cromer ordering;
because acceleration depends on velocity, **the damped nonlinear update is not
mathematically symplectic**. Both methods are first order.

## Expected behavior and experiments

At mu = 1, expect a smooth noncircular cycle, x extrema close to +/-2 and
period about 6.66 time units. At mu = 3 the waveform has slower portions and
faster transitions, with period about 8.86. The phase portraits show convergence
from the interior and exterior; an apparent phase difference is not failure
to approach the same cycle.

1. Compare the three initial conditions using late-run extrema and crossing
   periods. Compare cycle geometry, not instantaneous distance between states.
2. Compare Euler and Euler–Cromer at mu = 1 using the same rate and duration.
   Neither is an exact continuous-time trajectory.
3. Change each network's `*_damping` weights from `[mu,-mu]` to `[0.5,-0.5]`
   or `[2,-2]`. Increase duration if startup has not settled. Larger mu creates
   faster transitions that require smaller dt; the supplied mu = 3 case does
   not qualify arbitrarily large mu.
4. Set x(0)=v(0)=0: this is an exact equilibrium, so an exactly initialized
   model stays there despite its instability for mu > 0. Use a nonzero initial
   condition to observe growth.
5. Set mu = 0 to remove the nonlinear drive and compare the harmonic-oscillator
   energy behavior with the [linear laboratory](../linear/README.md).

Probes `small_x/small_v`, `cycle_x/cycle_v`, and `large_x/large_v` expose the
three state pairs. For SymplecticIntegrator, output 0 is x and output 1 is v.
At runtime step k the states represent `(k-1)*dt`; the observation label is
`k*dt`. First publication is the initial state. Separate Euler integrators
read the same old states, rather than a newly integrated velocity.

## Reference checks and refinement

From the repository root:

```bash
python3 tools/generate_nonlinear_examples.py
cmake -S . -B build
cmake --build build --target s7tests_nonlinear_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_nonlinear_examples_smoke$' --output-on-failure
```

The shared test loads the saved diagrams without altering native-rate schemas,
then repeats at 2 kHz. Long-double RK4 runs independently in the test only;
one step versus two half-steps must agree within 1e-7 over the full duration.
Maximum Euclidean (x,v) error over the first five time units must be below 0.1
and decrease by a factor of 1.7–2.3 when dt halves.

Cycle comparisons use time >= 20 for mu = 1 and >= 30 for mu = 3. Every
initial condition must produce repeated cycles, x extrema in (-2.2,-1.9) and
(1.9,2.2), extrema within 0.03 of RK4, and mean positive-going x=0 crossing
period within 0.5% of RK4. Across initial conditions, period spread must be
below 0.05 and positive-peak spread below 0.03. These are finite-run geometry
checks, not a proof of global convergence or a universal stability guarantee.

For a Studio refinement run at 2 kHz, use dt = 0.0005 consistently for every
integrator; Run Configuration updates those parameters. Keep duration and
initial conditions fixed. There are no sample-count source events to rescale.
