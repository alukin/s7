# S7 Test Philosophy

S7 tests are based on well-known examples from DSP and Digital Control theory.

The goal is not just regression protection for implementation details, but verification that
core mathematical behavior matches canonical discrete-time systems.

## Current Suite

Executable: `s7tests`

- FIR impulse response
  - Example: symmetric 3-tap FIR
  - Expected response: exact coefficient sequence on impulse input

- First-order IIR step response
  - Example: `y[k] = (1-a) x[k] + a y[k-1]`
  - Expected response: `y[k] = 1 - a^(k+1)` for unit step input

- Harmonic vs digital oscillator agreement
  - Condition: 200 samples per period, 100 periods
  - Metric: RMS error from energetic difference element
  - Requirement: RMS below a strict threshold

- Harmonic vs classical 2-integrator oscillator agreement
  - System: `dx/dt = v`, `dv/dt = -x` (omega = 1 rad/s), Euler forward integration
  - Topology: two `Integrator<Real>` elements and two `Sum<Real>` scaling elements
    in a feedback loop (mirrors `classical_oscillator_2integrators.json`)
  - Condition: dt = 0.001 s, x(0) = 1, v(0) = 0 → x(t) = cos(t); 3 periods
  - Metric: RMS error of x[k] vs `HarmonicOscillator` (phase = π/2 → cos)
  - Requirement: RMS < 5e-2 (threshold accounts for O(dt) Euler drift)

## Running Tests

```bash
cmake --build /home/al/projects/s7/s7pp/cmake-build-debug -j
ctest --test-dir /home/al/projects/s7/s7pp/cmake-build-debug --output-on-failure
```

## Extension Rules

When adding tests:

1. Prefer theory-backed reference behavior.
2. State the equation being verified.
3. Use quantitative thresholds where exact equality is not expected.
4. Keep tests deterministic and independent of insertion order.

