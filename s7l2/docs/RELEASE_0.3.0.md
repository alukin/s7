# S7 0.3.0 — hardware-oriented signal-processing qualification

**Scope revision: 2026-11-20.** This supersedes the 2026-11-18
candidate-only introduction while preserving the historical feature research
below. The 0.2.0 contract remains the independently qualified baseline.

The maintainer has explicitly selected **all 24 original MIT-SDR labs**
(SDR01–SDR24) and **all 12 original MIT-Audio labs** (AU01–AU12) as
**mandatory 0.3.0 deliverables**. Their purpose is to improve and verify
S7's causal numerical execution, hardware-facing I/O, buffering, sample
rates, clock provenance and real-time-like behavior. Include four named
preliminary whole-frame video tests **VID01–VID04**, rather than a complete
video education track. **Production GStreamer integration for both audio and
video is an independent mandatory release gate (H6)**, not merely a video
smoke test or external testing utility.

The [0.3.0 qualification work plan](RELEASE_0.3.0_WORKPLAN.md) is the
authoritative **live gate matrix**, engineering phases, tests, required
evidence and exclusions. No gate is declared passed merely by this planning
revision. The laboratories' own course maps and numerical oracles remain
in [MIT-SDR](../education/mit-sdr/COURSE_MAP.md) and
[MIT-Audio](../education/mit-audio/COURSE_MAP.md); all share the
[educational verification standard](../education/LAB_STANDARD.md).

## Mandatory release objectives

1. **H0 — Hardware-oriented causal execution:** reviewed support for
   coordinated source/sink ownership and one-clock full-duplex audio,
   properly bounded buffering and failure semantics, explicit rate/clock
   and device metadata; no hidden model-time changes.
2. **H1 — SDR teaching conformance:** all 24 SDR diagrams, guides and
   independent test oracles, with recorded I/Q and the specified opt-in
   RX and calibrated coax TX/RX qualification on available devices.
3. **H2 — Audio teaching conformance:** all 12 audio diagrams, guides,
   numerical oracles, file replay and live audio demonstrations.
4. **H3/H4 — Physical hardware evidence:** genuinely measured local
   microphone/guitar-to-headphone latency, processing deadlines and
   dropouts; real SDR RX/TX readback, continuity and physical measurements.
   The under-10-ms local audio goal is a **measurement target** on a
   named qualified profile, not a claim of existing capability.
5. **H5 — Preliminary video:** VID01 frame fidelity/stride/colors,
   VID02 frame lifecycle/backpressure, VID03 opt-in webcam whole-frame
   capture/output, VID04 selected media-adapter frame tests.
6. **H6 — Production GStreamer audio AND video:** actual S7 processing graphs
   connected through bounded GStreamer appsrc/appsink paths, with independent
   reference outputs, caps/clock/ownership tests, latency measurements,
   failures and opt-in live audio/camera checks. Follow the
   [mandatory GStreamer contract](RELEASE_0.3.0_GSTREAMER.md).
7. **H7 — Final integrated candidate:** same source revision, real saved
   diagrams, independent references, Studio workflow, installed package,
   Linux and named platform checks, real-device logs and explicit faults.

A mandatory lab that has not met its corresponding numerical or stated
hardware acceptance criteria is **not complete**. Do not silently lower
the target or substitute plots and tool-only experiments for actual
S7 execution. An explicit later maintainer scope change is required to
revise a gate.

**Safety/architecture:** OTA transmission is optional and always subject
to local radio regulations and measured emission constraints; safe,
attenuated coax experiments are the baseline for TX/RX. Audio callback
threads must not run unconstrained S7 graph code. Video frame processing
uses bounded whole frames, not a graph step per pixel. No FPGA programming
is in this release. The approved serial causal schedule remains the
reference unless an independently reviewed runtime improvement is proven.

## Additional research and candidate backlog (not automatic release gates)

The remaining historic sections record research and candidates that
were deferred from 0.2.0 or independently discussed for future versions.
Only requirements specifically promoted into H0–H7 above are mandatory.
For example, the larger two-host Full HD video showcase, node
administration, PKI UX, printing and general parallel scheduler are
**not** silently required by this narrower hardware-education contract.
The explicitly promoted **H6 GStreamer** gate requires qualified S7 audio
**and** video adapters, not an external test-tool invocation. The historical
GStreamer subsection below initially focused on video and is superseded
where its scope is narrower than [H6](RELEASE_0.3.0_GSTREAMER.md).

## Candidate priorities and sequencing

| Area | Planning status | Prerequisites and first useful increment |
| --- | --- | --- |
| Production whole-frame video | Candidate, currently researched | Review frame/ownership/media-session contracts; deliver a real webcam-to-remote-viewer pipeline and frame-to-numeric bridge without changing scalar step semantics |
| GStreamer audio + video | **Mandatory 0.3.0 H6 gate** | Implement and qualify bounded production S7 appsrc/appsink adapters for both media paths; compare with native PortAudio/PipeWire and FFmpeg |
| Headless remote node administration | Deferred 0.2.0 goal; design review required | Versioned node-settings API, atomic persistence, authorization, revisions and supervisor-owned restart |
| Credential tooling and Studio trust UX | Deferred 0.2.0 goal; depends on administration/provisioning decisions | Headless cert/CSR/signing tools first; viewer and Trusted Networks UI consume independently reviewed services |
| Studio printing and software instrument | Deferred 0.2.0 goals; separable | Explicit PDF/print viewer matrix; independent two-channel schema-backed generator with headless tests |
| Developer test selection | Deferred optimization, not a release correctness substitute | CTest area/cost labels and documented focused selections; full release suite retained |
| Within-node runtime parallelism | Research decision only | Resolve execution regions, plugin safety, cost and determinism before selecting any scheduler |
| Broader distributed timing/coherence | Research candidates | Define multi-rate, hardware-clock alignment and failure semantics independently of the static single-rate profile |

The maintainer/Codex track may continue video design and experiment work while
the distributed work is qualified for 0.2.0 by its assigned owners.
Do not silently enlarge 0.2.0's delivery gates when an experiment succeeds.

## Whole-frame VideoSource/VideoSink

The original [video plan](VIDEO_IO_PLAN.md) proposed scalar pixel streams.
Its [numeric V0 spike](VIDEO_IO_SPIKE.md) is historical cost evidence; the
[frame-oriented direction](VIDEO_IO_FRAME_DESIGN.md) supersedes a per-pixel
production execution contract. [Full HD FFmpeg measurements](VIDEO_IO_FULL_HD.md)
report synthetic 1080p at nominal 30 fps and real 720p camera capture upscaled
to 1080p at a lower measured rate. Those are **external media experiments**,
not production S7 frame ports, VideoSource/VideoSink elements or Studio viewers.

Candidate first increment:

1. Define frame dimensions, planes/stride, pixel formats, ownership, reference
   counting/copying, sequence/timing provenance, bounded queues, failure/EOS
   and conversion rules. Evaluate one whole frame per frame element, not one
   S7 graph step per pixel.
2. Capture a webcam through a headless `VideoSource` on one host and
   display it through `VideoSink` or a viewer on another host. Verify
   format, frame continuity, drop policy, session authorization and
   observable backpressure/latency.
3. Add a receiver-side frame transform and an explicit frame-to-numeric
   computation/result bridge. Verify numerical results independently of
   any displayed video.
4. Support explicitly selectable codecs/settings, such as raw or
   lossless transport and reviewed JPEG/H.264 profiles; refuse unsupported
   formats or security-policy combinations without silent downgrade.
5. Expand to screen/window capture and native Windows video only after the
   first complete provider/transport/viewer profile is measured and
   documented on named hosts.

Production implementation depends on a separate reviewed agreement about
whether and how specialized media transport cooperates with S7
control/deployment/security and the existing frame-service semantics.
Do not infer native camera capabilities from the synthetic FFmpeg tests.

## GStreamer adapters

**Originally assigned for video on 2026-11-18; expanded to mandatory
production audio AND video on 2026-11-20.** Follow the
[H6 GStreamer contract](RELEASE_0.3.0_GSTREAMER.md). Add practical
GStreamer adapters alongside the native PortAudio/PipeWire audio and
FFmpeg-oriented video paths. They were not
a prerequisite for the 0.2.0 Full HD experiment and are not a new 0.2.0
runtime/package dependency.

The adapters should let a headless S7 video chain exchange complete frames
and encoded access units with GStreamer pipelines. Candidate integrations
include `appsrc`/`appsink`, capture/output devices, codecs and media
transports. Choose the supported subset explicitly rather than treating
every installed GStreamer plugin as an S7 support commitment. Preserve frame
layout, ownership, sequence, timing provenance, bounded queues and explicit
conversion/codec settings.

Acceptance goals, subject to the video-contract review:

- Headless source/sink integration; metadata, offline validation and plugin
  loading must not silently activate capture or playback.
- Checked format/plane/stride and ownership handoff, cancellation/EOS,
  error propagation, backpressure and explicit whole-frame drop policies.
- Explicit plugin/capability discovery with useful missing-codec/format errors;
  no silent replacement of requested encoders or security profiles.
- Receive-side frame transform and frame-to-numeric bridge, checked against
  an independent reference rather than display appearance.
- Measured Full HD throughput, per-frame latency, CPU, memory, copies and
  queues on identified hosts; platform/package qualification for the
  explicitly supported Windows/Linux configurations.
- Reuse S7's control authorization for managed media sessions and coordinate
  network/protocol changes with the distributed work owner.

Existing GStreamer-backed research drivers are test tooling, not shipping
adapters. API references:
[GStreamer appsink](https://gstreamer.freedesktop.org/documentation/app/appsink.html),
[appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html) and
[buffer ownership](https://gstreamer.freedesktop.org/documentation/additional/design/buffer.html).

## Remote node administration and Trusted Networks

The existing control protocol deploys **graphs**; it does not expose a
versioned transaction to edit the runtime's listener, TLS or controller
authorization configuration or safely restart a process. This remains a
separate service, not a Studio-only implementation.

Before coding, review a headless contract for:

- Read/validate/stage/commit of node settings, optimistic revisions,
  idempotent retry and atomic persistence; preserve unrelated settings.
- Bind/advertise endpoints, data security profiles, controller identities,
  purpose/fingerprint pins, pending-versus-effective state and refusal
  of unauthorized requests.
- Loss of a control connection while updating the endpoint or pin,
  reconciliation after reconnect, rollback/forward recovery and
  **supervisor-owned** restart. A node cannot promise to restart itself
  after its process exits.
- Studio discovery/configuration versus diagram deployment versus node
  startup-settings ownership. Do not overload
  `GET_NETWORK_CONFIGURATION`, which returns deployed diagram configuration.

Coordinate C++ reference behavior, CLI, shared fixtures, supported Python
parity and Studio/Trusted Networks UI only after agreeing on this protocol.
The prior detailed proposal is preserved in the Git history of
`RELEASE_0.2.0.md` and
[the Studio boundary document](STUDIO_DESIGN_0_2.md).

## Certificate provisioning, creation and inspection

The 0.2.0 two-host demonstration uses existing documented independent
credentials; comprehensive provisioning and UI are not prerequisites.
For a later release, independently review:

- Headless CA import/generation, CSR/signing, node/control profiles, SANs
  including `spiffe://s7/node/<uuid>`, TLS usages, private-key protection,
  safe output, renewal/replacement and controller-pin rotation.
- Studio certificate viewer showing parsed subject/issuer, validity,
  fingerprints, SANs, key usages and chain details, distinctly marking
  parsing versus actual verification and authorization.
- Negative tests for expiry, untrusted chain, mismatched node UUID,
  missing control EKU, wrong pin, failed output, replacement and reload.
- Packaged provisioning and node-control workflows without private
  development CA keys.

See [identity and provisioning](../../doc/IDENTITY_AND_CREDENTIAL_PROVISIONING.md).
An identity viewer must never reveal private keys or imply authorization
from display-only parsing.

<a id="studio-instruments-printing-and-ui"></a>

## Studio instruments, printing and UI

These are separable user-facing candidates, **not** core distributed-runtime
release gates.

**Diagram and viewer printing:** choose the supported diagram scope, PDF and
physical-printer workflow, fit/tiling and page settings, and the Scope/live
and 3D viewer matrix. Preserve mathematical symbols, saved element geometry,
legible wires and coherent live-data snapshots. Printing must not pause or
advance the simulation. Test vector/PDF output, failure/cancellation and
relocated package dependencies. Physical-printer evidence differs from
automated PDF verification.

**Schema-backed two-channel signal generator:** use the UTG932E as an
interaction reference, not as a required full-hardware emulation. Candidate
first waveforms are sine, square/pulse, triangle/ramp and DC with separately
configured channel amplitude, frequency, phase, offset and output enable.
Define resets, duty cycle, parameter transitions, model-rate limits and
aliasing. Runtime generation must remain deterministic and headless; Studio
owns rotary controls and visualization. Verify against independent discrete
waveform expectations, saved diagrams and Scope. Advanced modulation,
arbitrary waves and hardware control require separate approval.

The earlier
[Studio instrument control design](STUDIO_INSTRUMENT_CONTROLS.md) records UI
details and historical 0.2.0 assignments.

**Test grouping:** label CTest cases by technical area and relative cost;
provide fast local subsets and document file-to-test guidance. Keep full,
slow numerical and GUI release regressions mandatory in CI/qualification.
Test selection is a developer-productivity improvement, not a replacement
for correct release evidence.

## Runtime parallelism and later distributed research

The [parallelism research record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md)
preserves candidate execution regions, cost models, worker pools and unresolved
plugin safety/determinism requirements. It **does not** commit to a parallel
scheduler in 0.3.0. Sequential compiled execution remains the reference until
a separate decision and validation contract justify a new mode.

Additional candidates — multi-rate distributed scheduling, coherence
domains and independent-clock alignment, generic zero-delay feedback,
remote observation subscriptions, durable process-crash replay and broader
hardware/video integration — need their own problem statement and acceptance
envelope. They are not implicit side effects of video or administration work.

## Promotion rule

Before moving any candidate into a release gate, record the owner, user-facing
goal, minimum supported platform/profile, explicit exclusion list, independent
correctness reference, failure semantics and release qualification cost.
Prefer a small complete capability over a large unqualified matrix.
