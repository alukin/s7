# S7 0.3.0 — mandatory GStreamer audio and video integration

**Accepted release objective: 2026-11-20. Planning only; no new production
GStreamer adapters or qualified graph runs are claimed by this document.**

The maintainer requires GStreamer as an **S7 production integration for
both audio and video**, not merely as a helper in a video test harness.
This is gate **H6** of [0.3.0](RELEASE_0.3.0.md) and the
[qualification work plan](RELEASE_0.3.0_WORKPLAN.md).

Read the [current audio I/O contract](HARDWARE_IO.md),
[low-latency audio execution proposal](../education/mit-audio/REALTIME_PROFILE.md),
[whole-frame video design](VIDEO_IO_FRAME_DESIGN.md) and
[Full HD experimental report](VIDEO_IO_FULL_HD.md).
Native PortAudio/PipeWire and the FFmpeg research path remain useful
independent comparators; they are not replaced by declaring GStreamer a
universal source/sink.

## First supported profile and conceptual data paths

Support a documented **Linux GStreamer 1.x** profile on named tested
hosts with explicit plugin/capability discovery. The precise version,
selected installed plugins and supported caps are recorded in the
release evidence; do not promise every optional GStreamer plugin/codec.

```text
Audio input:
  GStreamer selected source -> appsink -> owned S7 audio boundary
    -> actual S7 causal audio DSP graph
    -> owned S7 audio boundary -> appsrc -> selected output

Video input:
  GStreamer test/camera source -> appsink -> owned S7 frame boundary
    -> actual S7 whole-frame transform -> owned S7 frame boundary
    -> appsrc -> selected output/encoder/viewer
```

Names above are roles, **not invented current element APIs**.
An approved design must specify the concrete plugin, typed S7 boundary,
stream ownership, graph lifecycle, rate-domain interaction and package
surface. GStreamer must deliver samples/frames *through* an S7 graph,
not bypass it in an external `gst-launch` pipeline. Headless operation
is essential; Studio integration additionally observes and configures
the same processing graph without running its DSP in a UI thread.

**Audio minimum:** mono/stereo raw PCM at 48 kHz, with a named
F32 or I16 profile and explicit interleaving, channel order, full-scale
meaning, negotiated caps and output sample mapping. Exercise a bypass
graph and at least one verified S7 effect (e.g. gain/EQ/delay), using
synthetic data first and an opt-in actual mic or guitar interface.
Measure GStreamer-path physical analog loopback latency and software
processing/buffer metrics **separately** from the native PortAudio/
PipeWire baseline. The goal of low-latency effects applies to S7;
GStreamer may have a distinct measured performance envelope. Never
hide a clock mismatch through unreported implicit resampling.

**Video minimum:** bounded whole frames through one precisely named
raw RGB or YUV geometry/format profile (e.g. 640x480, nominal 30 fps).
Exercise a deterministic S7 grayscale/ROI transformation, source
capture or generated frames, and output/viewer. Define stride, planes,
colorspace, sequence, timestamp and any explicit conversion. An
encoded path (such as JPEG/H.264) is optional for the first correctness
profile, but when supported its codec/caps, keyframes and lossy
tolerances must be explicit.

**Multi-device or distributed A/V synchronization, general-purpose
adaptive resampling, arbitrary codec coverage, end-to-end zero-copy
and production Full HD remote streaming are NOT implied by this minimum
adapter profile.** They require separate documented capability claims
and tests if implemented.

## Architecture, safety and timing invariants

1. **Explicit opt-in and capability checks.** Constructing, parsing,
   validating or opening a diagram does not activate microphones,
   speakers or cameras. Missing plugins/caps/devices fail visibly.
   No unexpected decoder or fallback pipeline may replace a requested
   exact format or security profile.
2. **Owned memory and stable lifetime.** GStreamer `GstBuffer`
   and mapped-memory lifetime, plane strides and offsets, reference
   counts and S7-owned copies/leases are explicitly modeled. Do not
   retain an `appsink` buffer pointer after release. Never treat an
   encoded packet as a decoded frame.
3. **Time semantics.** Keep GStreamer pipeline running time, PTS/DTS,
   buffer duration, nominal sample/frame rate, hardware timestamp,
   host callback time and S7 graph step/index distinct. Report caps
   renegotiation and discontinuity. No software wall-clock timestamp
   is relabeled as ADC acquisition time or UTC.
4. **Bounded flow control.** Specify allocation limits, queue depth,
   maximum worker wait, blocking or explicitly selected drop policy,
   end-of-stream/drain and cancellation. Report underrun, overflow,
   dropped/duplicated media, conversion, late frames and the valid
   output prefix. A media buffer is not a model-step delay.
5. **Causal runtime.** Do not run arbitrary graph evaluation,
   allocation, logging, UI rendering or blocking locks inside
   real-time audio callbacks. Use a bounded owned processing worker.
   Preserve normal graph state transitions, observer separation,
   error visibility and sample-to-output mapping.
6. **Safety.** Initialize audio gain conservatively and monitor via
   headphones for microphone/guitar tests; avoid microphone-speaker
   feedback. No actual hardware is opened during CI.

## Mandatory acceptance tests

| Case | Workload and reference | Expected observable failure or boundary test |
| --- | --- | --- |
| **GS-A1** | Offline `audiotestsrc` or `appsrc` known PCM -> actual saved S7 identity/gain graph -> `appsink`; compare exact indexed PCM samples with independent oracle | Unsupported caps/rate, wrong channel order, short buffer, EOS/drain and tail handling |
| **GS-A2** | Live same-clock opt-in audio I/O via GStreamer -> S7 bypass/one effect -> output; physical loopback latency distribution, xrun/CPU/queues vs native baseline | Worker stall, underrun, disconnect and clean cancellation; accurately report any device/server conversion |
| **GS-V1** | `videotestsrc` or `appsrc` known frames -> actual S7 grayscale/ROI transform -> `appsink`; independent pixel/stride/sequence reference | Odd stride, invalid format/caps, source stall and truncated plane |
| **GS-V2** | Opt-in real camera -> S7 frame transform -> output/viewer through selected GStreamer source/sink | Permission denial, camera unplug, EOS and explicit drop/frame-rate statistics |
| **GS-X** | Lifecycle and packaging: plugin load/unload, missing optional plugin, cancellation during start/stop, installed package and disabled-backend build | Never activate a real device in normal CI; no hidden codec fallback or resource leak |

Test code should include deterministic synthetic/fake media flows and
additional opt-in hardware tests. Record the *actual saved S7 graph*
SHA, GStreamer version and caps, backend/device/driver, samples/frames
admitted and delivered, sample rate and PTS provenance, queue allocation,
CPU and latency distribution. A visually correct video or audible audio
alone is not enough.

## Delivery sequence

1. Design review of GStreamer adapter interface alongside S7 audio
   duplex and video whole-frame ownership contracts; decide one
   common bounded media-session abstraction **only if measured
   semantics actually match**, not by forcing audio and video into
   a single clock model.
2. Land synthetic **GS-A1** and **GS-V1** with production adapters,
   independent oracles and lifecycle faults.
3. Land **GS-A2** and **GS-V2** on named opt-in hardware with clear
   latency, discontinuity and error evidence.
4. Run **GS-X**, package and Studio checks and compare final results
   against the independent native audio and FFmpeg paths.
5. Close H6 only when both audio and video pass. The video `VID04`
   smoke case may reuse GS-V1 evidence but **must not replace
   GS-A1/GS-A2**.

The S7 0.3.0 release is blocked if either mandatory audio or video
GStreamer path lacks the above acceptance evidence. Any change to
causal execution, port data ownership or media clock semantics
requires a reviewed architecture decision and regression tests.
