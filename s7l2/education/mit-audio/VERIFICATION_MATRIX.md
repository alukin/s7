# MIT Audio — independent numerical and real-time verification

**Baseline: planning only (2026-11-20). AU01–AU12 are proposed; zero
new audio labs are qualified by this document.**

All qualification follows the shared [LAB_STANDARD](../LAB_STANDARD.md).
A saved graph is the object under test, not a hand-coded substitute.
For relevant labs maintain *separate* dimensions:

- **Numerical**: exact saved S7 graph versus analytical/direct-recursion
  reference with specified rate, initial states and phase conventions.
- **File**: known reference PCM/WAV samples, boundary/tail/error handling,
  sample indices and channels. No external audio devices.
- **Live functional**: graph executes through explicitly opened real
  audio input/output; underrun/overrun/stop and device changes tested.
- **Live latency**: independent physical analog measurement with
  repeated trials and stated uncertainty. A virtual sink or callback
  completion timestamp does not establish this quantity.
- **Student/UI**: Studio load -> run -> inspect -> edit -> rerun
  with correct axes, units and sample mapping, plus packaging links.

## Verification gates

| Gate | Independent evidence required | Required negative control |
| --- | --- | --- |
| A0 | Exact MIT 6.341 reference and separate S7-original effect objective; fixture rights | Unsupported exact-assignment or redistributed audio claim |
| A1 | Saved diagram loads with actual production elements, types, rate, state and valid causality | Incorrect channel mapping, invalid cycle, incompatible rates |
| A2 | Headless output matches independent closed form or direct recurrence, including early/late/tail | Delay off by one, wrong sample format, wrong gain/phase |
| A3 | WAV and recorded PCM replay is exact under declared conversion/normalization | EOF, truncated header, stereo inversion, clip, unpresented tail |
| A4 | Full-duplex fake backend confirms input frame -> processed frame -> output mapping, and no hidden resampling | Clock mismatch, short callback, worker stall, failure rollback |
| A5 | Physical full-duplex confirms explicit input/output device, negotiated rate, complete counters | Missing interface, disconnect, overflow, underrun, interrupted startup |
| A6 | Physical analog loopback reports median, p95, p99, peak observed lag, uncertainty, repeated trials, buffers and baseline | Mislabel software timestamp as ADC/DAC; hidden lag spikes/dropouts |
| A7 | Studio view and effect controls yield measured expected output and stable state | Stale scope data, inverted axes, unsafe parameter jump |
| A8 | Offline and live processing of equivalent captured samples match within justified accuracy | Split blocks, varying block length, buffer wraparound |
| A9 | Release artifact contains documents/diagrams/test fixtures with working paths; normal CI never opens audio | Missing plugin, binary fixture or activation guard |

## Important quantities and definitions

At 48 kHz, one 64-frame buffer lasts 1.333 ms; 128-frame buffer
2.667 ms; 256-frame buffer 5.333 ms. These are nominal durations,
**not end-to-end latency promises**.

**Physical input-to-output lag** must be obtained from input and output
analog signal observations with a shared reference/correlation or a
documented equivalent; record source and device calibration uncertainty,
host/driver/buffer settings and repeat measurements. Separate:
graph processing time, queue residence, interface/OS delay and
physical propagation/acquisition/presentation.

For streaming, preserve frame sequence and continuity, and report
actual vs accepted and delivered/presented values separately. A sound
driver's callback timestamp is not automatically a calibrated audio
converter timestamp. Record xrun and emergency-output counts, not merely
the number of frames accepted by the S7 graph.

For effect mathematics specify filter state convention, output sample
time, allpass/comb stability domain, fractional-delay interpolation,
clipping curve/normalization and dB reference. For stochastic sound
or perceptual comparisons use blinded/seeded trials only when a
meaningful quantitative claim needs them; subjective sound quality
does not replace objective correctness.

## Initial qualification ledger

| Labs | Status | Initial qualified milestone |
| --- | --- | --- |
| AU02 | Proposed | Fake and physical identity full-duplex; independently measured analog loopback |
| AU01 | Proposed | Exact synthetic WAV read/write, dry run and output drain |
| AU03, AU04, AU06, AU07 | Proposed | Independent DSP recurrences and safe live variants |
| AU05, AU08–AU11 | Proposed | Reference interpolation, time-varying state, feedback and frame tests |
| AU12 | Proposed | Combined pedalboard and cross-backend load/latency report |

Log exact code commit, test command, machine/toolchain, tested backend,
device, buffer size, effective sample rate, fixture, expected result,
measured result, failure case and operator approval for each promotion.
**Do not mark any AU lab qualified until both required tests and evidence
exist.** This course is designed to expose S7 runtime defects, not hide them.
