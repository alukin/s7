# Small-angle double-pendulum integrator comparison

Nine Studio diagrams: three initial conditions for each of the three existing
integrators. Open any `.s7diagram` here in Studio, set **Run Configuration** to
20 seconds, choose 1, 10 or 100 kHz, run, and open `scope_states`. Its four
channels are theta1, omega1, theta2, omega2. Angles are radians, velocities rad/s.
The default diagram discretization is 1 kHz; Studio updates integrator `dt`
when changing frequency. Inline probes expose the same four outputs to tests.

| Initial condition | Euler | Symplectic | Implicit Trapezoid |
|---|---|---|---|
| Slow: theta1=0.1, theta2=sqrt(2)*0.1 | [diagram](double_pendulum_slow_euler.s7diagram) | [diagram](double_pendulum_slow_symplectic.s7diagram) | [diagram](double_pendulum_slow_trapezoid.s7diagram) |
| Fast: theta1=0.1, theta2=-sqrt(2)*0.1 | [diagram](double_pendulum_fast_euler.s7diagram) | [diagram](double_pendulum_fast_symplectic.s7diagram) | [diagram](double_pendulum_fast_trapezoid.s7diagram) |
| Mixed: theta1=0.2, theta2=0.1 | [diagram](double_pendulum_mixed_euler.s7diagram) | [diagram](double_pendulum_mixed_symplectic.s7diagram) | [diagram](double_pendulum_mixed_trapezoid.s7diagram) |

All initial velocities are zero. `generate_diagrams.py` regenerates the nine
files using only Python's standard library. Each diagram includes its method,
initial conditions, equations and limitations in a text frame. This is a
linearized model; comparison here does not validate the full nonlinear pendulum.

## Equations and independent references

For m1=m2=l1=l2=1, g=9.81:

```
theta1'' = -2*g*theta1 + g*theta2
theta2'' =  2*g*theta1 - 2*g*theta2
```

Let `w-=sqrt(g*(2-sqrt(2)))`, `w+=sqrt(g*(2+sqrt(2)))`,
`A=(theta1(0)+theta2(0)/sqrt(2))/2`, and
`B=(theta1(0)-theta2(0)/sqrt(2))/2`. The continuous reference is

```
theta1(t) = A*cos(w-*t) + B*cos(w+*t)
theta2(t) = sqrt(2)*(A*cos(w-*t) - B*cos(w+*t))
```

Velocities are the analytical derivatives. Slow and fast periods are about
2.621 and 1.086 seconds. The physical quadratic energy is

```
E = (2*omega1^2 + 2*omega1*omega2 + omega2^2
     + g*(2*theta1^2 + theta2^2))/2
```

The test additionally derives independent closed-form discrete references:

- **Euler:** simultaneous forward Euler, modal amplification `1+i*w*h`.
  Amplitude/energy growth is expected for this undamped system.
- **Symplectic:** two `SymplecticIntegrator` elements, both reading the old
  position vector: `v[n+1]=v[n]+h*a(q[n])`,
  `q[n+1]=q[n]+h*v[n+1]`. Modal positions obey
  `q[n+1]-(2-(w*h)^2)*q[n]+q[n-1]=0`. Energy error is bounded in the tested
  regime; physical energy is not conserved exactly.
- **Implicit Trapezoid:** four `TrapezoidIntegrator` elements, no artificial
  delays. NodeRuntime jointly solves
  `z[n+1]=z[n]+h/2*(F(z[n])+F(z[n+1]))` over all four states.
  The modal amplification is `(1+i*w*h/2)/(1-i*w*h/2)`.
  This gives second-order state convergence and preserves the quadratic energy
  of this linear conservative system up to solve/rounding error. Default
  `mode="implicit"` uses bounded fixed-point correction; reduce `dt` if it
  reports nonconvergence. Stateful direct-feedthrough feedback cannot be
  reevaluated safely and is rejected. See the
  [numerical contract](../../docs/TRAPEZOID_INTEGRATION.md).

The supplied-sample `mode="sampled"` is a separate quadrature use case. Its
first call records the first input and it has direct feedthrough. The former
arrangement using sampled averaging plus added delays was first order and
showed substantial energy growth. Those extra delays have been removed from
these examples; they are not needed by the implicit ODE integration stage.

After runtime step `k`, the observed integrator state is `z[k-1]`, while the
runtime observation timestamp is `k*h`. This is the existing causal-memory
publication convention for all three methods. Tests check both separately. Trace files include
`time_s` and `state_time_s`; reference comparisons/plots use the latter.
No phase shift is fitted to hide a disagreement.

## Tests and generated artifacts

From the `s7l2` directory:

```bash
cmake -S . -B build
cmake --build build --target s7tests_coupled_pendulum_smoke s7studio_coupled_pendulum_test --parallel
ctest --test-dir build -R 's7tests_coupled_pendulum_smoke|s7studio_coupled_pendulum$' --output-on-failure
# Optional reviewable plots; gnuplot is not needed by the tests:
gnuplot -e "report_dir='build/s7tests/coupled_pendulum'" examples/double_pendulum/plot_results.gp
```

The full matrix takes several minutes in an unoptimized build.

The headless test is available with `S7_BUILD_STUDIO=OFF`; omit the Studio test
target in that build. Both tests load the committed diagrams. They exercise all
nine at 1/10/100 kHz for 20 model seconds (27 configurations per execution path).
The Studio test loads each scene, checks observer-channel/probe wiring and
serialization, then exercises `LocalSchemaRunner` on its worker and drains the
sample mailbox. It compares every channel with headless checkpoints and checks
all delivered sample counts, order and model timestamps. It does not render
Scope or qualify recording/overload behavior; those need separate tests.

Metrics are evaluated on a common 1 ms state-time grid plus initial/final
samples. Headless reports and 10 ms plotting traces are generated under
`build/s7tests/coupled_pendulum/`: `summary.csv`, 27 trace CSVs, and, after
running gnuplot, `mixed_mode_comparison.png` and `convergence.png`.
Tests keep no full-resolution in-memory trace. Throughput in the summary
includes sampling/reference/report overhead and is not a runtime benchmark.

Acceptance checks:

- Actual graph versus its closed-form discrete solution: max state residual
  at most `1e-8`, including startup and final publication.
- Continuous-reference maximum angle error decreases by a factor between
  7 and 20 for each tenfold frequency increase for Euler/Symplectic (first
  order), and between 70 and 130 for implicit Trapezoid (second order).
- At 100 kHz, maximum angle error is below `0.002 rad` in every case;
  implicit Trapezoid has the tighter `2e-8 rad` budget.
- Symplectic maximum relative energy error is bounded by `10*h` for these
  configurations. Implicit Trapezoid maximum relative energy error must
  remain below `1e-8`; Euler energy growth is reported.
- Bundled/headless checkpoint differences at most `1e-12`; every expected
  sample delivered, with zero mailbox drops.

A passing test establishes the stated discrete implementation and accuracy
budget. It does not make coarse Euler accurate or establish stiff/nonlinear
problem support for the implicit solver.

## Measured results

The reproducible summary/trace files and plots describe the current implicit
implementation. Earlier first-order delayed-Trapezoid results do not describe
these updated diagrams. The table below records the native Linux Debug build run on 2026-10-19.

Maximum absolute angle error over both pendulums, in radians:

| Mode | Method | 1 kHz | 10 kHz | 100 kHz |
|---|---|---:|---:|---:|
| Slow | Euler | 0.00822 | 0.00080123 | 7.9919e-05 |
| Slow | Symplectic | 0.00017105 | 1.6966e-05 | 1.6952e-06 |
| Slow | Implicit Trapezoid | 3.0858e-06 | 3.0858e-08 | 3.0858e-10 |
| Fast | Euler | 0.054759 | 0.004705 | 0.0004636 |
| Fast | Symplectic | 0.00043186 | 4.1149e-05 | 4.0945e-06 |
| Fast | Implicit Trapezoid | 4.5264e-05 | 4.5264e-07 | 4.5264e-09 |
| Mixed | Euler | 0.046071 | 0.004082 | 0.00040348 |
| Mixed | Symplectic | 0.0005075 | 4.9405e-05 | 4.9359e-06 |
| Mixed | Implicit Trapezoid | 3.1858e-05 | 3.1858e-07 | 3.1858e-09 |

Across all nine implicit-Trapezoid configurations, maximum relative energy
error was below `3e-13` and maximum disagreement with the closed-form discrete
solution was below `1e-13`. A tenfold frequency increase reduced angle error by
approximately 100 times. In the fast mode at 1 kHz, the old delayed arrangement's
angle error was about `0.13 rad`; the implicit method's is about `4.53e-5 rad`.

These measurements establish this conservative linear model's behavior and the
stated budgets. They do not validate arbitrary forcing, nonlinear, stiff or
chaotic models. Windows/Wine validation was not performed for this change.

All 27 configurations passed through Studio's scene loader and bundled runtime,
with every expected sample delivered and agreement with headless checkpoints
within `1e-12`. The full native build's 42 test cases passed across the regular
run and socket-enabled reruns of the five sandbox-blocked network cases.
