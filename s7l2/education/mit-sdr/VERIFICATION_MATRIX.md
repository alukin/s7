# MIT SDR curriculum — verification and qualification matrix

**Planning record: 2026-11-20. No SDR lab is qualified by the existence of
this document.** The base is [S7 LAB_STANDARD](../LAB_STANDARD.md).
Use one row per lab ID; preserve previously observed failures and distinguish
machine-independent numeric facts from physical device evidence.

## Inherited status vocabulary

`proposed`, `source-reviewed`, `diagram-ready`,
`headless-verified`, `studio-verified`, `release-qualified`,
`blocked`, `deferred`. These apply to a named, pinned code revision.
**RF-qualified** is an independent dimension, **not** a synonym for
`release-qualified`; an offline teaching lab may be release-qualified
without any radio.

## Mandatory verification gates

| Gate | Tested object / acceptable evidence | Failures to exercise |
| --- | --- | --- |
| V0 Source | Exact MIT link, course term/lecture/topic, licence/provenance, diagram ownership; no invented reproduction claims | Invalid resource, copied exercise, missing dataset licence |
| V1 Static model | Saved diagram loads in production loader; explicit complex type, rate, delay/state/initial condition and parameter constraints | Invalid type, wrong rate, illegal graph cycles, invalid device selector |
| V2 Deterministic reference | Headless *saved S7 graph* outputs vs independently implemented equation, short direct DFT or separate reference receiver; exact early and late samples | Conjugate/IQ sign inversion, off-by-one delay, wrong FFT normalization, rate boundary |
| V3 Statistical | Seeded repeated AWGN trials; exact SNR/Eb/N0 conventions, BER/SER and confidence intervals against theory | Underpowered sample count, correlated seeds, incorrect bit/symbol alignment |
| V4 Viewer | Real Studio open -> run -> named scope/constellation/spectrogram -> edit -> rerun -> reopen, checked display values/axes/units/window normalization; packaged links | Wrong channel label, time origin, frequency direction, PSD unit or stale frame |
| V5 Recorded I/Q | Fixture SHA/licence/metadata; identical chain compares with simulation; clip/gap conditions handled; no hidden sample drop | Gaps, truncation, device format mismatch, incorrect timestamp epoch |
| V6 RX | Device/driver/settings readback, bounded real capture, controlled known signal, statistics and continuity/error reporting | Timeout, device removal, rate readback mismatch, gain surprises |
| V7 Coax TX/RX | Operator-authorized calibrated attenuator chain; finite TX; independent measurement; safe cancellation, power/duration limits | TX/radio fault, partial output, unexpected RX clipping, unsafe fixture abort |
| V8 OTA (optional) | Legal radio profile and operator approval, measured emissions & conditions, repeatable receiver result | Unexpected occupancy, out-of-envelope power/bandwidth; immediate safe stop |
| V9 Advanced clocks | Known synthetic offsets; qualified hardware time provenance; independent drift/phase/latency estimates with uncertainty | Lost coherent reference, software arrival mislabelled UTC, burst/sample gaps |
| V10 Packaging | Relocated source and release artifact, links/assets, default tests don't access hardware, RF opt-in clearly indicated | Missing plugin/dataset, installation path, unintentional TX |

A gate is not closed by GUI screenshots, RF observations alone, passing a
mock-driver test alone, or promises from an AI assistant. Record exact
command, commit SHA, host/driver, observed metrics, tolerance rationale and
result. The verifier must inspect a real display where UI validity is
claimed, not assume numerically recorded samples match rendering.

## Current curriculum qualification ledger

| Batch | Lab IDs | Status at planning date | Next independently checkable milestone |
| --- | --- | --- | --- |
| A: I/Q and RF fundamentals | SDR01–SDR07 | `proposed` | Identify exact core capabilities and implement first three graphs with direct complex-reference oracles |
| B: Waveforms/detectors | SDR08–SDR15 | `proposed` | Reference modulation, signal-space and matched-filter recurrences; seeded BER tests |
| C: Synchronization and OFDM | SDR16–SDR21 | `proposed` | Declare frame/rate/clock contracts; independently verify timing/CFO and small-size DFT |
| D: Physical receiver and advanced bench | SDR22–SDR24 | `proposed` | RX/device inventory and safety fixture; synthetic ADS-B parity; separate clock stress models |

There are **24 planned lab IDs**, and **zero new qualified SDR diagrams**
as of creation. Existing MIT-EE and generic hardware examples are
implementation references only. Update the ledger by lab when real tests
are committed; include failure status or explicit blocked reasons.
No automatic addition to S7 0.2.0 release gates.

## Numerical measurement conventions to freeze

- Complex baseband: `z[n] = I[n] + j Q[n]`; define spectrum sign, LO
  offset, bin order and sample time `n / f_s`; include IQ inversion
  negative controls.
- Frequencies in Hz/rad/sample, gains as dB amplitude/power with the
  correct factor 20/10; sample rate readback distinct from requested.
- FFT/window coherent gain, ENBW, one-/two-sided PSD and dBFS calibration.
  For complex baseband, a two-sided spectrum is the natural baseline.
- Convolution and RRC filter latency: exact tap indexing and group
  delay, boundary initial state and tail/padding policy.
- QAM normalization: symbol energy, average bit energy, Gray mapping and
  exact bit order; distinguish EVM, SER and BER. `Eb/N0` measured at
  explicitly chosen reference point and noise bandwidth.
- PLL/Costas/FLL: gain normalization, loop state, pull-in/capture range,
  frequency/phase offset, slips and residual-error metric.
- Frame synchronization: exact finite preamble, codebook, threshold,
  false-alarm criterion and metadata for fragments.
- ADS-B: distinguish 56/112-bit messages, valid preamble/pulse spacing and
  appropriate CRC/parity logic for the selected message type; compare
  against independently generated and published *permitted* vectors.
  Receiving a visible burst is not proof of decoding.
- Coherence: distinguish common signal, relative phase at RF inputs,
  sample clocks, timestamp provenance and network time. Drift estimates
  require an uncertainty statement and reference method.

## Capability work is part of the code verification program

A missing or incorrect feature discovered while implementing a lab is a
**documented S7 defect or supported-feature gap**, not a reason to adjust
the expected physics or silently weaken the lab. Reproduce it in the smallest
unit/integration test, define invariants and acceptance, implement the
capability, then rerun affected saved labs and their negative cases.
Treat changes to scheduler, rate propagation, state ownership,
multi-boundary hardware activation or network timing as architectural
changes requiring maintainer review and independent regression tests.

See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for ordered work.
