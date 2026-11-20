# MIT-aligned Audio DSP and Low-Latency Sound Effects in S7

**Status: planning proposal, 2026-11-20. Zero AU labs have been implemented,
audibly demonstrated or qualified under this track.**

This is an **original S7 educational/engineering course**, not an official MIT
course, a reproduction of MIT assignments, or an MIT-endorsed project.
Its primary teaching reference is [MIT 6.341 Discrete-Time Signal Processing,
Fall 2005](https://ocw.mit.edu/courses/6-341-discrete-time-signal-processing-fall-2005/),
taught by Alan V. Oppenheim. The published lecture sequence covers all-pass
systems, fractional delay, sample-rate conversion, quantization, FIR/IIR
structures, FFT/DFT filtering, and spectral analysis. Some sound-effects and
real-time engineering labs are **S7-original extensions**, not specific MIT
experiments. [SOURCES.md](SOURCES.md) records the exact alignments.

## What students will actually do

Plug in a microphone, replay WAV audio, or connect an electric guitar through a
proper **Hi-Z instrument input on an audio interface**, then construct an
audible DSP chain in S7 and listen to the output through headphones or
properly routed monitors. These are **signal-processing experiments**, not
music-performance, melody, beat tracking or composition classes.

**Primary engineering goal:** S7 should become useful for **low-latency,
device-paced, bidirectional audio processing**, while preserving the same
causal model and numerical correctness as offline simulations.

## Course size and modes

The course contains **12 planned labs (AU01–AU12)**, from WAV playback and
mic-through to filters, delay, distortion, dynamics, time modulation and a
guitar-pedalboard capstone. See [COURSE_MAP.md](COURSE_MAP.md).

Every applicable lab has separate qualification profiles:

- **SIM**: deterministic signal generation, exact saved S7 diagram and an
  independently implemented numerical oracle.
- **FILE**: licensed/provenance-checked WAV or synthetic PCM in a reproducible,
  offline saved-diagram workflow; output and alignment measured.
- **LIVE**: explicit user-enabled input -> S7 graph -> output through native
  PipeWire or PortAudio; latency/dropout metrics and audible verification,
  not a substitute for numeric tests.

The core signal path should remain conceptually identical between offline
and live modes. The audio device boundary, not a hidden effect component,
owns clocked pacing, device state and transport.

## What currently exists and what does not

The public `s7audio` module has independently tested PortAudio and native
PipeWire **capture** and **playback** backends, `AudioSource`/`AudioSink`,
multi-channel F32/I16/I32 sample contracts and explicit opt-in hardware
activation. [Existing documentation](../../docs/HARDWARE_IO.md) states that
the current runner admits **one hardware boundary and one exact rate domain**
per run. It cannot yet execute the desired full-duplex
`AudioSource -> S7 effects -> AudioSink` graph in one coordinated run.

The [2026-11-16 live tests](../../docs/HARDWARE_IO_LIVE_TESTS.md)
successfully tested standalone I/O, virtual routes and failure injection,
but **did not** validate low-latency physical DAC-to-ADC loopback.
A 394.29 ms *software* two-host estimate reported there includes network
and buffering and **must not** be represented as S7 local guitar latency.
Current output completion aborts pending audio, rather than assuring a
drained final tail.

Those limitations are reasons to improve S7, not reasons to abandon the
course. See [REALTIME_PROFILE.md](REALTIME_PROFILE.md) and
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for an architectural-first,
regression-tested plan.

## Success criteria

The first key milestone is **mic/guitar input -> identity S7 graph -> output**,
with an independent physical loopback measurement and repeatable dropout,
latency distribution and CPU reporting. **10 ms measured input-to-output
latency at 48 kHz is an initial target, not a guaranteed design limit**;
report the actual tested backend, buffer size, device, OS and load.

Only after a credible no-effects baseline do we implement a selectable chain
of gain, EQ, echo, compressor, chorus and overdrive.

Inherit the existing
[S7 educational verification standard](../LAB_STANDARD.md):
Predict -> Construct -> Simulate -> Measure -> Verify -> Explain -> Extend.
No lab may be called `headless-verified` because a filter sounded good;
no lab may be called `live-qualified` because a numerical test passed.
The [verification matrix](VERIFICATION_MATRIX.md) states both requirements.

## Links

- [COURSE_MAP.md](COURSE_MAP.md) — 12 labs, explicit MIT references, oracles.
- [SOURCES.md](SOURCES.md) — official MIT lecture links and original-extension labels.
- [REALTIME_PROFILE.md](REALTIME_PROFILE.md) — realtime audio contract, latency methods.
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — first milestone and staged engineering handoff.
- [VERIFICATION_MATRIX.md](VERIFICATION_MATRIX.md) — lab and runtime acceptance gates.

**Boundaries:** No physical acoustics, microphone-array source localization or
music analysis in this track. These can become separate research curricula
later. This course is distinct from `mit-ee` and `mit-sdr` and does not
alter the separate 0.2.0 release commitments. By the maintainer's
2026-11-20 decision, **all AU01–AU12 are mandatory 0.3.0 targets**;
see the [release work plan](../../docs/RELEASE_0.3.0_WORKPLAN.md).
