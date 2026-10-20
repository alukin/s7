# Lotka–Volterra predator–prey laboratory

Open any diagram and run **40 seconds at the rate in the table**. Open
`population_scope` for both time traces and `population_phase` for the phase
portrait: horizontal prey, vertical predator.

| Diagram | alpha | Initial prey, predator | Rate |
|---|---:|---|---|
| [Population cycles](predator_prey.s7diagram) | 1 | 4, 1 | 1 kHz |
| [Near equilibrium](predator_prey_near_equilibrium.s7diagram) | 1 | 2.2, 2 | 1 kHz |
| [Coexistence equilibrium](predator_prey_equilibrium.s7diagram) | 1 | 2, 2 | 1 kHz |
| [Faster prey growth](predator_prey_faster_growth.s7diagram) | 1.5 | 4, 1 | 4 kHz |

All cases use beta = delta = 0.5 and gamma = 1. Populations are positive,
dimensionless scaled abundances, not integer animal counts. Time is normalized;
one time unit is represented by one model second.
The faster-growth case uses dt = 0.00025 rather than 0.001 to keep its
40-unit state error within the same checked budget.

**Experimental teaching models.** Automated checks cover the finite envelope
below. Manual Studio open/run/observe/edit/rerun remains unqualified. This ideal
model is not fitted to a real ecosystem.

## Equations, equilibrium and conservation

```
prey' = alpha*prey - beta*prey*predator
predator' = delta*prey*predator - gamma*predator
```

One Multiply produces the interaction term. Weighted Adders form births minus
predation, and predator growth minus deaths. Two EulerIntegrator blocks hold
the states; both derivative calculations use the same old state pair. No
population clipping or special ecological solver is hidden in the graph.

The coexistence equilibrium is `(gamma/delta,alpha/beta)`: (2,2) by default,
or (2,3) for faster prey growth. The origin is also an equilibrium. Positive
continuous solutions stay positive and have a first integral:

```
H = delta*prey - gamma*log(prey) + beta*predator - alpha*log(predator)
dH/dt = 0
```

For positive parameters, neighboring positive orbits surround the coexistence
equilibrium; they are **not attracting limit cycles**. Different initial states
select different conserved levels. The near-equilibrium linearized angular
frequency is `sqrt(alpha*gamma)`, so the small-amplitude period is
`2*pi/sqrt(alpha*gamma)`.

Expect prey to increase when predator < alpha/beta, followed by predator growth
when prey > gamma/delta. Predator peaks follow prey peaks. The equilibrium
diagram remains constant: its phase portrait is a single point, not a failed
viewer. Faster prey growth changes the predator equilibrium, orbit and period.

## Experiments and numerical limits

1. Compare baseline and near-equilibrium orbits. Measure periods and phase lag
   after startup. Only small orbits should approximate the linearized period.
2. Change positive initial populations and predict whether each species
   initially increases using the derivative signs above.
3. Change alpha in `prey_rate` weights `[alpha,-beta]`; change beta there and
   delta/gamma in `predator_rate` weights `[delta,-gamma]`. Calculate the new
   equilibrium before running. The interaction block remains prey*predator.
4. Double the native rate (2 kHz, dt = 0.0005; faster-growth case: 8 kHz,
   dt = 0.000125) for the same duration. Run Configuration
   updates the integrator dt; no sample-count source events need rescaling.
   Compare state error and H drift against an independent reference.

Forward Euler does not preserve H and generally produces outward numerical
drift rather than a closed orbit. Do not mistake that spiral for ecological
instability or for an attracting cycle. Euler can also produce nonpositive
populations with an excessive dt; the logarithmic H diagnostic then becomes invalid.
The graph does not clamp those states, because clamping would change the model.
Longer runs or changed populations/parameters need fresh refinement checks.
The model omits carrying capacity, seasonal forcing, migration, extinction
thresholds and stochastic population fluctuations.

Probes are `prey` and `predator`, both dimensionless. At runtime step k they
represent `(k-1)*dt`, while the observation label is `k*dt`. Step 1 publishes
the initial state. Align reference samples with this state time.

## Headless reference checks

From the repository root:

```bash
python3 tools/generate_nonlinear_examples.py
cmake -S . -B build
cmake --build build --target s7tests_nonlinear_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_nonlinear_examples_smoke$' --output-on-failure
```

The test loads all four saved graphs and repeats at twice each native rate. Independent
long-double RK4 references run outside the production graph. One step versus
two half-steps must agree within 1e-8 over the full duration, and the reference
H drift must stay below 1e-9. Initial states and every timestamp are checked.
Published populations must stay finite, positive and below 12.

Maximum Euclidean population error must be below 0.5 and absolute H drift
below 0.1 over 40 time units. Both must decrease by a factor of 1.7–2.3 when
dt halves, except at the exact equilibrium where both stay below 1e-12.
Non-equilibrium cases must show repeated rising crossings of prey = 2, with
period between 4 and 10. The near-equilibrium period must be within 0.05 of
2*pi. These are finite-duration acceptance bounds, not a claim that Euler
conserves populations or the first integral exactly.
