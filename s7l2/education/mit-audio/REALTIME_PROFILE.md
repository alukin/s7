# Real-time audio execution profile — proposed architecture and measurement plan

**Design proposal, not implemented or benchmarked (2026-11-20).**
Existing `s7audio` exposes separate source/sink providers, and the current
`s7hardware::Run` permits **one boundary and one exact rate domain**.
Changing that is an explicit architectural decision, **not a minor lab fix**.
Consult [hardware contract](../../../doc/S7_Hardware_IO_Contract.md),
[ADR history](../../../doc/S7_Architectural_Decisions.md),
[hardware implementation guide](../../docs/HARDWARE_IO.md)
and [live-test record](../../docs/HARDWARE_IO_LIVE_TESTS.md) before code.

## Proposed first execution contract

Implement a **single physical/virtual audio device, common sample clock,
full-duplex** profile; the user's chosen hardware route must explicitly
identify capture and playback channels from one driver session or
documented same-clock backend pair.

`AudioSource -> zero or more S7 causal DSP elements -> AudioSink`

- A single run owner coordinates activation, input/output streams,
  lifetime, graph compilation, direction and rate negotiation.
- **Input device clock is the master**. Graph time advances for each
  admitted sample frame; output consumes the corresponding output stream
  at the admitted rate. Distinguish ADC capture index, graph-step index,
  DAC accepted frame and physical presentation (usually unknown).
- In the first profile do not silently resample, drop/repeat, or invent
  samples to conceal clock mismatch. **Different unclocked devices are
  rejected** until an explicit clock-drift/resampling contract is added.
- Preserve S7's existing causal step semantics, explicit Delay memory,
  state commit/rollback and observer noninterference. Compare full-duplex
  output against the *same offline diagram* for identical input fixtures.
- The backend callback **must not run arbitrary S7 graph code**, allocate,
  lock a mutex, perform I/O, plot or log. Use preallocated bounded queues
  and a specifically owned worker. Measure whether that architecture
  actually meets the latency budget.
- Coordinate bounded prefill without introducing an accidental extra
  graph step. Define initial silence and output mapping to the first input
  sample; distinguish startup latency from steady-state latency.
- Terminal/drain contract: `finish` drains the accepted finite output
  prefix when possible within a bounded deadline; `abort/cancel`
  stops promptly and reports the undelivered tail distinctly. Capture
  overrun, playback underrun, stream disconnect, partial processing and
  device replacement produce explicit terminal status.
- Dynamic controls (effect coefficients, bypass, dry/wet) use controlled
  staging/crossfade or explicitly documented reset behavior. Avoid
  state mutation from UI threads; prevent glitches and coefficient
  discontinuities where promised.
- Multi-rate graphs, separated input/output clocks, external audio
  routing and remote real-time audio are **future extensions** with their
  own contracts. Do not claim hard real-time scheduling guarantees.

## Latency budget and instrumentation

Target (initial **engineering hypothesis**, not acceptance assumption):
**under 10 ms measured analog input-to-output for a specific 48 kHz,
headphone-capable single-interface configuration in steady state**.
This must be tested under repeatable normal/stressed host loads.

At 48,000 samples/s: 64 frames = 1.333 ms; 128 = 2.667 ms;
256 = 5.333 ms; 512 = 10.667 ms. These are *frame durations*, not
guaranteed system latency and not a promise about callback granularity.

Record independent quantities:
1. **Graph processing**: wall/CPU processing duration per worker batch
   and histogram, plus processed samples/s, max/mean/p50/p95/p99.
2. **Queues and scheduling**: capture queue residence, output queue
   prefill/occupancy, late cycles, missed service deadlines, underflows,
   overflows, emergency-zero frames and recovered/discontinuous frame
   counts. Host timestamps are not ADC/DAC timestamps.
3. **Device/host path**: negotiated sample rate/period/latency,
   backend and driver; channel count, endpoint identity, declared
   sample format and sound server settings; physical I/O path.
4. **Physical analog loopback**: known impulses/MLS/chirps supplied
   to an ADC, S7 identity path to a DAC, record DAC's analog output
   through a separate reference/return input with **proper levels**.
   A signal split/reference capture permits input/output lag
   via cross-correlation. Report peak or group delay, uncertainty,
   sample-period resolution and calibration offset as applicable.
   Do not confuse digital virtual monitor results or software
   completion timestamps with this physical measurement.

Store configuration/commit SHA, test duration, number of independent
trials, power/CPU conditions, channels, rate, frames per period and
full latency distribution. Silence glitches are not acceptable
"successful continuity". Report exact dropout count and whether
an interruption produced a recoverable or terminal error.

**Comparison basis:** (a) direct interface/host software monitor path,
(b) identity graph S7 path, (c) each effects graph. The deltas
identify S7 overhead separately from interface/OS cost. Baseline
measurement and uncertainty take precedence over arbitrary 10 ms pass
claims. A 394.29 ms previous two-host **SSH PCM** round-trip is a
different experiment, not relevant evidence of local physical guitar
latency.

## Fault injection and rollback

- Sustained artificial worker stall above the buffer budget.
- Consecutive short/irregular callbacks, partial input/output,
  output backpressure, full/empty rings.
- Lost device, failed stream start, control changes while active,
  cancellation in startup and steady state.
- Channel/rate mismatch, application I/O exceptions, unsafe levels,
  sudden graph-run failure.
- Parameter automation near limits, feedback-loop stability, finite
  playback tail, bypass/crossfade transitions.

No normal CI test may open the microphone or play audio. Add a
deterministic fake-duplex backend for CI and a separate **opt-in**
virtual PipeWire/PortAudio test and physical loopback qualification.

## Operator safety and hearing protection

Start live tests muted or at low output gain, avoid routing a live
microphone to loudspeakers (acoustic feedback), and select explicit
headphone/monitor outputs. An electric guitar should enter a suitable
high-impedance instrument input, not a microphone or ordinary line
input chosen by assumption. Provide explicit device selection, gain
clamping, channel mapping, configurable bypass/mute, and immediate
shutdown on a terminal error. **No claim of a brick-wall safety limiter**
without implementing and validating peak behavior.

## Proposed acceptance targets

First prove a bounded **functional** identity loop with exact samples
and fault reporting, then measure local physical latency. The under-10-ms
goal can only be marked met for tested devices where repeated physical
measurements show it, with measured p95/p99/tail and dropouts disclosed.
Optimization priorities should be driven by these measurements. Optional
SCHED_FIFO, memory locking or CPU affinity may be investigated with
permissions/security implications; none is required or presumed.
