# S7 0.3.0 — release goals

Planning record, 2026-11-18. Goals below are assigned work, not implemented
capabilities or final release qualification. The separate
[runtime parallelism record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md) retains its
own research/decision boundaries; it does not promise a parallel scheduler.

## GStreamer adapters

**Assigned to 0.3.0 by the maintainer, 2026-11-18.** Add practical GStreamer
adapters alongside the FFmpeg-oriented video path. They are not a prerequisite
for the current Full HD experiment or a new 0.2.0 production dependency.

The adapters should let a headless S7 video chain exchange complete frames and
encoded access units with GStreamer pipelines. Candidate integrations include
`appsrc`/`appsink`, capture/output devices, codecs and media transports. Choose
the supported subset explicitly rather than treating every installed plugin as
an S7 support commitment. Preserve frame layout, ownership, sequence, timing
provenance, bounded queues and explicit conversion/codec settings.

Acceptance goals:

- Headless source/sink integration with the reviewed video-frame contract;
  metadata, offline validation and plugin loading do not activate a pipeline.
- Checked format/plane/stride and ownership handoff, cancellation/EOS, error
  propagation, backpressure and explicit whole-frame drop policies.
- Explicit plugin/capability discovery with useful missing-codec/format errors;
  no silent replacement of the requested encoder or security profile.
- Demonstrate a receive-side frame transform and a frame-to-numeric result
  bridge; verify reference results independently of display.
- Measure Full HD throughput, latency, CPU, memory, copies and queue behavior
  on named hosts; qualify supported Windows/Linux paths and installed packages.
- Reuse S7 control authorization for any managed network/media sessions and
  coordinate protocol integration with the distributed work owner.

The existing GStreamer-backed research drivers are experimental evidence and
test tooling, not delivery of these production adapters. The
[video design](VIDEO_IO_FRAME_DESIGN.md) and [V0 report](VIDEO_IO_SPIKE.md)
record the distinction. API references:
[GStreamer appsink](https://gstreamer.freedesktop.org/documentation/app/appsink.html),
[appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html),
[buffer ownership](https://gstreamer.freedesktop.org/documentation/additional/design/buffer.html).
