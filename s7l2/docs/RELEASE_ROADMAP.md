# S7 release roadmap — 0.3.0 through 1.0.0

**Maintainer direction: 2026-11-20.** Planning, not an assertion of
implemented features or passed release gates.

**Product vision:** S7 is a design and verification environment where the
saved diagram is the authoritative specification. S7's dynamic reference
runtime executes the design; a future native compiler produces efficient
software with equivalent observable causal/numerical semantics. Hardware
adapters bridge processing models to real sensors/outputs, independently
of the execution backend.

| Version | Purpose | Principal outcome | Status |
| --- | --- | --- | --- |
| **0.3.0** | **Bridge to reality** | 24 SDR labs, 12 audio labs, four video smoke tests, mandatory GStreamer audio/video, device pacing, and H8 provider/model/host separation plus deterministic capture/replay | Agreed H0–H8 gates; final qualification open |
| **0.4.0** | **Compile the model** | Local diagram -> validated/typed causal IR -> efficient C++ source and native executable; differential verification against S7 reference runtime | Preliminary |
| **0.5.0** | **Distributed coherence** | Explicit physical clock/coherence relationships, timing uncertainty, multi-domain signal processing and distributed sensing experiments | Preliminary |
| **0.6.0** | **Pre-GA integration beta** | Compile, build, deploy and run a real S7 DSP model on named ARM Linux hardware with microphone or SDR input | Preliminary |
| **1.0.0** | **General Availability** | Documented supported profiles, compatibility policy, reproducible artifacts and evidence-backed reliability/security/performance qualification | Future GA decision |

## Architectural dependencies

- **0.3.0** makes device interfaces trustworthy and independent of the
  current dynamic graph executor: provider owns I/O, causal model owns
  signal processing, host owns lifecycle/pacing/physical effects. Captured
  data can be replayed through the same model with independently checked
  outputs. No C++ source compiler is needed to pass 0.3.0.
- **0.4.0** makes the diagram executable without the general-purpose
  graph engine: an explicitly supported local model subset is compiled
  into readable, optimized C++. Generated code retains numerical and
  causal equivalence; physical device interfaces remain separate.
- **0.5.0** adds distributed coherence beyond nominal equal-rate clocks.
  Source-time provenance, drift, delay and uncertainty must be explicit.
  It need not compile a whole distributed graph into one executable.
- **0.6.0** integrates 0.3 hardware boundaries and 0.4 compiled models
  into a **real ARM Linux application**. Broad 0.5 coherence support is
  valuable but not a hidden prerequisite for a local physical target.
- **1.0.0** is contingent on explicit release qualification rather than
  dates or feature checklists alone. Further 0.x stabilization releases
  remain possible.

## 0.6.0 flagship acceptance demonstration

Use an actual saved S7 diagram, verify the model by replaying known
recorded inputs, generate native C++, build it for a named ARM Linux
target, deploy it without Studio or the general-purpose dynamic graph
loader, and process real microphone/SDR data via a qualified hardware
adapter. Compare retained captured input through the compiled model
and S7 reference execution. Record CPU, RAM, binary size, data rate,
latency/deadlines, queue occupancy, gaps, failure/restart and limitations.

**At least one physical ARM Linux profile is mandatory:** preferably
Raspberry Pi with qualified microphone hardware. A Linux LibreSDR
(Zynq-7020/ARMv7) with real IQ input is an important second target,
but requires separate driver, ABI and runtime qualification—compilation
for Raspberry Pi does not imply SDR device support. A second platform
may be added only with truthful evidence.

## Release naming

**Beta is not General Availability.** 0.6.0 is a **pre-GA beta** (with
optional `v0.6.0-beta.N` preview tags). `v1.0.0` is GA only after
supported-platform, reproducible-build, API/format, security, stability
and documentation gates have passed. No 1.0 date is promised.

See the [0.3.0 work plan](RELEASE_0.3.0_WORKPLAN.md),
[0.4.0 preliminary notes](RELEASE_0.4.0.md),
[0.5.0 preliminary notes](RELEASE_0.5.0.md), and
[0.6.0 preliminary notes](RELEASE_0.6.0.md).
