# S7 Element Library (Built-ins)

**Status:** Active  
**Purpose:** Reference for built-in elements, equations, pins, and parameters  
**Last Updated:** May 13, 2026

---

This document describes built-in S7 elements, their equations, pin signatures, state behavior, and netlist parameter keys.

## 1. Common Conventions

- Signal types are strict: `Real -> Real`, `Complex -> Complex`.
- Input pins are taps to upstream output values.
- `kIsStateful`: element has internal state.
- `kHasDirectFeedthrough`: current output depends on current input in same tick.
- Lifecycle:
  1. `Schema::prepareExecution()`
  2. `Element::calculateParameters()` for all elements
  3. repeated `Element::tick()`

## 2. Netlist Value Types

JSON param values support:
- `number` -> `Real`
- `{ "real": x, "imag": y }` -> `Complex`

## 3. Element Reference

## 3.1 `Constant`

- Type names: `Constant`
- Variants: `Constant<Real>`, `Constant<Complex>`
- Pins: input `0`, output `1`
- Traits: stateful `false`, direct feedthrough `false`
- Equation: `y[k] = value`
- Params:
  - `value` (Real or Complex), default `0.0`

## 3.2 `Integrator`

- Type names: `Integrator`
- Variants: `Integrator<Real>`, `Integrator<Complex>`
- Pins: input `1`, output `1`
- Traits: stateful `true`, direct feedthrough `false`
- Equation: `state = state + input`, output is `state`
- Params:
  - `init_state` (Real or Complex), default `0.0`

## 3.3 `Sum`

- Type names: `Sum`
- Variants: `Sum<Real>`, `Sum<Complex>`
- Pins: input `N`, output `1`
- Traits: stateful `false`, direct feedthrough `true`
- Equation: `y[k] = sum_i w[i] * u_i[k]`
- Params:
  - `weight` (single scalar weight)
  - or indexed `w0`, `w1`, ... (contiguous)
  - defaults to two real weights: `[1.0, 1.0]`
- Notes:
  - Do not mix `weight` with indexed `w*` keys.
  - All weights must be all-Real or all-Complex.

## 3.4 `FIRFilter`

- Type names: `FIRFilter`
- Pins: input `1`, output `1`
- Traits: stateful `true`, direct feedthrough `true`
- Equation:
  - `y[k] = sum_i b[i] * x[k-i]`
  - internal `x_state` is explicit `z^-1` chain
- Params:
  - `b0`, `b1`, ... (contiguous from `b0`), default `[1.0]`
  - optional initial state `x0`, `x1`, ... (contiguous from `x0`)
- Notes:
  - All coefficients/states are `Real`.

## 3.5 `IIRFilter`

- Type names: `IIRFilter`
- Pins: input `1`, output `1`
- Traits: stateful `true`, direct feedthrough `true`
- Equation:
  - `y[k] = sum_i b[i] * x[k-i] - sum_j a[j] * y[k-j-1]`
  - `a` stores `a1..aN` (`a0` is implicitly `1`)
  - internal `x_state`, `y_state` are explicit `z^-1` chains
- Params:
  - `b0`, `b1`, ... (contiguous from `b0`), default `[1.0]`
  - `a1`, `a2`, ... (contiguous from `a1`)
  - optional initial state `x0..` and `y0..` (both contiguous from `0`)
- Notes:
  - `a0` is not accepted as a parameter key.

## 3.6 `DigitalOscillator`

- Type names: `DigitalOscillator`
- Pins: input `0`, output `1`
- Traits: stateful `true`, direct feedthrough `false`
- Form:
  - implemented via `IIRFilter` core
  - second-order resonator:
  - `y[k] = 2*r*cos(w)*y[k-1] - r^2*y[k-2]`
  - `w = 2*pi*frequency_hz*dt`
- Params:
  - `frequency_hz` (Real), default `440.0`
  - `amplitude` (Real), default `1.0`
  - `phase_rad` (Real), default `0.0`
  - `radius` (Real), default `1.0`
- Notes:
  - `radius > 0` is required.
  - Coefficients are precomputed in `calculateParameters()`.

## 3.7 `ToComplex`

- Type names: `ToComplex`
- Pins: input `2` (`Re`, `Im`), output `1` (`Complex`)
- Traits: stateful `false`, direct feedthrough `true`
- Equation: `y = complex(re, im)`
- Params: none

## 3.8 `ToReals`

- Type names: `ToReals`
- Pins: input `1` (`Complex`), output `2` (`Re`, `Im`)
- Traits: stateful `false`, direct feedthrough `true`
- Equation:
  - `out0 = real(in)`
  - `out1 = imag(in)`
- Params: none

## 3.9 `Vca`

- Type names: `Vca`
- Pins: input `2` (`signal`, `gain`), output `1`
- Traits: stateful `false`, direct feedthrough `true`
- Equation: `y = signal * gain`
- Params: none

## 3.10 `Vco`

- Type names: `Vco`
- Pins: input `1` (`cv`), output `1`
- Traits: stateful `true`, direct feedthrough `false`
- Equation:
  - `freq = base_hz + sensitivity_hz_per_v * cv`
  - `phase += 2*pi*freq*dt`
  - `y = amplitude * sin(phase)`
- Params:
  - `base_hz`, default `440.0`
  - `sensitivity_hz_per_v`, default `100.0`
  - `amplitude`, default `1.0`
  - `initial_phase_rad`, default `0.0`
- Notes:
  - static terms are precomputed in `calculateParameters()`.

## 3.11 `HarmonicOscillator`

- Type names: `HarmonicOscillator`
- Pins: input `0`, output `1`
- Traits: stateful `true`, direct feedthrough `false`
- Equation:
  - `y[n] = amplitude * sin(2*pi*frequency_hz*n*dt + phase_rad)`
- Params:
  - `frequency_hz`, default `1.0`
  - `phase_rad`, default `0.0`
  - `amplitude`, default `1.0`
- Notes:
  - `omega_dt` is precomputed in `calculateParameters()`.

## 4. Validation Errors (Typical)

- Algebraic loop among direct-feedthrough dependencies -> `SchemaExecutionError`.
- Non-contiguous indexed params (`w*`, `b*`, `a*`, `x*`, `y*`) -> runtime error during build.
- Type mismatch (`Real` vs `Complex`) at connect-time -> runtime error.

## 5. Recommended Usage Pattern

1. Build schema and connect elements.
2. Call `run()` (or explicitly `prepareExecution()` then `tick()` loops).
3. If you modify structure/params between runs, rebuild/prepare again before stepping.

