# Deterministic model signal sources

The built-in registry exposes these sources in the `sources` category, including
Studio's element picker. Their C++ templates and F64 aliases are declared in
`s7/sources.hpp`, also included by `s7/elements.hpp` and `s7/core.hpp`.
All five support F16, F32, F64 and F128; parameter `type` can pin the output type.
The VCO frequency input shares its output type.

Each instance declares its `sample_rate_hz` (default 1000). Connected integrators
and other rate-pinned elements must agree. Sources advance once per runtime
step, independently of wall-clock scheduling. Constructing a fresh instance
restarts its sequence. Step zero is the first call to `step()`.

| Registry type / F64 C++ alias | Parameters besides `type` and `sample_rate_hz` |
|---|---|
| `TriangleGenerator` | `amplitude=1`, `offset=0`, `frequency_hz=1`, `phase=0` cycles, `rise_fraction=0.5` |
| `PulseGenerator` | `low=0`, `high=1`, `frequency_hz=1`, `phase=0` cycles, `duty=0.5` |
| `ImpulseGenerator` | `area=1`, `start_sample=0` |
| `StepGenerator` | `initial_value=0`, `final_value=1`, `start_sample=0` |
| `ControlledOscillator` | `amplitude=1`, `offset=0`, `phase=0` radians; input port 0: `frequency_hz` |

The corresponding template names append `T<SignalType>`, for example
`TriangleGeneratorT<SignalType::F128>`.

## Triangle and sawtooth

Amplitude is half the peak-to-peak range; the range is
`offset-amplitude` to `offset+amplitude` for nonnegative amplitude.
At phase zero a triangle starts at its minimum. It rises for `rise_fraction`
of a period, then falls for the remainder:

- `0.5`: symmetric triangle.
- `1`: rising sawtooth; phase zero is the minimum.
- `0`: falling sawtooth; phase zero is the maximum.

Phase is normalized to [0,1). Negative frequency reverses phase progression;
zero frequency freezes the waveform at the configured phase. Negative amplitude
inverts it. Initial phases outside [0,1) are wrapped.

## Pulse train

The output is `high` when normalized phase is strictly less than `duty`, and
`low` otherwise. Duty must be in [0,1]; its endpoints produce constant low and
constant high. Frequency and initial phase have the same conventions as the
triangle source. Unlike `PWM`, this source has a fixed duty parameter rather
than a latched duty input.

## Delta impulse and sigma step

`start_sample` is a nonnegative, zero-based model sample index. To schedule an
event at a time that lies on the model grid, use `start_sample = time / dt`.
For off-grid times, choose explicitly whether to round up or down.

An impulse emits exactly one sample of height `area * sample_rate_hz` at
`start_sample`; all other samples are zero. Thus `sum(output * dt) = area`
within floating-point rounding, with `dt = 1/sample_rate_hz`. This is a sampled
delta, not a finite-width rectangular pulse.

A step emits `initial_value` before `start_sample`, and `final_value` at and
after that sample. At the default index zero it emits the final value immediately.

An `EulerIntegrator` driven by an impulse of area one publishes a unit step
**one sample after** the impulse: the integrator stages its updated state for
the next runtime step. This is the normal causal-memory convention. A sampled
trapezoid quadrature block has different discrete transient behavior.

For example, the following computational schema emits an impulse at 2 ms and
publishes its integrated unit step at 3 ms:

```json
{
  "name": "delta_to_step",
  "elements": [
    {"id": "delta", "type": "ImpulseGenerator",
     "params": {"sample_rate_hz": 1000, "start_sample": 2, "area": 1}},
    {"id": "integral", "type": "EulerIntegrator", "params": {"dt": 0.001}}
  ],
  "connections": [
    {"from_element": "delta", "from_port": 0,
     "to_element": "integral", "to_port": 0}
  ]
}
```

## Frequency-controlled oscillator (VCO)

`ControlledOscillator` emits `offset + amplitude * sin(phase)`. Its initial
phase uses radians, matching `AnalogOscillator`. It emits sample n at the
current phase, then advances phase by `2*pi*frequency_hz[n]/sample_rate_hz`.
Frequency changes preserve phase continuity; zero holds phase and negative
frequency reverses rotation. Because the current input updates the next phase,
its influence first appears in the next output sample. The element conservatively
declares direct feedthrough; use a causal-memory element to close feedback loops.

Use ordinary `Gain` and `Adder` blocks to implement the voltage-to-frequency
law `frequency_hz = base_frequency + sensitivity_hz_per_volt * voltage`.

## Numerical limits and errors

Timing parameters use double precision independently of the output type.
Periodic phase uses at least double precision (F128 for F128 signals), so F16
outputs can run at high sample rates without their phase increments stalling.
Waveform output and level parameters retain the selected signal type.

Rates must be finite, positive and representable by S7's rate model. Levels,
amplitudes, offsets, frequencies and phases must be finite. Invalid duty/rise
fractions are errors; they are not silently clamped. Nonfinite VCO input is an
execution error. Impulse construction rejects a height that overflows the
selected output type; for example, a unit-area impulse at 100 kHz needs a type
wider than F16. Arithmetic and output rounding otherwise follow the chosen
floating type.

Sources are sampled waveforms, without antialiasing. Frequencies above Nyquist
alias; triangle/sawtooth/pulse harmonics can alias even below Nyquist. Choose a
model rate that resolves the dynamics and switching transitions of interest.

## Verification

```bash
cmake --build build --target s7core_sources_smoke
ctest --test-dir build -R '^s7core_sources_smoke$' --output-on-failure
```

The smoke test covers waveform samples and boundaries, asymmetric triangles,
reverse phase, duty endpoints, step/impulse timing, impulse integration, VCO
frequency changes and phase hold, high-rate phase accumulation, registry
factories/defaults/type inference, invalid parameters and conflicting rates.
Waveform, VCO and integration checks run for all four real floating types.
