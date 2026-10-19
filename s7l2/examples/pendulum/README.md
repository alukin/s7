# Nonlinear single pendulum: 100-second quality benchmark

Open [Euler](nonlinear_pendulum_euler.s7diagram),
[Symplectic](nonlinear_pendulum_symplectic.s7diagram), or
[implicit Trapezoid](nonlinear_pendulum_trapezoid.s7diagram) in Studio.
Set Run Configuration to **100 seconds**, first **1 kHz**, then **10 kHz**.
Open `scope_states` to view angle and angular velocity.

The model is `theta''=-9.81*sin(theta)`, initial angle **1.5 rad**, initial
velocity zero. This large-amplitude pendulum tests nonlinear dynamics, period,
phase, long-run energy behavior and integration convergence.
Energy is `omega^2/2+9.81*(1-cos(theta))`.

## Budgets declared before the comparisons

[quality_budgets.json](quality_budgets.json) is the machine-readable contract,
shared by the tests. Each method faces the same accuracy budgets:

| Rate | Max angle error | Max velocity error | Max relative energy error | Max period error |
|---|---:|---:|---:|---:|
| 1 kHz | 0.01 rad | 0.03 rad/s | 0.005 | 0.00005 s |
| 10 kHz | 0.001 rad | 0.003 rad/s | 0.0005 | 0.0000005 s |

Euler is an expected **accuracy failure** baseline: its regression test
checks that the failure and energy growth are reported. A passing regression
does not turn that trajectory into an accurate simulation. Symplectic and
Trapezoid must pass the budgets. Over the first five seconds, tenfold timestep
refinement must reduce maximum angle error by 7–15 times for Euler/Symplectic,
and 70–130 times for Trapezoid. Trapezoid endpoint equation residual must be
below `1e-12`; its maximum relative energy error must also be at most `20*dt^2`.

## Independent reference and time alignment

Let `k=sin(1.5/2)`, `K=ellint_1(k)`, `u=K-sqrt(9.81)*t`:

```
theta(t) = 2*asin(k*sn(u,k))
omega(t) = -2*k*sqrt(9.81)*cn(u,k)
period = 4*K/sqrt(9.81)
```

Reference values use Boost.Math elliptic functions in long double. The period
is independently checked using the arithmetic-geometric mean, and the
trajectory against a separate long-double RK4 implementation at two timesteps.
The finer RK4 error must be below `1e-8` and improve by at least tenfold.
Sources: [Boost.Math modulus convention and functions](https://www.boost.org/latest/libs/math/doc/html/math_toolkit/jacobi/jacobi_elliptic.html)
and [NIST DLMF pendulum application](https://dlmf.nist.gov/22.19).

Runtime observation timestamps are `step*dt`; these causal-memory models
publish the state at `(step-1)*dt`. Tests verify both labels explicitly;
physical reference comparisons use state time. Zero-crossing periods are
interpolated using consecutive full-rate state samples. State/reference
errors are measured on a 1 ms grid plus the last state; energy and implicit
endpoint residuals are checked at every step. Initial/final values and
maximum crossing-time drift are reported.

The Studio test must deliver all samples with no mailbox loss, match the
headless trace within `1e-12`, and export every timestamp/value through Scope
CSV with exact double round-trip equality. These are numerical and recording
checks for this workload, not a general overload/performance qualification.

## Reproduce the comparisons

From the `s7l2` directory:

```bash
cmake -S . -B build
cmake --build build --target s7tests_nonlinear_pendulum_smoke s7studio_nonlinear_pendulum_test --parallel
ctest --test-dir build -R 's7tests_nonlinear_pendulum_smoke|s7studio_nonlinear_pendulum$' --output-on-failure
# Optional figures:
gnuplot -e "report_dir='build/s7tests/nonlinear_pendulum'" examples/pendulum/plot_results.gp
```

The headless test also works in `S7_BUILD_STUDIO=OFF` builds; omit the Studio
target there. Generated reports and decimated traces live under
`build/s7tests/nonlinear_pendulum/`; the recording report is under
`build/s7studio/nonlinear_pendulum/`. Full-rate comparison streams and CSV files
are temporary test artifacts, processed sequentially with bounded memory.
The Studio test loads actual diagram/observer wiring and exercises the bundled
runner, live Scope recording and the CSV writer used by the export action.
It does not automate the native file-picker dialog or qualify all run controls.
`generate_diagrams.py` reproduces the three diagrams using standard Python.

## Measured results (Linux, 2026-10-19)

All runs cover 100 model seconds. The reference period is 2.3309867821412907 s.

| Method | Rate | Accuracy budget | Max angle error (rad) | Max relative energy error | Max period error (s) |
|---|---:|---|---:|---:|---:|
| Euler | 1 kHz | FAIL (expected) | 3.69053 | 0.736337 | 0.534086 |
| Euler | 10 kHz | FAIL (expected) | 2.51887 | 0.0690117 | 0.0326092 |
| Symplectic | 1 kHz | PASS | 0.00214726 | 0.00138854 | 6.74185e-8 |
| Symplectic | 10 kHz | PASS | 0.000213618 | 0.000138725 | 6.74173e-10 |
| Trapezoid | 1 kHz | PASS | 0.000248200 | 7.59671e-7 | 1.35969e-6 |
| Trapezoid | 10 kHz | PASS | 0.00000248200 | 7.59676e-9 | 1.35970e-8 |

The RK4 reference cross-check maximum state errors were 3.72489e-9 and
3.06182e-10 at 2 ms and 1 ms respectively. All refinement and implicit
endpoint-residual checks passed. Trapezoid has bounded, timestep-dependent
energy error on this nonlinear example; it does not conserve this energy exactly.
Euler accumulates substantial phase and energy errors even at 10 kHz.

All six bundled-runner/Scope comparisons captured every sample: 200,000 values
per 1 kHz run and 2,000,000 per 10 kHz run, with zero mailbox drops and zero
runtime difference from headless execution. All 6.6 million exported CSV rows
preserved timestamps and values exactly on parsing back to double. Scope CSV
now writes 17 significant digits; its former six-digit formatting could lose
recorded precision. This evidence covers these two channels and this graph on
the tested Linux build, not a universal performance envelope.
