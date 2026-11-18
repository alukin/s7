# VideoSource / VideoSink implementation plan

The maintainer assigned production [GStreamer adapters to 0.3.0](RELEASE_0.3.0.md#gstreamer-adapters).
The current Full HD experiment evaluates FFmpeg; existing GStreamer-backed
spikes remain test tooling rather than a production adapter dependency.
The [Full HD report](VIDEO_IO_FULL_HD.md) records the subsequent whole-frame
FFmpeg measurements and separates native synthetic 1080p from upscaled camera input.

**Direction revised, 2026-11-18:** the maintainer clarified that production video
may use specialized media transfer and normally processes whole frames.
[Frame-oriented video design](VIDEO_IO_FRAME_DESIGN.md) supersedes the per-pixel
production baseline below. Numeric chains remain available through explicit
frame/block processing and frame-to-numeric elements, including on the receiver.
The numeric pixel proposal below is retained as the historical V0 experiment;
it is not a requirement for the next implementation.

Status: **implementation direction approved, 2026-11-18; V0 evidence delivered;
public native video elements not yet shipped**.
Maintainer/Codex own this work. D0 and D1/D2 remain teren_ukr's responsibility.
The maintainer requested native PipeWire video capabilities, Windows capture
research, a webcam-on-one-host / viewing-on-another first milestone, and
**user-selectable, tunable compression**. Camera capture is first; screen/window
capture follows. This document proposes the implementation sequence, not a new
accepted wire protocol or an already-qualified performance envelope.

Implementation started after the maintainer's instruction to proceed. The
[V0 spike](VIDEO_IO_SPIKE.md) now records 16 passing two-host cases: real scalar
graphs and existing numeric mTLS transport, with GStreamer PipeWire adapters and
a batched-owner comparison. Public native video providers are not shipped by
that experimental driver; its evidence and limits are recorded separately.
New codec/profile semantics still require the review described below.

## 1. First observable result

Host A captures a real webcam through PipeWire; host B reconstructs and displays
the moving images. Start with a small raw stream, then compare user-selected
lossless and lossy compression. The first S7 end-to-end graph is:

```text
Host A: webcam -> VideoSource -> numeric pixel processing -> network sink
Host B: network source -> VideoSink -> PipeWire video output -> viewer
```

PipeWire output needs a consuming application to display it; exporting a node
alone does not create a window. An external viewer can establish the first
result. A Studio Video Viewer follows, with the same headless capture/transport
implementation. Network boundaries stay explicit. The first driver owns the
hardware run and authenticated transport directly; it does not claim that
ordinary Studio Run or service deployment already activates hardware.

## 2. Existing numbers and existing causal rules

Use existing numeric signals first. No new image-valued port type, special
scheduler, per-pixel event loop, or change to publication/rollback is required
by this baseline. The [formal model](../../doc/S7_Formal_Model.md) already treats
a signal as an opaque value; the implementation's numeric catalog is sufficient
for a raster stream.

Proposed first application layout:

- One graph evaluation transfers one pixel, with four `U8` ports named
  `B`, `G`, `R`, `A`. Initial images are progressive, top-to-bottom, left-to-right,
  BGRA8; opaque captures have alpha 255.
- One existing numeric **sample frame** contains those four simultaneous channel
  values. One **video image** contains `width * height` sample frames. They are
  different units throughout APIs, counters, CLI help and tests.
- `pixel_rate = width * height * image_rate`, reduced exactly as a rational.
  A 30-image/s video does not imply a 30-step/s pixel graph. Image rate supports
  rational values such as `30000/1001`; it is distinct from actual arrival rate.
  This raster model-time grid does not describe physical pixel readout times.
  Preserve image-level capture/PTS evidence separately; do not manufacture an
  acquisition timestamp for each pixel from its logical position.
- Image `n` occupies pixel positions `[n*width*height, (n+1)*width*height)` in a
  fresh segment beginning at position zero. Network packets can split an image
  or contain pixels from adjacent images. Reassembly uses positions, not packet
  boundaries or arrival time. Size/rate/layout changes require a new segment.
- A scalar `Delay` delays one pixel evaluation, not a whole image. No element
  becomes causal memory merely because its device/codec buffers images.
- Native pitch, padding, bottom-up storage and unused alpha bytes are explicitly
  unpacked into this application layout. YUV/RGB conversion, scaling and frame
  rate conversion must be selected/reported, not hidden graph conversions.

The raw path can reuse `NetFrameSink` / `NetFrameSource` with four U8 channels
and the [existing frame service](FRAME_TRANSPORT.md). Images larger than its
256 KiB decoded-packet bound span multiple packets; the bound is not raised to
fit an image. Raw, LZ4 and Zstandard use the existing formats.

Both ends must agree on geometry, pixel layout, alpha/color interpretation,
image/pixel rates and segment identity before transfer. A shared immutable run
manifest checked by the two-host spike driver is sufficient for the experiment.
Production needs a reviewed descriptor exchange and mismatch errors; matching
U8 channel types alone cannot establish compatible image dimensions. Do not
silently add fields to a wire schema or advertise deployment support. Coordinate
that extension with teren_ukr without taking over D0 or D1/D2.

## 3. Modules and resource ownership

Add public `s7video` with `VideoSource` / `VideoSink` metadata, factories and
provider adapters. Reuse `s7hardware::Boundary`, `Stream`, `Provider` and `Run`
where their existing contracts fit. Video-specific configuration adds width,
height, application/native pixel format, image rate, color interpretation and
bounded image queues; the generic hardware sample-frame rate is the pixel rate.
Use distinct backend IDs, provisionally `pipewire-video` and `mf-video`, so the
runner cannot resolve a video request through the existing audio `pipewire`
provider. Exact public parameter names are settled in V0/V1.

| Component | Responsibility / proposed location |
| --- | --- |
| `s7video` | Raster/layout validation, image assembly/disassembly, PipeWire and Media Foundation adapters, explicit video boundary elements |
| `s7hardware` | Existing run ownership, cancellation, scalar staging and failed-step notification; narrow video-related extensions only when demonstrated necessary |
| `s7net`, `s7transport` | Existing explicit numeric links; later reviewed image-codec boundary/profile and negotiation, with shared fixtures |
| `s7hw` / SDK test driver | Explicit device discovery/activation and a run plus network owner; offline check remains device-free |
| `s7studio` | Element editing and optional Video Viewer; bounded completed-image handoff from the stepping thread, no capture in Qt rendering callbacks |
| Build/package tools | Optional providers/codecs, SDK/plugin installation, Linux packages and Windows ZIP dependency/notices inventory |

One hardware boundary and one exact rate domain per initial node graph preserve
the [current hardware profile](HARDWARE_IO.md). Capture and output on separate
hosts meet that boundary count, but do not prove that independent physical clocks
are locked. Measure drift/queue growth. No graph code runs in a PipeWire or MF
callback. Callbacks copy validated data into preallocated bounded storage and
return native buffers promptly; workers own conversion/codec work. Device,
image staging, transport replay, codec and viewer allocations are all budgeted.

On known loss, malformed data, changed format or failed graph evaluation, latch
the fault and stop the segment. Never splice an incomplete image into the next
one or retry consumed hardware data. Receiver queues cannot grow indefinitely.
The initial reliable mode stops/faults when bounds/deadlines are exceeded; it
does not silently drop pixels. Preview may skip **complete** images in its
display mailbox, with a separate visible counter, without changing graph data.
An optional lossy live-stream policy needs its own explicit whole-image skip
and position contract; it is not implied by enabling JPEG or H.264.

## 4. PipeWire capabilities and Linux sequence

Implement native `pw_stream` capture, format/buffer negotiation, state/error
tracking and explicit target selection. The official capture tutorial covers
raw formats, image sizes, rational image rates and input streams. [P1]

`VideoSource` consumes a PipeWire stream (`PW_DIRECTION_INPUT`). `VideoSink`
produces one (`PW_DIRECTION_OUTPUT`), either targeting a selected consumer or
exporting a video source for an application to consume. S7's sink is therefore
a producer from PipeWire's perspective. Validate routing with an actual viewer;
do not assume audio policy automatically connects video. [P2, P3]

1. Webcam: enumerate candidate nodes/formats, explicitly select the target,
   negotiate the requested application format and report native/conversion
   evidence. If BGRA/BGRx negotiation is unavailable, exercise an explicitly
   configured converter rather than treating YUY2/NV12 bytes as BGRA.
2. PipeWire video output: assemble complete images, negotiate consumer buffers
   and expose a named output node. Report accepted images separately from
   displayed images. Test idle/no-consumer startup and consumer disconnect.
3. Screen/window capture: add the XDG ScreenCast session/selection flow and
   connect using its restricted PipeWire remote. Keep portal interaction in
   a session/controller layer usable by Studio; unavailable interaction in a
   headless session produces an explicit result. Do not replace Wayland capture
   with an assumed unrestricted desktop node. Feature-detect targeting data:
   newer portal versions expose a stream serial; older versions return node IDs.
   The Camera portal is also relevant for portal-mediated camera access. [P4, P5]
4. Virtual-camera interoperability is a separately tested output capability.
   A PipeWire video source is not a promise that every V4L2-only application sees
   a webcam. Do not install a kernel loopback module as an implicit dependency.

Copy/shared-memory buffers are the first profile. Validate every plane, stride,
offset and byte bound. DMA-BUF import/modifiers/synchronization are later
performance work, with explicit support checks; a DMA-BUF fd is not assumed to
be readable CPU memory. Preserve timestamp provenance: SPA header PTS is a
presentation timestamp, not automatically sensor acquisition time or UTC. [P6, P7]

## 5. Windows recommendation and evidence

Use native **Media Foundation Source Reader** for webcam capture. Enumerate
with `MFEnumDeviceSources`, identify a selected device by its symbolic link,
activate its media source, negotiate the media type, and read through
`IMFSourceReader`. Microsoft documents this capture path; friendly names need
not be unique. Microsoft recommends newer capture APIs over legacy DirectShow
for new code. Qt Multimedia or OpenCV need not become runtime dependencies. [W1, W2]

Use asynchronous reads and a bounded handoff, with explicit COM/MF lifecycle
ownership. Flush/stop must prevent callbacks from reaching destroyed state.
Handle null samples, stream ticks, media-type changes, permission denial and
device removal. Native timestamps keep their documented units/domain, separate
from graph model time. [W3]

Use `IMF2DBuffer2::Lock2DSize` when available, with a checked older-buffer path.
Handle padding and negative pitch; return/unlock all borrowed buffers. Initial
application output is the same numeric BGRA8 raster. Native RGB32/BGRx needs
defined alpha handling. Native YUV requires explicit conversion. Microsoft's
basic Source Reader video-processing switch is software processing with stated
real-time limitations, so benchmark it rather than assuming it is the optimized
capture path. [W4, W5, W6]

| Windows capability | Plan |
| --- | --- |
| Webcam `VideoSource` | Media Foundation, after the Linux two-host spike |
| View remote video | Optional shared Studio Video Viewer; no PipeWire dependency on Windows |
| Screen/window capture | Later `Windows.Graphics.Capture` adapter; separate from webcam capture [W7] |
| PipeWire `VideoSink` | Linux capability; unavailable explicitly on Windows |
| Virtual webcam output | Separate Windows 11 feature: `MFCreateVirtualCamera`, custom media source/registration/lifetime work; not just a Source Reader setting [W8] |

### Read-only/toolchain findings, 2026-11-18

- `al1`: USB2.0 HD UVC WebCam nodes `/dev/video0` and `/dev/video1`, PipeWire
  development version 1.6.11, user PipeWire/Wayland sockets and GStreamer.
  These two nodes do not establish two independent cameras.
- `win`: currently Linux, no `/dev/video*`; user PipeWire socket and GStreamer
  present. No Wayland socket found; the SSH environment alone does not prove
  whether a usable desktop/display is available. Confirm a visible viewer before
  choosing it as host B; otherwise use it to verify reception and return completed
  images for inspection as an explicitly limited intermediate check.
- `mail`: `stream_hdmirx`, Rockchip processing/codec nodes; not evidence of a
  webcam. No user PipeWire socket or GStreamer found in this inventory.
- An isolated CMake **compile/link-only** probe passed using our current Clang
  22.1.8/MinGW toolchain and MSYS2 sysroot. It references MF startup, enumeration,
  Source Reader creation, callback and 2D buffer interfaces, and links `mf`,
  `mfplat`, `mfreadwrite`, `mfuuid`, `ole32`. Probe source/logs are under
  `/tmp/s7-video-mf-probe/`. It was not executed and opened no camera.
- The inspected sysroot contains the basic capture headers/libraries but no
  `mfvirtualcamera.h`. The newer virtual-camera path needs a separate SDK/build
  investigation. Microsoft's API minimum is build 22000. [W8]

No live video was captured and no remote host was modified by these checks.
Wine can verify builds/offline/fake-provider behavior; native Windows capture,
camera privacy and display require a real Windows run. Do not reboot dual-boot
hosts as part of unattended qualification.

## 6. Compression is an explicit user setting

Compression belongs to a video **link/stream configuration**, independently of
which camera or screen is selected. Raw is the default diagnostic baseline.
Each stream may select its own codec/settings; the receiver must support and
accept that descriptor before activation. No silent codec substitution or
hardware-encoder fallback. Changing settings initially stops/revalidates and
starts a new segment; live reconfiguration is later work.

| User selection | Controls / contract | Delivery order |
| --- | --- | --- |
| `raw` | Uncompressed, byte-exact application pixels | First spike and reference |
| `lz4`, `zstd` | Existing lossless numeric packet codecs; expose only controls actually supported by the current codec API | Compare in first spike; bit-exact reconstruction |
| `jpeg` | Independent encoded image, JPEG quality and chroma-subsampling settings; alpha initially opaque | First image codec; optional libjpeg-turbo candidate [C1] |
| `h264` | Target bitrate, latency/preset, keyframe interval, profile, explicit encoder implementation/software-hardware selection | Next video codec; optional FFmpeg adapter candidate, native MF encoder evaluated separately [C2, C3] |
| HEVC / AV1 / other codecs | Capability-discovered extensions, their own dependencies/settings/qualification | Later; not required to complete the first milestone |

JPEG/H.264 are explicitly lossy modes for this plan; report decoded image
quality and any selected RGB/YUV or chroma conversion. They must not inherit a
bit-preserving numeric-stream claim. Camera-native MJPEG/H.264 and link encoding
are distinct choices: passthrough is possible only when explicitly selected and
no requested processing/layout change requires decoding. Do not silently bypass
the graph to obtain a better benchmark.

For JPEG/H.264, assemble pixels into complete images at an explicit encoder
boundary and decode before the receiver's numeric pixel graph. The encoder
buffers data but does not create a causal feedback break. Keep codec code outside
hardware discovery and off realtime callbacks. This compresses the link, not the
graph's evaluation count. JPEG is an independently decodable first candidate;
H.264 adds state/reference pictures, startup/keyframe requirements and possible
encoder buffering. Low-latency options have quality/resource tradeoffs. [C1, C2, C4]

The existing `FRAME_ENCODING` extension means raw/LZ4/Zstandard numeric bytes;
do **not** insert JPEG/H.264 into that enum or pretend compressed bytes are pixel
sample frames. Before V3 implementation, review a versioned image-codec profile
over the existing authenticated transport: image/segment sequence, geometry,
pixel/color interpretation, codec initialization, decoded-size bounds,
keyframe/dependency rules, timestamps/provenance, chunking and complete-image
consumption/credit. Large encoded images span bounded messages with bounded
reassembly. Loss/restart invalidates dependent H.264 pictures; initial behavior
faults/stops and starts a new segment with codec initialization/keyframe.
No unreviewed recovery, auto-deployment or Python-parity promise is added.

Studio presents codec selection and only its applicable controls. CLI/JSON and
headless validation expose identical choices. Unknown parameters, unavailable
codec/encoder, incompatible decoder/profile or invalid bounds fail explicitly.
Compression availability, selected implementation and effective settings are
visible in status and saved with the stream configuration.

## 7. Spikes and implementation sequence

| Step | Deliverable | Acceptance / stop condition |
| --- | --- | --- |
| V0a — Two-host feasibility spike | Native PipeWire webcam capture on A, existing numeric transport, image assembly plus PipeWire output/viewer on B; an isolated bulk-transfer control measures device/network ceilings | Visible moving video on B. Fix target/format/display problems before feature integration. Preserve a reproducible command/manifest/report. A GStreamer sanity check is auxiliary evidence, not an S7 element pass. |
| V0b — Actual scalar graph and performance | Execute `VideoSource -> NetFrameSink` and `NetFrameSource -> VideoSink` with one hardware boundary per host; compare raw/LZ4/Zstd and the bulk control | Correct pixel/image positions and synthetic byte-exact reference; measure graph steps/s separately from displayed images/s. Select an achieved envelope before promising resolution/rate. |
| V1 — Public Linux module | `s7video` SDK/plugin, offline metadata, checked layout/rates/budgets, fake provider, explicit `s7hw`/network run owner | Offline checks open nothing; cancellation/faults and partial images are tested. No numeric core type or scheduler change. |
| V2 — Screen/window and viewer | ScreenCast/Camera portal sessions, PipeWire output routing, optional Studio Video Viewer | Webcam and selected screen/window can be viewed remotely; cancellation/session end and format-change behavior explicit. Preview workload remains bounded and independent of graph publication. |
| V3 — User-controlled image codecs | Reviewed codec/profile descriptor and fixtures, JPEG first, H.264 next; saved CLI/Studio settings | Raw/lossless byte identity; lossy quality measures; mismatch/oversize/corruption/slow consumer/keyframe restart cases; latency/CPU/bitrate comparison. JPEG spike may run alongside V0, production follows the contract review. |
| V4 — Windows camera/viewing | Native MF capture using the same numeric contract; shared optional viewer and codec choices | Cross-build/offline/Wine checks plus native Windows camera-to-Linux and Linux-to-Windows viewing. Device denied/busy/removed, pitch and shutdown tests. |
| V5 — Release qualification | SDK consumers, Linux portable/RPM/DEB and Windows ZIP, configured providers/codecs and precise capability notes | Relocated package/plugin/codec checks and named-host live results from the candidate source. No broad device or real-time guarantee inferred from one webcam. |

If per-pixel execution is the bottleneck, first measure packet batching, copies
and dispatch cost. A later equivalent bulk execution proposal needs its own
causal/reference tests and review. Do not invent an image type, bypass graph
processing or change sample rate to conceal that cost.

## 8. Performance and correctness record

The following are **calculated payload requirements**, not measurements. BGRA8
uses four bytes per pixel; values exclude headers/TLS and server/device copies.

| Requested video | Pixel graph steps/s | Raw payload MB/s (decimal) | Raw payload Mbit/s |
| --- | ---: | ---: | ---: |
| 320 × 240 × 15 images/s | 1,152,000 | 4.608 | 36.864 |
| 640 × 480 × 30 images/s | 9,216,000 | 36.864 | 294.912 |
| 1280 × 720 × 30 images/s | 27,648,000 | 110.592 | 884.736 |
| 1920 × 1080 × 30 images/s | 62,208,000 | 248.832 | 1,990.656 |

Begin at 320×240/15 if the camera supports it or an explicit scale/rate conversion
is configured; 640×480/30 is the next target, not a claimed pass. Request exact
native alternatives when appropriate and record the negotiated result. Run
60-second trials and a longer bounded stability trial, repeating performance
cases. Use both synthetic gradients/color bars and real stationary/moving video;
report raw results even when compression expands/no-ops a packet.

For every configuration record host/build/device identity, negotiated format,
dimensions, requested/achieved image rate, pixel steps/s, useful/transmitted
bytes and bitrate, codec/settings, CPU and wall time at each stage, RSS/reserved
buffers, queue depth/high-water mark, timeouts, lost/incomplete/skipped images
and cancellation time. Separate capture, graph, encode, transfer, decode and
display costs. A fast bulk transport control does not qualify the scalar graph.

Measure same-clock stage latency and total queue residence distributions.
Cross-host one-way capture-to-display latency needs qualified clock mapping or
an optical/common-time experiment; do not subtract unrelated local timestamps.
Report round trips as round trips and submission as submission, not presentation.
For raw/LZ4/Zstd, assert exact pixels against deterministic reference images.
For JPEG/H.264, report PSNR/SSIM and visible artifacts against the same source
sequence; agree useful quality/latency thresholds from V0/V3 measurements rather
than inventing a universal compression ratio or real-time deadline.

Negative tests cover wrong dimensions/channel order/rate/codec, arithmetic
overflow, short/oversized/strided/multi-plane buffers, native format changes,
missing metadata, absent timestamps, disconnect, slow/stopped receiver, bounded
replay/assembly pools, mid-image failure, cancellation and callback cleanup.
Pure fake-device tests run by default; live capture/screen access is an explicit
operation. No webcam recordings/images are committed as test fixtures.

## 9. Sources used in this plan

Accessed 2026-11-18. Upstream API facts above are separate from S7 proposals and
local read-only/compile evidence. No upstream performance claim is treated as
an S7 benchmark.

- **P1:** [PipeWire tutorial 5: capturing video frames](https://docs.pipewire.org/page_tutorial5.html).
- **P2:** [PipeWire Streams: directions, negotiation, buffer ownership](https://docs.pipewire.org/page_streams.html).
- **P3:** [PipeWire video producer example](https://docs.pipewire.org/video-src_8c-example.html) and [video consumer/viewer example](https://docs.pipewire.org/video-play_8c-example.html).
- **P4:** [XDG ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html).
- **P5:** [XDG Camera portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html).
- **P6:** [PipeWire SPA buffers and memory types](https://docs.pipewire.org/group__spa__buffer.html).
- **P7:** [SPA header timestamp and sequence semantics](https://docs.pipewire.org/structspa__meta__header.html).
- **W1:** [Microsoft: audio/video capture in Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/audio-video-capture-in-media-foundation).
- **W2:** [Microsoft: DirectShow status and recommended alternatives](https://learn.microsoft.com/en-us/windows/win32/directshow/about-directshow).
- **W3:** [Asynchronous Source Reader](https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-source-reader-in-asynchronous-mode).
- **W4:** [IMF2DBuffer](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imf2dbuffer).
- **W5:** [IMF2DBuffer2](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imf2dbuffer2).
- **W6:** [Source Reader video processing limitations](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-enable-video-processing).
- **W7:** [Windows.Graphics.Capture](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture?view=winrt-22000).
- **W8:** [MFCreateVirtualCamera requirements and lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera).
- **C1:** [libjpeg-turbo upstream/API overview](https://github.com/libjpeg-turbo/libjpeg-turbo).
- **C2:** [FFmpeg codec options and optional implementations](https://ffmpeg.org/ffmpeg-codecs.html).
- **C3:** [Media Foundation H.264 encoder](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder).
- **C4:** [GStreamer x264 encoder buffering/low-latency tradeoffs](https://gstreamer.freedesktop.org/documentation/x264/index.html).

Relevant inspected implementation: `s7hardware/include/s7/hardware.hpp`,
`s7hardware/src/hardware.cpp`, `s7hardware/tools/s7hw.cpp`,
`s7audio/src/pipewire.cpp`, `s7net/src/net_frames.cpp`,
`s7transport/include/s7/frame_transport.hpp`, and the
[Windows toolchain](../cmake/toolchains/x86_64-w64-mingw32-clang.cmake).
The current [hardware contract](../../doc/S7_Hardware_IO_Contract.md),
[sample-rate model](../../doc/S7_Sample_Rate_Model.md),
[frame elements](FRAME_ELEMENTS.md) and [Windows build guide](WINDOWS_BUILD.md)
remain the baseline; video-specific additions above are proposed.
