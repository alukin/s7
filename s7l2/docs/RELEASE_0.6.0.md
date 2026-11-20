# S7 0.6.0 — preliminary pre-GA ARM deployment beta

**Status: planning (2026-11-20), not implemented.**
The maintainer's target is a real compiled S7 application on Linux-based
SDR or a Raspberry Pi with microphones. This is a **pre-GA beta**, not
General Availability. See the [roadmap](RELEASE_ROADMAP.md).

## Principal acceptance scenario

1. Design an actual **local** S7 DSP/controller diagram.
2. Verify its saved graph against synthetic and recorded input through
   `NodeRuntime`, with known parameters and expected outputs.
3. Compile with 0.4.0 code generation, producing readable optimized C++
   and a native ARM Linux executable/model component.
4. Build on-target or cross-compile using a **documented, reproducible**
   toolchain for the named ARM ABI; deploy and start headlessly,
   without Studio or the general-purpose dynamic graph executor.
5. Connect qualified 0.3.0 hardware adapter(s): microphone/USB-I2S
   audio or live SDR IQ; process **real physical samples**.
6. Replay the captured real input through the reference and native
   model, compare outputs, and measure resource and failure behavior.

**Minimum beta gate:** one complete, evidence-backed real ARM Linux
profile, preferably **Raspberry Pi plus microphones**. Qualify a
**LibreSDR/Zynq-7020 Linux SDR** independently as a strongly preferred
second profile; it has distinct ARMv7, device driver, memory and
throughput constraints. No claim of support just because the source
cross-compiles. Optional Orange Pi comparison can follow.

## Candidate beta gates

| ID | Requirement | Evidence |
| --- | --- | --- |
| **B0** | Traceable native build | Diagram hash, generated source, compiler version/flags, target ABI, build/install commands |
| **B1** | Real hardware integration | Exact qualified device, actual stream settings, bounded adapter/host handoff, sample output |
| **B2** | Numerical parity | Captured real input replayed through native and dynamic S7 paths; documented tolerances |
| **B3** | Resources and latency | CPU, peak RSS, binary size, throughput, FIFO/queue bounds, xruns/gaps and measured delays |
| **B4** | Faults and recovery | Missing device, wrong rate, disconnect, overflow, stop/reset and repeatable recovery; output/TX opt-in |
| **B5** | Deployment usability | Small documented dependencies, headless logs/configuration and third-party reproduction instructions |

The execution host handles device clocking, activation, shutdown,
backpressure and physical effects. The generated processing model
contains no Qt GUI, general-purpose schema parser, runtime element
registry or mandatory dynamic graph traversal.

## 1.0.0 outlook

**0.6.0 demonstrates a credible integrated beta**, not universal device
support, production safety certification or hard real-time guarantees.
For **1.0.0 General Availability**, require a documented compatibility
policy, validated supported-platform matrix, stable APIs/file/protocol
commitments, reproducible packages, extensive numerical/performance/
reliability/security testing, long-running hardware and failure tests,
user guides and an explicit GA release decision. More 0.x stabilization
releases may be needed; no fixed 1.0 date is assumed.
