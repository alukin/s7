# MIT-aligned SDR track — staged implementation and S7 verification plan

**Approved educational scope: 2026-11-20.** Target: senior students, PhD
students and practicing engineers; **24 labs** mapped to published MIT
teaching topics. Hardware: maintainer's PlutoSDR Rev. B plus two LibreSDR
boards; SIM, recorded I/Q, RX and regulated TX over coax or eligible
over-the-air links. FPGA programming is **out of scope for now**.

This is a **plan, not a report of executed tests**. A major objective,
besides teaching communications, is **verification and improvement of S7
itself**. Missing capabilities are work items, not automatic reasons to
cancel labs. Follow [S7 lab contract](../LAB_STANDARD.md),
[COURSE_MAP.md](COURSE_MAP.md), [SOURCES.md](SOURCES.md),
[VERIFICATION_MATRIX.md](VERIFICATION_MATRIX.md) and
[HARDWARE_PROFILES.md](HARDWARE_PROFILES.md).

## Organization and compatibility

Separate sibling tracks under `s7l2/education`:
`mit-ee/` for the existing MIT-aligned electrical-engineering curriculum and
`mit-sdr/` for this new SDR curriculum. Keep only shared standards and
the cross-track index at the parent. The current `education/labs`,
`education/generators` and associated links/tests are still used by the
published EE materials and must be **migrated by a separate tested
refactoring** before removing old paths; do not merge a half-migrated
course layout. Preserve Studio links, package `share/s7/education` links,
CMake test paths and generated diagram help links. Make the directory
migration its own reviewable change when possible.

## Responsibilities, constraints and PR boundaries

- **Maintainer**: priorities, architecture decisions, legal operating
  envelopes, physical fixture approval, final student workflow and
  qualification review.
- **Coding agents**: inventory actual S7 APIs, propose small reviewable
  additions, implement new saved diagrams and independent test oracles,
  execute reproducible checks and report failures precisely.
- **Architectural integrity**: don't bypass causality, shared-memory
  ownership, model time, exact rate contracts, scalar complex-port
  conventions, or observed/transmitted prefix reporting. No hidden
  compute inside view/observer code.
- Each PR should group 1–3 related original labs **or** one prerequisite
  capability with focused tests and migration notes. Avoid a 24-lab
  mega-PR, and don't equate `build succeeded` with `lab verified`.
- Keep the 0.2.0 EE educational gate separate. SDR implementation does
  not automatically enlarge the existing release scope.

## Phase 0 — inventory, source and hardware baseline

1. Review `AGENTS.md`, current EE lab architecture, source contracts
   (hardware, complex type, step and sampling semantics), actual Soapy
   provider code, Studio observer behavior, and available generators/tests.
2. Inspect *each* physical radio's Soapy/libiio device and firmware
   capability record: RX/TX channels, sample formats, supported rates and
   bandwidths, gain/readback, stable I/O, cancellation, timestamps.
   Preserve radio-specific identity and RF parameters in private local
   notes; publish only non-sensitive profiles.
3. Freeze the teaching corpus and independent source ledger: MIT 6.02,
   6.450, 6.973 and 2025 6.S965 as **separate, dated editions**, with
   companion ADI, PySDR and Harvey Mudd sources. No unverified chapter
   numbers, lecture IDs or assignment reproductions.
4. Determine a small reproducible I/Q fixture format. Define type,
   endianness, full-scale interpretation, sample count/rate, stationarity,
   gap map, timestamp origin and test-data license. Prefer synthetic
   fixtures; recorded real data requires separate provenance and approval.
5. Obtain and document antenna/cable/attenuator and safe-output limits
   before ever activating transmitter tests. Check specific legal
   conditions for any proposed OTA experiment.
6. Reproduce the public baseline **without RF autoactivation**. Record
   exact SHA/toolchain/commands; isolate known limitations from new defects.

Exit: traceable per-lab inventory, safe bench SOP, link audit and prioritized
capability/test gaps. No live RF required for this phase.

## Phase 1 — mathematical receive pipeline (SDR01–SDR07)

- Create **purpose-built** `.s7diagram` files and student guides for
  I/Q, sampling, calibrated FFT/PSD, receiver artifacts, frequency
  translation, multirate FIR and AGC.
- Build independent scalar or short direct-DFT reference implementations;
  compare exact early samples, phase and magnitude, indices, units and
  negative variants. Distinguish finite-record effects and approximations.
- Where core S7 lacks a required runtime element, implement it with
  explicit state/rate contracts and unit tests *before* declaring the
  associated diagram qualified.
- Introduce a reusable record/replay adapter or input fixture if absent.
  Preserve exact complex-value semantics and explicit sample continuity;
  test short files, EOF, gaps, bad metadata and unsupported rates.
- Qualify Studio views against recorded values, not just screenshot shapes.
- Add RX-only Pluto and LibreSDR variants after independent receiver
  device profiles are measured; they are not prerequisites for offline
  course correctness.

Exit: SDR01–SDR07 each with independent deterministic tests and actual
Studio workflow; at least one bounded real I/Q capture as optional
device qualification where practicable.

## Phase 2 — modulation, symbol decisions and statistics (SDR08–SDR15)

- Implement AM, FM, OOK/ASK, FSK, BPSK, QPSK/QAM, pulse shaping and BER
  as original S7 diagrams. Do not hide a complete modem inside one opaque
  element when explicit construction teaches the subject.
- Build golden symbol generators and reference receivers outside S7.
  Freeze constellation phase, bit ordering, energy normalization and
  filter latency. Inject deliberate frequency/phase and timing errors.
- For stochastic tests, fix RNG identity and seeds for reproducibility,
  **also** run independent multi-seed trials and confidence intervals;
  compare BER/SER vs mathematical theory within justified bounds.
- Qualify a physically safe, attenuated coaxial TX→RX procedure with
  explicit opt-in, finite run length and stop-on-error. Where S7 cannot
  drive RX and TX simultaneously, specify and implement a bounded
  duplex/shared-session or coordinated two-radio mode, with failure
  injection and no implicit graph-rate changes.
- OTA is an **optional test extension only** with measured spectrum,
  specific lawful setup and operator approval. Never require OTA for CI
  or grade completion.

Exit: SDR08–SDR15 with numeric/stochastic oracles, viewer checks and
documented optional coax profiles. Any missing duplex capability has its
own accepted tests before hardware teaching claims.

## Phase 3 — synchronization and complete receiver (SDR16–SDR21)

- Carrier FLL/PLL/Costas, timing recovery, correlation-based frame
  detection, equalization, OFDM and pilot tracking.
- Prioritize explicit *stateful* receiver components (loop-filter state,
  phase accumulator, interpolator, frame state); preserve S7 causal
  behavior. Add block/frame adaptation only through an reviewed
  boundary contract: do not smuggle variable-sized frames into scalar
  graph ports.
- Independently derive known-impairment synthetic fixtures; test
  acquisition range, phase slips, timing drift, false alarms, ill
  conditioning and short CP. Verify exact finite-N IFFT/FFT with a
  separate direct DFT, not just FFT-vs-FFT.
- Compare with recorded I/Q and optionally coax hardware. Record
  frame-level failure, sample gaps, underrun/overrun and recovered data.

Exit: quantitative synchronization and OFDM tests with documented
limitations, not merely a plausible constellation.

## Phase 4 — demonstration/engineering research (SDR22–SDR24)

- **SDR22**: a safe end-to-end physical link. Separate exact simulated
  oracle from cable/RF measurements, state accepted output and actual
  receiver input, obtain waveform/power measurements, and verify TX
  cancellation/stop. Optional OTA only when lawful.
- **SDR23**: ADS-B 1090 MHz as a flagship receiver. Begin with synthetic
  known valid and corrupted message fixtures and independent parity/pulse
  checks; then recorded I/Q; finally an RX-only Pluto/LibreSDR antenna
  experiment. Link the MIT 2025 RFSoC lab, but write an **original S7
  implementation**, not a copied RFSoC assignment.
- **SDR24**: independent SDR clock error, offset, jitter and coherence.
  Use two *generated* streams with precise injected offsets; then
  independent physical radios with known cable/reference conditions.
  Distinguish true hardware timestamps from delivered-frame indexes,
  and timestamp-constrained inference from mere correlation.

Exit: one end-to-end radio demonstration with reproducible data, a
verified ADS-B decoder, and an honest bounded multi-radio clock report.
The advanced hardware experiment may be marked blocked with explicit
capability gaps until measurements permit it.

## S7 capability candidate backlog (inspect before implementing)

| ID | Candidate need | Existing known baseline or trigger | Required acceptance |
| --- | --- | --- | --- |
| C1 | Reproducible I/Q record/replay plus metadata | S7 Soapy provider exists; recordings as interchangeable hardware owners not yet established | Same graph output on deterministic file and synthetic input; EOF/truncation/gap failure cases |
| C2 | Reusable DSP primitives (NCO/mixer, AGC, RRC, EVM, slicers) | Exact inventory needed; write only missing composable parts | Stateful recurrence, type/rate tests, independent math and Studio documentation |
| C3 | Explicit bounded rational-rate conversion/decimation | Current public hardware execution starts with one rate domain | Reference polyphase filters, transient/tail, known alias and rejected rate mismatch |
| C4 | Frame/message handling for modem/OFDM/ADS-B | Scalar frame ports and explicit graph steps are initial contract | Lifecycle, exact sample indices and boundary metadata, no silent loss or implicit graph delay |
| C5 | Duplex and/or two independent radio hardware owners | Current hardware run admits one boundary | Bounded queues, start/stop, cancellation, partial transfer, concurrency safety, actual rate and error reports |
| C6 | Clock/timestamp provenance and multi-device synchronization | Driver-local time/unknown epoch, no global alignment guarantee | Artificial drift tests, real device calibration and uncertainty envelopes; never infer UTC |
| C7 | Studio RF workflow, constellation, waterfall and package links | Viewer capabilities exist but require lab-specific verification | Open/run/edit/rerun/view against recorded samples and relocated package; RF opt-in gate |
| C8 | On-air emission/profile guardrails | Not a universal Soapy capability; legal profile is external | Off-by-default, capped duration/power under operator-approved envelope and tested safe stop |

**Defect workflow**: record failing minimal S7 graph and mathematical
oracle -> file/design discussion if formal semantics affected -> add
regression -> implement fix/capability -> run focused tests -> rerun all
affected saved diagrams and independent observers -> update evidence.
Do not fix a physics-vs-code discrepancy by weakening the reference.

## Delivery and review protocol

1. After Phase 0, begin with **SDR01–SDR03** as a thin, valuable vertical
   slice (saved diagram, source ledger, headless oracle, Studio, fixture).
2. Continue SDR04–SDR07 and reuse the now-proven infrastructure.
3. Use small PRs for capability work when semantics are affected.
4. Advance lab status only with evidence at a pinned commit and
   platform. Log tests actually run and explicitly label untested
   physical configurations.
5. Qualify the installed Linux course and independently review Studio
   rendering/axes/viewers. Do not advertise Windows SDR capability if
   that platform profile disables `S7_BUILD_SDR`.
6. Qualify **all SDR01–SDR24 as mandatory 0.3.0 release targets**
   under the [0.3.0 work plan](../../docs/RELEASE_0.3.0_WORKPLAN.md),
   while keeping the 0.2.0 MIT-EE gates independent. Hardware-required
   modes must have actual opt-in evidence rather than simulated substitutes.

## Ordered first handoff for Codex

**Documentation/architecture first.** Review the whole
`s7l2/education/mit-sdr/` set, `AGENTS.md`,
`s7l2/education/LAB_STANDARD.md`, the actual Soapy and public
hardware source/tests, and the existing MIT-EE examples. Produce a
matrix of implemented vs missing capabilities with precise file/API
references. Record and resolve any contradictions with current code.
Do not assume a planned lab exists. Implement no RF transmission
automatically. Then propose the first narrowly scoped SDR01–03 PR with
independent golden references and exact commands needed for acceptance.
