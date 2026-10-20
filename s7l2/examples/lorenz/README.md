# Lorenz system: chaos and nearby initial conditions

Open [Lorenz attractor](lorenz_attractor.s7diagram) and run **40 seconds at
4 kHz**, dt = 0.00025. For a parameter contrast, open
[stable case](lorenz_stable.s7diagram), **10 seconds at 4 kHz**.

Each contains two independent networks: `base` starts at (1,1,1), `nearby`
starts at (1.000001,1,1). Open `base_trajectory` and `nearby_trajectory` for
3D phase trajectories, with inputs x/y/z in that order. Rotate the view and use
XY/XZ/YZ projections. `base_scope` and `nearby_scope` show time traces;
`difference_scope` shows each nearby-minus-base coordinate.

**Experimental teaching models.** Automated checks cover the numerical envelope
below. Manual Studio workflow remains unqualified. A plausible butterfly alone
does not validate a chaotic computation.

## Equations, parameters and graph

```
x' = sigma*(y-x)
y' = rho*x - y - x*z
z' = x*y - beta*z
sigma = 10; beta = 8/3
rho = 28 for attractor, 10 for stable comparison
```

The states and time are dimensionless; one normalized time unit is represented
by one model second. They are not positions measured in metres. Each copy uses
three EulerIntegrator states, two Multiply blocks (xz and xy), and weighted
Adders for the derivatives. All derivatives use the currently published states.
Feedback closes through explicit causal-memory integrators. There is no new
Lorenz C++ element or graph-wide numerical solver.

The equilibria are the origin and, for rho > 1,
`(+/-sqrt(beta*(rho-1)), +/-sqrt(beta*(rho-1)), rho-1)` with matching signs.
For the supplied rho = 10 initial conditions both trajectories approach the
positive equilibrium `(sqrt(24),sqrt(24),9)`. For rho = 28, expect repeated
visits to both lobes and separation of initially nearby states.

## Sensitivity experiments

1. Inspect the early coordinate differences, then the later differences. The
   two trajectories initially stay close and later separate substantially.
   Their divergence demonstrates sensitivity for this numerical experiment;
   it is not a measured Lyapunov exponent.
2. Change `nearby_x` initial_value to 1.00001 or 1.0000001. Keep all other
   settings fixed and compare how long the traces remain visibly close.
3. Compare rho = 10 and rho = 28. To change rho manually, change the first
   weight in both `*_dy` Adders. Change sigma in both weights of `*_dx`, and
   beta in the second weight of both `*_dz` Adders. The two copies should
   differ only in initial conditions for a sensitivity comparison.
4. Repeat at 8 kHz, dt = 0.000125, with the same 40-unit duration. Run
   Configuration updates integrator dt. There are no timed source events to
   rescale. Expect short-horizon improvement but a different late trajectory:
   numerical truncation also perturbs the orbit.

Forward Euler is first order, can distort geometry and can become unstable
at larger timesteps or changed parameters. Smaller dt is not proof of accuracy.
Do not use long-term pointwise trajectory agreement as the acceptance criterion
for chaos. Lobe shape, boundedness and broad statistics are more useful later,
but finite-run agreement of those quantities is not proof of invariant-measure
accuracy either. No exact lobe-switch times or long-run forecast are promised.

At runtime step k the published states represent `(k-1)*dt`; observation time
is `k*dt`. Initial states appear at step 1. Align numerical references with
state time rather than shifting them by one sample. Probes are `base_x/y/z`,
`nearby_x/y/z`, and `delta_x/y/z`, all dimensionless.

## Headless checks

From the repository root:

```bash
python3 tools/generate_nonlinear_examples.py
cmake -S . -B build
cmake --build build --target s7tests_nonlinear_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_nonlinear_examples_smoke$' --output-on-failure
```

Both saved graphs run at 4 and 8 kHz with their full documented durations.
Independent long-double RK4 references cover the **first time unit only**;
four versus eight reference substeps must agree within 1e-8. Maximum Euclidean
state error must be below 0.25 and decrease by a factor of 1.7–2.3 when dt halves.
Each nearby-minus-base probe and every published timestamp are checked too.

Full-run checks require finite states, |x| < 30, |y| < 40, and 0 < z < 60 in
both copies. For rho = 28, the base trajectory must visit x > 2 and x < -2
repeatedly after t = 10; mean z must be 15–35 and RMS x 4–14 during t >= 10.
Nearby-state distance must remain below 0.01 during the first time unit and
exceed 0.1 sometime after t = 20. These loose geometry/sensitivity gates do not
require equal long trajectories across timesteps. For rho = 10 the final base
state must be within Euclidean distance 0.05 of the positive equilibrium.
