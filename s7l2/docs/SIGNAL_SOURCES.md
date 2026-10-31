# Deterministic model signal sources

The built-in registry exposes these sources in the `sources` category, including
Studio's element picker. Their C++ templates and F64 aliases are declared in
`s7/sources.hpp`, also included by `s7/elements.hpp` and `s7/core.hpp`.
All seven core generators support F16, F32, F64 and F128; parameter `type` can pin the output type.
The VCO frequency input shares its output type.

Sources are rate-following elements
([sample rate model, section 8](../../doc/S7_Sample_Rate_Model.md#8-model-time-and-element-rate-classification-2026-10-31)):
created from a document or in Studio, a source runs at the rate of its rate
domain, normally the run's `dt`, and declares none of its own. All durations
are times in seconds. A duration must be a whole number of samples at the run
rate; otherwise validation reports the element, the parameter and the rate.
Changing the run's `dt` keeps every event at the same model time and does not
edit the document. Sources advance once per runtime step, independently of
wall-clock scheduling. Constructing a fresh instance restarts its sequence.
Step zero is the first call to `step()`.

| Registry type / F64 C++ alias | Parameters besides `type` |
|---|---|
| `TriangleGenerator` | `amplitude=1`, `offset=0`, `frequency_hz=1`, `phase=0` cycles, `rise_fraction=0.5` |
| `PulseGenerator` | `low=0`, `high=1`, `frequency_hz=1`, `phase=0` cycles, `duty=0.5` |
| `ImpulseGenerator` | `area=1`, `start_time=0` s |
| `StepGenerator` | `initial_value=0`, `final_value=1`, `start_time=0` s |
| `ChirpGenerator` | `start_hz=1`, `end_hz=100`, `sweep_time=1` s, `amplitude=1`, `offset=0`, `phase=0` radians, `sweep=linear`, `end_behavior=hold` |
| `PRBSGenerator` | `order=7`, `seed=1`, `bit_time=0.001` s, `low=-1`, `high=1` |
| `ControlledOscillator` | `amplitude=1`, `offset=0`, `phase=0` radians; input port 0: `frequency_hz` |

Older documents store `sample_rate_hz` and sample counts (`start_sample`,
`sweep_samples`, `bit_samples`; `PWM`'s `period_samples`). They still load:
`sample_rate_hz` is ignored as a rate, and each count is converted to a time at
that stored rate (1000 Hz when absent, the old default), so the event keeps its
model time at any run rate. A source built in C++ with an explicit sample rate
is a fixed pin at that rate instead; passing `std::nullopt` makes it follow.

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

`start_time` is a nonnegative time in seconds. It must fall on the model grid,
`start_time = n * dt` for a whole number n, which is checked at the run rate;
for an off-grid time, choose explicitly whether to round up or down.

An impulse emits exactly one sample of height `area / dt` at `start_time`; all
other samples are zero. Thus `sum(output * dt) = area` within floating-point
rounding. This is a sampled delta, not a finite-width rectangular pulse.

A step emits `initial_value` before `start_time`, and `final_value` at and after
it. At the default time zero it emits the final value immediately.

An `EulerIntegrator` driven by an impulse of area one publishes a unit step
**one sample after** the impulse: the integrator stages its updated state for
the next runtime step. This is the normal causal-memory convention. A sampled
trapezoid quadrature block has different discrete transient behavior.

For example, the following computational schema, run at `dt = 1 ms`, emits an
impulse at 2 ms and publishes its integrated unit step at 3 ms:

```json
{
  "name": "delta_to_step",
  "dt": 0.001,
  "elements": [
    {"id": "delta", "type": "ImpulseGenerator",
     "params": {"start_time": 0.002, "area": 1}},
    {"id": "integral", "type": "EulerIntegrator"}
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
current phase, then advances phase by `2*pi*frequency_hz[n]*dt`.
Frequency changes preserve phase continuity; zero holds phase and negative
frequency reverses rotation. Because the current input updates the next phase,
its influence first appears in the next output sample. The element conservatively
declares direct feedthrough; use a causal-memory element to close feedback loops.

Use ordinary `Gain` and `Adder` blocks to implement the voltage-to-frequency
law `frequency_hz = base_frequency + sensitivity_hz_per_volt * voltage`.

## Chirp frequency sweeps

`ChirpGenerator` emits a sine with a linear or logarithmic frequency sweep.
Its duration is `sweep_time` model seconds, a positive whole number of samples
`N = sweep_time / dt`. Frequency sample n is:

- Linear: `(1-n/N)*start_hz + (n/N)*end_hz`.
- Logarithmic: `exp(log(start_hz) + (n/N)*(log(end_hz)-log(start_hz)))`.

Logarithmic sweeps require positive endpoints. Linear sweeps support descending,
zero and negative frequencies. The phase parameter is in radians. Like the VCO,
output sample n uses the current phase, then advances it by `2*pi*f[n]*dt`.
This is a discrete frequency sweep, rather than exact integration of a continuous
chirp formula between sample times.

With `end_behavior=hold`, frequency reaches `end_hz` at sample N and stays
there. With `repeat`, samples 0 through N-1 repeat the frequency sweep; phase
continues across the boundary rather than resetting. The final endpoint is not
a separate sample in repeat mode. Output amplitude and offset behave as for the VCO.

## PRBS excitation

`PRBSGenerator` supplies deterministic two-level excitation, useful for system
identification. Supported orders are 7 and 15, with bit periods 127 and 32767.
Each bit is held for `bit_time` seconds, a positive whole number of samples
`bit_time / dt`, so the bit rate is `1/bit_time` and the sequence period is
`(2^order-1)*bit_time` seconds.

The generator uses a left-shifting Fibonacci LFSR with feedback from bits
`order-1` and `order-2` (zero-based). It emits the most-significant bit before
each shift, maps zero to `low` and one to `high`, and masks to the selected
width. These correspond to polynomials `x^7+x^6+1` and `x^15+x^14+1`.
The seed must be nonzero and fit the selected order. A new instance with the
same seed reproduces the sequence exactly. No wall-clock seed is used.

## Recording and replay (`s7io`)

`RecordingSink` saves F64/C64 signals, and `RecordedSource` replays them.
Both are available in Studio, `s7runtime` and `s7ctl`. The default format is
compact binary: 8 bytes per real sample or 16 bytes per complex sample.
Readable Gnuplot text is also supported through `format=gnuplot-text`.

Every recording has a versioned header containing the signal type, exact sample
rate and a configurable short UTF-8 description. Replay takes its rate from the
header by default and supports error/hold/repeat at EOF. Sink construction during
validation and compilation never creates or truncates a file.

See [RECORDING_FORMAT.md](RECORDING_FORMAT.md) for the byte-level binary layout,
text header, parameters, examples, buffering and file lifecycle.

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
cmake --build build --target s7core_sources_smoke s7io_replay_smoke
ctest --test-dir build -R '^(s7core_sources_smoke|s7io_replay_smoke)$' --output-on-failure
```

The smoke test covers waveform samples and boundaries, asymmetric triangles,
reverse phase, duty endpoints, step/impulse timing, impulse integration, VCO
frequency changes and phase hold, high-rate phase accumulation, registry
factories/defaults/type inference, invalid parameters and conflicting rates.
Waveform, VCO and integration checks run for all four real floating types.

The second batch adds full-period PRBS window/balance checks, chirp phase checks
for both sweep laws and end behaviors, and replay tests for real/complex signals
in both file formats, EOF modes, registry factories and malformed recordings.
The runtime control smoke test also checks recorded replay through `RuntimeService`.
