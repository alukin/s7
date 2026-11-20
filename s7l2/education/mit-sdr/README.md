# S7 SDR and Digital Communications — MIT-aligned laboratory track

**Status: 2026-11-20 planning proposal. The 24 SDR labs are not yet implemented,
numerically verified, hardware-qualified or release-qualified.** This is an
independent S7 teaching track, **not an MIT course, official MIT laboratory,
or MIT-endorsed software**.

## Scope and audience

Audience: senior undergraduate students, PhD students and practicing
electronics, communication and software engineers. Expected prerequisites:
complex numbers, sampling, Fourier/DFT, FIR filtering and basic probability.
Provide short refresher links to the existing S7
[electrical-engineering labs](../mit-ee/README.md); advanced notes cover estimation,
synchronization, OFDM and device timing.

**24 original S7 labs** follow MIT subject topics:
- [6.02 Digital Communication Systems (Fall 2012)](https://ocw.mit.edu/courses/6-02-introduction-to-eecs-ii-digital-communication-systems-fall-2012/): digital signaling, noise, channels, filters, modulation and coding.
- [6.450 Principles of Digital Communications I (Fall 2006)](https://ocw.mit.edu/courses/6-450-principles-of-digital-communications-i-fall-2006/): signal spaces, constellations, random processes and detection.
- [6.973 Communication System Design (Spring 2006)](https://ocw.mit.edu/courses/6-973-communication-system-design-spring-2006/): multitone systems, 802.11a architecture, synchronization, channel estimation and equalization.
- [6.S965 Digital Systems Laboratory II (Fall 2025)](https://rfsoc.mit.edu/6S965/F25/assignments): selected RF reception/FIR/ADS-B *topics*, not an FPGA prerequisite. The current MIT catalog also lists 6.6040 Digital Systems Laboratory II; do not imply the 2025 labs are a 2026/2027 6.6040 assignment.

Supplementary readings (especially practical demonstrations) include Analog
Devices' *Software-Defined Radio for Engineers*, ADALM-PLUTO/AD936x technical
docs, PySDR and Harvey Mudd College's *Learn SDR* series. See
[SOURCES.md](SOURCES.md) for source editions, precise links, rights and
attribution rules. Every delivered lab must cite a verified section or
chapter, and distinguish **MIT-topic alignment** from **exact reproduction**.
Exercises, diagrams and reference implementations must be original.

## Execution and verification: the labs are also S7 conformance tests

Each laboratory targets three independently qualified paths when applicable:

1. **SIM**: offline synthetic signals, mathematically specified impairments,
   fully deterministic where possible; execute the *saved* S7 graph headlessly.
2. **IQ**: checked-in small synthetic fixtures or licensed/redistributable
   recorded complex-I/Q fixtures. Preserve capture metadata and discontinuity
   flags; compare identical processing chains with simulations.
3. **RF**: optional, opt-in real-device experiments, with independent settings,
   measured samples, environmental conditions and radio-specific acceptance.
   RX-only, attenuated coaxial TX/RX and compliant over-the-air TX/RX are
   separate profiles and evidence categories.

The same **logical signal-processing graph** should be exercised across
profiles where possible, using clearly defined file/hardware source adapters.
Do not conflate nominal sample rate, physical sample clock, hardware time,
host arrival time or model time; do not hide sample losses. A pretty
constellation or waterfall is not evidence of correct decoding.

Every lab inherits [LAB_STANDARD.md](../LAB_STANDARD.md):
Predict -> Construct -> Simulate -> Measure -> Verify -> Explain -> Extend.
Require an independently implemented oracle, exact saved graph replay,
negative/failure experiment, Studio observer validation and recorded evidence.
RF results additionally require a documented device-and-fixture profile and
repeatability criteria. See [HARDWARE_PROFILES.md](HARDWARE_PROFILES.md) and
[VERIFICATION_MATRIX.md](VERIFICATION_MATRIX.md).

## Available hardware and planned development

Maintainer's test equipment: one **ADALM-PLUTO Rev. B** and **two LibreSDR
(Zynq-7020/AD936x-family)** devices. The S7 public SoapySDR hardware
implementation already exists. **This does not establish** that all three
devices or their possible RX/TX channels support every setting, streaming
mode, simultaneous session or synchronized capture.

The current [hardware I/O documentation](../../docs/HARDWARE_IO.md) allows one
hardware boundary and one exact rate domain per run, with explicit activation
and no general duplex/shared-session graph or clock alignment; the
[live-test record](../../docs/HARDWARE_IO_LIVE_TESTS.md) reports bounded
throughput and timestamp limitations. These are baseline constraints, **not
permanent exclusions**: missing capabilities may be developed as part of this
teaching program, each through a separate design/implementation/verification
gate. **No FPGA programming in this curriculum phase.**

## Directory and workflow

- [COURSE_MAP.md](COURSE_MAP.md) — 24 lab IDs, MIT references, execution modes,
  first independent oracles and expected capability work.
- [SOURCES.md](SOURCES.md) — primary and supplementary teaching sources.
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — staged engineering tasks
  with go/no-go and acceptance boundaries.
- [HARDWARE_PROFILES.md](HARDWARE_PROFILES.md) — device inventory, RF safety,
  regulatory constraints and capture metadata.
- [VERIFICATION_MATRIX.md](VERIFICATION_MATRIX.md) — numeric, backend, Studio
  and physical qualifications; no unearned completion claims.

When implementation starts, add one independently authored diagram and guide
under `education/sdr/labs/SDRxx-slug/` for each delivered lab. Do **not**
create 24 empty directories or claim that existing DSP diagrams are complete
SDR labs. Reuse existing libraries, generators and test infrastructure rather
than copying old examples verbatim. A lab may deliberately compare two
independently configured processing diagrams if its mathematical objective
requires it. Source and test evidence must be pinned to a commit.

## Release boundary

This track is **separate from the already agreed 0.2.0 core educational
release gate**. No automatic addition to 0.2.0 or implementation count.
The first coherent teaching module can be released independently, after its
specific numerical, GUI and (where advertised) RF gates pass.
