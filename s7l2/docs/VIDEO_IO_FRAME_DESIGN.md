# Frame-oriented video: revised design direction

Implementation steering, 2026-11-18: evaluate FFmpeg for the current video path.
Production GStreamer adapters are a separately assigned
[0.3.0 release goal](RELEASE_0.3.0.md#gstreamer-adapters). Temporary GStreamer
capture/test bridges do not count as shipping that integration.

The [Full HD FFmpeg experiment](VIDEO_IO_FULL_HD.md) now records five passing
two-host cases, including 1,800 native 1080p synthetic frames at nominal 30 fps,
receiver grayscale and explicitly upscaled 720p camera input at a lower rate.
It is media-pipeline evidence; the S7 contracts proposed below remain unimplemented.

Status: **proposed implementation contract, 2026-11-18**. The maintainer
clarified that video transfer may use specialized media techniques, numeric
streams remain appropriate for computation chains, receiver-side elements may
transform video, and video processing normally needs a **whole frame**. This
supersedes the per-pixel production baseline in the original
[video plan](VIDEO_IO_PLAN.md). Exact port, execution and transport contracts
remain to be reviewed and measured; this document does not implement them.

The [V0 spike](VIDEO_IO_SPIKE.md) remains historical evidence. Its scalar pixel
path is a useful cost baseline, not a requirement for subsequent video elements.
Changing the representation and evaluation granularity does not remove S7's
causal connections, explicit state, type validation or headless ownership.

## 1. Processing and transport units

The normal unit of video evaluation is one complete image, or later an explicit
tile/block where the algorithm benefits. An element receives a frame, performs
its operation and publishes a frame and/or numeric results. Pixel loops belong
inside that operation and may use vectorized CPU kernels or GPU APIs. There is
no general requirement to call `NodeRuntime::step()` once for every pixel.

For example, 640×480/30 implies 30 frame evaluations per second for a frame
element rather than 9.216 million pixel evaluations. Pixel processing and byte
volume still exist; this arithmetic is not a measured speedup or a promise of
30-image/s performance.

Proposed pipeline, with element names illustrative rather than registered APIs:

```text
capture host:
  VideoSource -> optional frame processing -> selected encoder -> video transport

receiving host:
  video transport -> decoder -> frame processing -> VideoSink / viewer
                                     |
                                     +-> numeric measurements -> computation chain
```

Examples of receiver processing are resize, color conversion, crop/region of
interest, filtering, feature detection and motion estimation. Algorithms needing
prior frames own that history explicitly. A scalar numeric result can be emitted
once per frame, or at a declared rate through an explicit conversion boundary.
Where numeric computation needs image arrays, process those arrays as frames or
blocks; do not automatically flatten them into per-pixel scalar evaluations.

Capture/transfer/viewing without image computation can stay entirely in the
media pipeline. If a camera provides the selected encoded format, an explicitly
negotiated passthrough path can avoid decode/re-encode when no operation requires
decoded images. Receiver processing decodes where necessary. Selecting such a
path must not silently skip processing elements present in the schema.

### Codec elements in `s7video`

`VideoDecoder` and `VideoEncoder` are representation boundaries around
the decoded-frame graph. The decoder takes encoded packets from an ingress
source and publishes complete decoded S7 frames. Image processing and Studio
display consume those frames. The encoder takes the processed frames and
publishes encoded packets for egress. Sources and sinks own device capture,
file I/O, container handling and network transport. The codec elements own
the conversion between encoded packets and decoded frames.

```text
Encoded source → VideoDecoder → decoded-frame processing → VideoEncoder → file or network sink
                                           └─────────────→ Studio Video Player
```

A source such as PipeWire can supply an encoded format such as MJPEG or H.264,
or publish a decoded frame directly when its raw layout matches the S7 input.
Typed packet connections are used at the encoded edges of the
pipeline. The initial codec provider links FFmpeg's `libavcodec`,
`libavutil` and `libswscale`. The production GStreamer media adapter has
its own qualification gate.

Each additional codec profile needs to identify its required initialization
data, bound the packet payload and queue, and preserve access-unit boundaries,
input timestamps and discontinuities. Profiles with buffered output need
explicit rules for output cardinality, EOS draining, cancellation and codec
state after a failed graph evaluation.

#### Initial MJPEG profile

The reference backend in `s7video` links FFmpeg directly and operates on
caller-owned spans. It supports compact Gray8 and RGB8 images with declared
width and height. The picture can have up to 4096 pixels per side and up to
8,388,608 pixels total. Each encoded packet carries one complete 8-bit,
independently decodable JPEG image, with matching dimensions and a byte
length bounded by the caller's capacity and the 32 MiB backend limit. The
length is the exact payload length including the final JPEG marker.

The approved MJPEG graph port carries a local typed packet lease and valid byte
length. The descriptor matches codec, width, height and capacity at compile
time. Backing storage is compact U8, and numeric port bindings require a
different descriptor. An encoder
or decoder evaluation produces one complete output or reports an error before
publishing. The backend currently starts fresh codec state on every call.
One complete packet can be retried after a graph failure, provided the
publisher still owns the input. The elements preserve source occurrence and
optional media timing in both directions. Timing carries signed presentation
ticks with a positive rational ticks-per-second time base and a discontinuity
flag. Graph domain time remains a separate axis.
H.264 requires separate review for buffering, reordering and EOS drain.

Set matching `packet_capacity`, `width` and `height` on the codec elements.
Each codec's `format` selects its decoded Gray8 or RGB8 side. To expose an
encoded port across a nested schema, declare `"type": "U8"`,
`"shape": [65536]` and `"packet": {"codec": "mjpeg", "width": 65, "height": 33}`
with the matching capacity and dimensions. A packet observer reads the exact
prefix via `ArrayValue::encoded_bytes()`.

Build and test the optional plugin against the current installed SDK. The
packet representation requires [plugin API v5](PLUGIN_INTERFACE.md).

```bash
cmake --install build-release --component s7sdk --prefix /tmp/s7-video-sdk
cmake -S s7video -B /tmp/s7-video-build -DCMAKE_PREFIX_PATH=/tmp/s7-video-sdk
cmake --build /tmp/s7-video-build
ctest --test-dir /tmp/s7-video-build --output-on-failure --no-tests=error
build-release/s7runtime/s7runtime --schema s7video/examples/schema_mjpeg_roundtrip.json --plugin-dir /tmp/s7-video-build --state-dir /tmp/s7-video-runtime-state
```

The smoke test checks odd-width grayscale and RGB pixel reconstruction to
a declared lossy tolerance. It checks truncation, concatenated images,
bad marker framing, wrong geometry and insufficient packet capacity.
The installed-plugin graph test covers a saved deterministic round trip,
a nested encoded port, rejected capacity and geometry mismatches, rejection
of a numeric U8 wire, media timing and rollback after a downstream fault.
The headless runtime loaded the installed plugin and executed five steps.
Studio showed the codec type and configured dimensions on the encoded wire.

#### Record and replay a raw MJPEG file

From a writable working directory, run the installed recorder with the absolute
paths to the plugin and recording schema:

```bash
s7video_record /path/to/libs7video-elements.so /path/to/schema_mjpeg_roundtrip.json encode 5 recording.mjpeg
```

The recorder appends the exact bytes of each committed output packet from port 0
of the named element. Use a graph that commits a packet at every step. The
destination must be new and its filesystem must support hard links. The
recorder publishes the destination only after every requested frame was written.

Run Studio from the working directory with the video plugin discoverable through
`S7_PLUGIN_PATH`. Open the installed `mjpeg_replay.s7diagram` and set the local
run to 25 Hz for 0.2 s. Open the Video Player to view the decoded output. For
headless replay use `schema_mjpeg_replay.json`. Both examples read
`recording.mjpeg` relative to the working directory.

Raw MJPEG stores a sequence of complete JPEG images. Set `frame_rate` to
reconstruct presentation time. The source starts at zero and increments one
tick per image at the configured rate. The `eof` output is 1 alongside the
last complete frame. Stepping past the end, a truncated image or a capacity
violation fails the step. Reopen the graph before retrying after a file source
fault.
On Fedora 45 with FFmpeg 9.0.2, the Release and Clang ASan/UBSan builds
passed. Both 65×33 reference frames had a maximum absolute error of
2 levels per channel. The existing no-device FFmpeg experiment also
passed its raw, JPEG and H.264 cases. The linked S7 backend qualifies MJPEG.
The Ubuntu installer was checked by dry run on Fedora.
The Fedora and Ubuntu development installers include the required FFmpeg
headers. Use `--no-video` for builds that do not include the standalone
backend. [Build dependencies](BUILD_DEPENDENCIES.md)

## 2. Frame ownership and integration

Propose a typed, immutable **frame lease** with geometry, pixel/color format,
plane layout/stride, segment and frame sequence, optional timing/provenance and
bounded owned storage. It may retain CPU memory or a supported native/GPU
surface. Metadata must distinguish a decoded image from an encoded access unit;
an H.264 packet is not interchangeable with an RGB frame.

Fan-out can retain references to the same immutable storage. An operation that
changes pixels obtains writable output storage. Backend buffer lifetime and
fences must remain valid until all consumers release their references. Returning
a PipeWire buffer and retaining its raw pointer is not an ownership mechanism.
GStreamer's buffer/memory design provides an established reference for retained
memory and copy-on-write behavior. [GStreamer buffer design](https://gstreamer.freedesktop.org/documentation/additional/design/buffer.html)

The current numeric runtime snapshots at most 32 bytes with `memcpy`, and its
signal catalog is also a numeric wire-type catalog. A frame lease is nontrivial
owned state: do not insert a shared pointer into those byte-copy paths or assign
an arbitrary numeric wire ID. The implementation design must specify typed
retain/copy/release and publication/rollback behavior, or use an explicit media
execution adapter with well-defined numeric bridges. Compare those integration
choices before changing public core interfaces. No global scheduler or solver
is required merely to choose frame granularity.

An evaluation publishes a complete output frame; a failed operation does not
publish a partially modified shared image. Previous-frame processing, startup
and initialization require explicit state contracts. Transport buffering does
not supply causal feedback memory. Multiple independent camera/audio clocks
still need explicit rate/timing policies; equal nominal rates do not align them.

## 3. Throughput candidates, in order

1. **Remove per-pixel graph and network overhead from the media path.** Compare
   whole-frame processing with the measured scalar/staged/bulk baselines using
   identical input content and repeated trials. Keep the numeric results path
   separate from pixel storage and transfer.
2. **Preserve useful native formats and minimize copies.** Avoid mandatory BGRA
   conversion and FIFO round trips. Reuse admitted frame pools; convert only
   when an element/encoder/display requires it. On Linux, evaluate PipeWire
   shared memory and DMA-BUF import with negotiated formats/modifiers and proper
   synchronization. DMA-BUF is local buffer sharing, not a network protocol;
   end-to-end zero-copy is conditional on all participating components.
   [PipeWire DMA-BUF negotiation](https://docs.pipewire.org/1.4/page_dma_buf.html),
   [GStreamer DMA-BUF integration](https://gstreamer.freedesktop.org/documentation/additional/design/dmabuf.html)
3. **Keep compression user-controlled.** Raw/lossless modes remain available.
   Evaluate JPEG as independent-image encoding and H.264 as stateful video
   encoding, with quality/bitrate/keyframe/latency controls and explicit software
   or hardware selection. An encoder unavailable on a host is a reported
   capability mismatch. Decoder references/keyframes and recovery are part of
   the segment contract, not invisible implementation details.
4. **Compare established media transport with frame-oriented TLS transfer.**
   GStreamer already provides H.264 RTP packetization and a WebRTC implementation;
   use those as candidates rather than creating another codec/packetization
   framework. Evaluate SRTP/WebRTC for a low-latency viewing profile and a bounded
   reliable profile for applications that need every accepted frame. Retain S7
   control authorization and bind any media session/keys to the authorized peers.
   New profiles and loss/recovery behavior require review; do not weaken the
   existing numeric stream's guarantees. D0/D1/D2 coordination stays with
   teren_ukr. [H.264 RTP payloader](https://gstreamer.freedesktop.org/documentation/rtp/rtph264pay.html),
   [GStreamer WebRTC](https://gstreamer.freedesktop.org/documentation/webrtc/)
5. **Overlap independent pipeline stages with bounded queues.** Capture,
   processing, encoding, transfer and display can have explicit asynchronous
   ownership. Bound retained surfaces and queue residence. User-selected live
   viewing can allow controlled whole-frame dropping; completeness-required
   processing must report backpressure/loss. Lossy compression and frame dropping
   are different choices. Do not infer a parallel graph scheduler from this I/O
   pipeline proposal.

Windows should use the same frame/codec contracts. Media Foundation capture
remains the source candidate; Direct3D surfaces and hardware codec support are
capability-dependent optimizations. Microsoft's Direct3D 11 integration describes
sharing the device between Media Foundation decoder and rendering components;
our existing compile-only capture probe does not qualify this GPU path.
[Media Foundation / Direct3D 11](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation)

## 4. Next experiments and evidence required

Run the same two hosts with:

- Whole-frame capture/transport/display, raw and selected JPEG/H.264 modes.
- A receiver frame operation, initially resize or grayscale, followed by display.
- A receiver frame-to-numeric measurement, with an independent reference result.
- CPU/shared-memory paths first, then supported GPU surfaces and hardware codecs;
  record actual transfers/maps/copies rather than assuming zero-copy.

Measure achieved images/s, stage wall/CPU time, transmitted bitrate, queue
residence/latency, retained memory, dropped/incomplete frames and quality for
lossy modes. Use long/repeated synthetic tests and real cameras separately.
Measure direct-link transport independently of the SSH tunnel used by V0 when
the host network permits it. Never obtain one-way latency by subtracting clocks
without a qualified mapping.

V0 does not prove that graph work is the sole bottleneck: its 640×480 staged raw
sender used about 2.41 CPU seconds over 6.28 wall seconds, and its bulk/staged
short-run ordering varied. Profile waits, flow control and transport along with
compute. Whole-frame evaluation removes a known source of repeated work; its
actual end-to-end benefit still requires these experiments.
