# S7 0.3.0 work plan — hardware-oriented correctness and timing

**Scope decision, 2026-11-20 (planning; implementation not claimed):**
The maintainer selected **all 24 MIT-SDR and all 12 MIT-Audio labs** as
mandatory 0.3.0 release targets. Add a **small, preliminary video smoke suite**
of four named cases, not a full video education curriculum. Add
**production GStreamer appsrc/appsink integration for both audio and video**
as a separate mandatory release gate, not just a video helper. The release is
about qualifying S7's hardware-facing causal runtime, not accumulating demos.

**Additional decision, 2026-11-20:** mandatory H8 covers separation of
device adapters, causal processing models and execution hosts, with
deterministic capture/replay for audio, IQ and whole-frame video. This
prepares the native compiler without requiring it in 0.3.0.
H0–H7 remain unchanged.

This plan owns the 0.3.0 **qualification matrix**, subject to the formal
[release contract](RELEASE_0.3.0.md). Preserve the
[0.2.0 release gates](RELEASE_0.2.0_WORKPLAN.md) and don't retrospectively
mark their tests passed. Completed 0.2.0 content is a regression baseline.
The ongoing 0.3.0 feature/backlog research remains in RELEASE_0.3.0.md.

## Objective

> Complete and independently qualify **24 SDR + 12 audio teaching labs**,
> developing the required device-paced, multi-boundary, timing-aware,
> buffer/frame and DSP capabilities in S7. Demonstrate a real microphone
> or guitar through an effects graph and qualified RX/TX radio paths.
> Introduce a bounded, original four-test whole-frame video smoke suite.
> Deliver production S7 GStreamer audio AND video processing chains with
> independently verified outputs, explicit buffers/clocks and opt-in hardware.

A new lab is accepted only when its **actual saved S7 diagram** runs using
the production execution path; an independent reference oracle, negative
case, source attribution, Studio workflow and package links are verified.
Separately qualify physical hardware where the lab specifies RX/TX/audio
LIVE. Do not require live RF/physical audio hardware in normal CI.

## Gate matrix (none of these gates is yet closed)

| ID | Mandatory 0.3.0 gate | Scope | Required evidence |
| --- | --- | --- | --- |
| **H0** | Device-paced causal execution | Reviewed single-device full-duplex audio, explicit hardware owners, honest rate/clock handling, queues and stop/drain | Fake-device graph conformance, deterministic regression of state/delay/rollback, failures, opt-in device runs |
| **H1** | MIT-SDR complete lab collection | **SDR01–SDR24** exactly; follow [course map](../education/mit-sdr/COURSE_MAP.md) | 24 individual canonical diagrams/guides, source-mapped oracles, recorded-IQ fixtures, headless and Studio checks, qualified RF subset |
| **H2** | MIT-Audio complete lab collection | **AU01–AU12** exactly; follow [course map](../education/mit-audio/COURSE_MAP.md) | 12 canonical diagrams/guides, independent audio DSP math, synthetic/WAV samples, headless/Studio checks, live duplex and effect measurements |
| **H3** | Physical low-latency audio | A named same-clock 48 kHz interface, microphone/Hi-Z instrument -> S7 -> headphones; bypass and selected effects | Analog loopback latency distribution, graph service time, FIFO state, dropout/underrun/overrun count, CPU and latency under load; **10 ms initial target is to be measured, not presumed** |
| **H4** | Physical SDR verification | Maintainer's Pluto Rev. B and two LibreSDR; RX and safe attenuated coax TX/RX, optional legal OTA | Device-discovery/readback, accepted/actual frame/rate data, independent signal/constellation/error measurements, cancellation, continuity, per-device limits |
| **H5** | Preliminary whole-frame video | **VID01–VID04** below; bounded whole-frame pipeline and media adapter bridge | Exact synthetic frame oracles, real camera opt-in smoke, metadata/drop/EOS failures, package/Studio view where advertised |
| **H6** | **Production GStreamer audio AND video** | Bounded, opt-in actual S7 audio and whole-frame video processing through GStreamer; [contract](RELEASE_0.3.0_GSTREAMER.md) | GS-A1/GS-A2, GS-V1/GS-V2 and GS-X; independent math, real audio latency/camera tests, caps, timestamps, lifecycle, error paths |
| **H8** | **Backend-neutral model and capture/replay** | Separate provider/model/host boundaries and same-diagram replay/live audio, IQ/SDR and video | BR-A/BR-S/BR-V independent numerical checks, type/rate/sequence/timing provenance and explicit no-device/fault tests; no generated C++ required |
| **H7** | Release candidate and interoperability | All above on pinned candidate SHA, Linux baseline, independent platform profiles as implemented | Installed/relocated package, no accidental device opening, full C++ numeric regressions, negative/interrupt tests, exact commands and evidence ledger |

**Release decision:** a blocked named lab or unqualified mandatory gate blocks
0.3.0 until fixed. A later explicit maintainer decision may revise the
scope; do **not** silently count a synthetic simulation as a physical TX,
DAC output as actual presentation, a code review as a passing test,
or a video tool spike as a shipped S7 element.

## Four preliminary video tests

There is intentionally no new `mit-video` teaching curriculum in 0.3.0.
Place video fixtures/tests near the eventual video module and keep the
evidence together. The four test IDs are **verification cases, not student
labs**:

| Case | Input / processing | Independent check and negative case |
| --- | --- | --- |
| **VID01 — frame fidelity** | Small synthetic RGB/YUV frames (odd width and nontrivial stride) -> explicit format conversion -> grayscale/ROI | Independent pixel math, layout/stride/color order, exact raw/lossless output; reject invalid dimensions/stride/format |
| **VID02 — frame lifecycle** | Finite sequence of synthetic whole frames with IDs, timestamps and bounded buffering | Exact sequence, bounded memory, EOS/stop/drain and explicitly reported gap/backpressure; inject truncation, rate mismatch and consumer stall |
| **VID03 — camera to observable output** | Opt-in real webcam -> headless S7 frame source -> bounded processing -> output/viewer, using one **qualified local** platform/profile | Verified actual captured formats and dimensions, counted frame continuity, measured FPS/latency and recovery; disconnect/permissions failure; no camera access in CI |
| **VID04 — media adapter bridge** | A fixed synthetic/recorded video fixture through one selected **GStreamer appsrc/appsink** profile and independently compared FFmpeg or raw reference | Exact raw/lossless reconstruction, bounded perceptual-codec error metric where lossy, visible actual conversion, EOS/cancel/plugin unavailable, no silently selected fallback |

The 2026-11-23 [array-signals 0.2.0 amendment](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#5-mandatory-020-minimal-vertical-slice-new-scope)
places local array-valued ports/wires, a basic Waterfall and synthetic/recorded
U8 Video Player under the 0.2.0 qualification gates. 0.3.0 **reuses and
extends** those capabilities: real VideoSource/VideoSink, cameras, codec/media
pipeline adapters, full audio blocks and additional frame transforms still
need independent H0–H8 qualification. Do not count 0.2.0 viewer tests as
GS-V1/GS-V2, VID03 or real camera/codec qualification.

The previous [Full HD FFmpeg report](VIDEO_IO_FULL_HD.md) is supporting
media-pipeline evidence only. Native S7 video-frame ports,
`VideoSource`/`VideoSink` and the `GStreamer` adapter have **not**
been qualified by that experiment. H5's default target is a correct
small/medium frame path. Full HD performance and remote display may be
additional experiments, **not** prerequisites unless selected as the
named accepted video profile.

## H8 bridge qualification

Three profiles must be independently accepted against the **same saved
causal processing diagram** for recorded and opt-in live sources:

| Profile | Replayed input and live adapter | Evidence |
| --- | --- | --- |
| **BR-A** | Synthetic or captured F32 audio, plus one microphone/Hi-Z/line backend | Independent output samples, channel layout, initial state, nominal rate, gap/segment and optional device timing evidence |
| **BR-S** | Recorded complex-IQ fixtures plus a qualified SDR RX backend | Sample scaling, order, selected settings/readback, position/continuity, known vs. unknown timestamp quality |
| **BR-V** | Raw/lossless frame fixture plus selected GStreamer webcam profile | Exact pixel/stride/color/sequence math, EOS/drop/backpressure, physical FPS and latency separately measured |

Contract and exit criteria:

1. Publish an explicit **device adapter / processing model / execution
   host** responsibility table and small stable typed interface. The model
   must not embed PortAudio, PipeWire, SoapySDR or GStreamer driver logic.
2. Preserve source-sample/frame positions, format/rate, stream/segment ID,
   actual clock domain and acquisition-versus-arrival timestamp meaning
   where available. **Never manufacture timing, perfect continuity or loss
   counts** for absent backend evidence.
3. Compare deterministic replay outputs with an independent oracle and
   verify the saved graph and state initialization are the same when connected
   to physical I/O. Treat live jitter and deadlines as separate measurements.
4. Validate stop/drain, mismatch, partial blocks, overflow, device loss and
   failure paths. Replay, metadata inspection and schema validation must
   never automatically activate physical I/O, especially output/TX.
5. Record platform, commands, tested source SHA, fixtures, expected/actual
   outputs, limits and opt-in physical results in the evidence ledger.
   The release remains blocked until BR-A, BR-S and BR-V are qualified.
   **A 0.4.0 C++ compiler is explicitly out of scope.**


## Work order and dependent engineering changes

### Phase A — device and graph contracts, recorded fixtures, fake backends

- Inventory current S7 sources and tests; trace live hardware on Linux,
  Windows backend exclusions and packages. Freeze independent reference
  fixtures: complex-IQ, mono/stereo F32 WAV, small RGB/YUV video.
- Review hardware single-boundary/one-rate behavior. Introduce precise
  supported profiles (one-clock full-duplex audio first; independently
  owned RX and TX where needed) through an ADR, not an ad hoc special
  case in a lab.
- Preserve causal memory/publication/rollback on graph failure. A
  physical output already accepted by a device cannot be rolled back.
  Rate/clock discontinuity policy and timestamp provenance must be
  explicit in the run report.
- Implement synthetic providers and acceptance tests for partial blocks,
  zero-frames, stop/drain, queue overflow, device loss, wrong type/rate
  and cancellation.
- Freeze a backend-neutral typed processing boundary so the same saved
  diagram can accept replay and physical input; keep device callback and
  execution host responsibilities independent of the graph's DSP logic.
- Add representative immutable audio/IQ/video fixtures early and verify
  their source metadata and numerical replay before live qualification.

- Design the GStreamer audio and video adapters as real S7 processing
  boundaries, not command-line media pipelines. Agree on appsrc/appsink
  ownership, caps, timebase mapping and clock policy before writing code;
  follow [the mandatory GStreamer contract](RELEASE_0.3.0_GSTREAMER.md). No device activation from graph loading, discovery
  in CI or clicking a student link.

### Phase B — independent DSP substrate and early labs

- **SDR01–SDR07**: IQ, sampling, spectral units, receiver artifacts,
  downconversion, filters/decimation, AGC. Provide reproducible I/Q
  source or capture/replay. Test exact early/late samples and aliases.
- **AU01–AU04**: WAV/file input-output, identity full-duplex, EQ, echo.
  Implement the first production GStreamer audio synthetic path (GS-A1)
  through the actual S7 graph, then compare its PCM to the native provider.
  Inspect block and feedback delay contracts; don't change graph
  semantics to obtain a better-sounding result.
- Implement only required DSP operators, sample-rate conversions,
  bounded file/block handling and observers. Verify *every* new element
  against a second independent implementation and unsupported-rate cases.
- Start physical RX-only and microphone/line-input smoke tests with
  documented device profiles; carry known failures into regression tests.

### Phase C — hardware paths and complete effect/modem chains

- **SDR08–SDR15**: original modulation, symbol decisions and BER labs;
  **AU05–AU09**: reverb, waveshaping, dynamics, time-varying delays.
- Qualify a real-time same-clock duplex audio session; test startup,
  late callbacks and end-of-stream. Qualify opt-in GStreamer live audio
  input -> S7 processing -> output (GS-A2) with physical latency evidence. Implement coordinated, operator-
  enabled RX/TX only with safe coax fixtures and bounded output.
- Check codec/format/sample conversions, clock drift and short transfer
  under real driver callbacks rather than equating mock success with RF.
- Implement necessary bounded queues, parameter-update staging,
  amplitude safeguards and explicitly visible rates; keep graph
  execution out of the host audio callback until a different safe
  architecture is reviewed and proven.

### Phase D — synchronization, OFDM, capstones, video

- **SDR16–SDR24**: CFO/timing recovery, frame synchronization,
  equalization, OFDM, physical link, RX-only ADS-B and measured two-radio
  clock comparison. Synthetic/recorded references precede hardware.
- **AU10–AU12**: time/pitch transforms, FFT effects, interactive
  guitar-effects graph. Quantify additional effect latency and dropout
  under common tested devices and host loads.
- **VID01–VID04**: whole-frame validation and minimal shipping media
  adapter path; qualify GStreamer synthetic (GS-V1) and opt-in camera
  (GS-V2) chains through real S7 frame processing. Use FFmpeg as an
  independent comparison or explicitly selected alternative. Do not fall back to per-pixel S7 steps for
  production image processing.
- Repeat full saved-graph, reference and Studio tests after any
  scheduler, buffer, clock or frame design change.

### Phase E — integrated release candidate

- Close **H8 BR-A/BR-S/BR-V** on the same pinned candidate, with recorded
  input oracles, live-device comparisons, no-device-activation negatives,
  bounded failure handling and explicit known/unknown clock evidence.

- A single reproducible source revision and build package must provide
  36 qualified lab guides/diagrams, four video verification tests and
  **production GStreamer sound and video integration** (GS-A1/A2,
  GS-V1/V2, GS-X).
- Independent regression tests prove actual math. For RF/audio, retain
  time-aligned captured samples, consent/rights-checked fixture metadata,
  physical settings, errors, measured distributions and operator logs.
- Perform Linux Studio/installed help/package qualification and named
  Windows/Wine checks where actually supported; no false Windows SDR
  capability assumptions.
- Show one complete low-latency guitar/mic effects pipeline, one verified
  SDR capture and a bounded permitted physical TX/RX experiment, and a
  small real-camera frame pipeline. Demonstrate both native audio and
  GStreamer audio through an actual S7 graph, plus GStreamer camera
  capture -> S7 whole-frame transform -> output. This is an **integrated hardware
  release test**, not a claim of universal hardware compatibility.

## Performance and reproducibility conventions

- **Audio:** start with one same-clock 48-kHz backend and 64/128/256
  frame buffer variants. Record measured analog input/output latency
  (median, p95, p99, worst observed), processing durations/variance,
  queue depth, actual driver rate, xruns, emergency-output and dropout
  count, along with load and device specs. Work toward the
  [10 ms local-path goal](../education/mit-audio/REALTIME_PROFILE.md)
  but report actual results and limitations. No hard real-time guarantee.
- **SDR:** physical and requested rate separately, amplitude scaling,
  channel count, frequency/bandwidth/gain/antenna readback, sample
  continuity, throughput versus nominal rate, and device/host timestamp
  provenance. Where hardware counters are unavailable, state **unknown**,
  not zero losses. Never treat the 2.4 GHz band as blanket authority
  for arbitrary over-the-air emission.
- **Video:** exact frame dimensions, pixel layout, stride, crop,
  sequence, detected gaps, actual FPS, end-to-end frame latency, CPU,
  memory and bounded queue occupancy. Perceptual lossy video is not
  expected to be pixel-identical.
- **Sources:** cite MIT 6.02/6.450/6.973 and 6.S965 correctly for SDR,
  6.341 for audio, along with licensed supplemental references. These
  remain **original S7 labs**, not official MIT assignments.
- **Quality:** all reference-suite tests are reproducible without
  hardware; physical tests are opt-in, resource-bounded and executed
  on named hardware. Separate numerical, recorded, and live results.

## Status ledger

| Item | Status 2026-11-20 | Next test milestone |
| --- | --- | --- |
| SDR01–SDR24 | Curriculum documented; **24 release targets, not qualified** | Implement first deterministic IQ lab group with real diagram oracles |
| AU01–AU12 | Curriculum documented; **12 release targets, not qualified** | Common-clock fake duplex, then physical identity loopback |
| VID01–VID04 | Newly specified; **not implemented/qualified** | Small synthetic frame + layout/EOS fault test |
| GStreamer audio and video | **Mandatory H6; not implemented/qualified** | Production appsrc/appsink S7 paths with GS-A1/A2, GS-V1/V2 and GS-X evidence |
| H8 hardware/model boundary and replay | **New gate; not qualified** | BR-A/BR-S/BR-V same-model replay/live profiles and no-device negatives |

| Hardware-oriented runtime | Existing single-boundary provider baseline; **new contracts open** | ADR + fake two-boundary / duplex conformance and deadline reporting |
| Candidate release artifacts | Not prepared | Gate-specific evidence pinned to the same source and package revision |

The [native compiler (0.4.0)](RELEASE_0.4.0.md),
[distributed coherence (0.5.0)](RELEASE_0.5.0.md) and
[ARM Linux deployment beta (0.6.0)](RELEASE_0.6.0.md) are future releases;
H8 is the limited interface and replay groundwork, not their implementation.

## Not selected as 0.3.0 blockers

The earlier 0.3.0 candidate backlog includes remote node administration,
PKI/Trusted Networks UX, signal generator UI, development-test
labels, general-purpose parallel graph scheduling and universal distributed
clock synchronization. They remain valid research/backlog items but do not
block the hardware-education 0.3.0 scope **unless needed to make a named
lab or mandatory video gate work**. In particular, **FPGA programming
is explicitly outside this course release**.

0.2.0 now owns diagram and viewer printing as mandatory G4/P; retain
its qualified PDF/print pathways in 0.3.0 regressions. It is not a
new 0.3.0 feature.

## First handoff for implementation agents

Inspect source **before** changing APIs. Create a capability matrix
mapping each SDR/AU/VID requirement to actual code locations and existing
tests. Propose one causal timing/hardware-run ADR and start in parallel
with independently verifiable **SDR01–SDR03**, **AU01/AU02**,
**VID01** and GStreamer synthetic bridges **GS-A1/GS-V1**.
Use small reviewable feature or lab PRs and retain a live evidence
manifest. A planned lab name or simulation diagram is not a passing
hardware verification result.
