# Numeric video over two S7 hosts — V0 spike

**Subsequent design clarification:** whole-frame video processing and specialized
media transfer are now allowed/preferred for the production path. The
[revised design](VIDEO_IO_FRAME_DESIGN.md) preserves numeric computation through
explicit bridges. The per-pixel paths in this report remain measured baselines.

This is an **opt-in research driver**, not shipped `VideoSource`/`VideoSink`
providers. It implements the first experiments in the
[video plan](VIDEO_IO_PLAN.md): existing numeric pixels, real scalar graph
execution, existing authenticated frame transport, and receiver image assembly.
PipeWire capture/output is accessed through GStreamer's existing adapters for
this spike; the planned independent native C++ providers remain V1 work.
No core type, evaluator, wire format, control authorization or deployment
semantics are changed. D0/D1/D2 remain teren_ukr's work.

## Observed results — 2026-11-18

The [machine-readable record](VIDEO_IO_SPIKE_RESULTS.json) contains **16 passing
two-host cases**, 2,790 checked images, 70,848,000 pixel sample frames and
134,784,000 ordinary graph evaluations summed over both hosts. Both `al1` and
`win` ran Fedora 45 / Linux 7.3.3 x86-64. Each case used a fresh stream identity
and mTLS credentials. All image digests matched and final consumption was
acknowledged. Raw camera output and a later 320×240 Zstandard camera output
were visually verified in actual viewer windows on the receiver's isolated
virtual display. Six saved PNG artifacts were decoded successfully.

The following are single-run synthetic capacity results. Image contents match
across all nine 160×120 mode/codec combinations. Rates use the slower of the
sender and receiver processing intervals; CPU columns are total process CPU
seconds for sender/receiver, including transport threads.

| 160×120, 60 synthetic images | Raw images/s | LZ4 images/s | Zstd images/s | Raw CPU s (A/B) |
| --- | ---: | ---: | ---: | ---: |
| Per-pixel network elements | 2.56 | 2.61 | 2.56 | 25.990 / 0.918 |
| Scalar graph, batched owner | 83.41 | 54.67 | 90.37 | 0.375 / 0.281 |
| Bulk transfer, no graph | 53.60 | 52.51 | 51.20 | 0.130 / 0.091 |

These short, single-run timings have scheduling/batching variability. In
particular, the higher staged raw/Zstd rates do not establish that adding graph
work makes transport faster. The robust finding is the large cost of per-pixel
endpoint calls. Batch the handoff while retaining scalar graph evaluations;
repeat/extend measurements before ranking codecs or claiming peak capacity.

Further staged raw capacity checks achieved **20.16 images/s at 320×240** and
**4.75 images/s at 640×480**. The ordinary graph still executes one step per
pixel; 640×480/30 requires 9.216 million steps/s and is not qualified by this
spike. Compression does not remove that graph cost. A paced 160×120 test
completed **1,800 images in about 60 seconds**, approximately 30 images/s,
without stream faults and with identical reconstructed pixels.

| Live camera, staged graph | Checked images | Pipeline images/s | Forward TLS bytes | Receiver consumer images observed |
| --- | ---: | ---: | ---: | ---: |
| 160×120, raw | 90 | 14.03 | 7,009,784 | 82 |
| 160×120, LZ4 | 90 | 14.02 | 5,774,030 | 84 |
| 160×120, Zstd | 90 | 14.01 | 5,045,713 | 84 |
| 320×240, Zstd | 90 | 13.93 | 20,375,334 | 85 |

Live trials capture different images, so this is not a controlled same-content
codec comparison. The synthetic cases provide that control. Camera adapter
waiting dominated the live 160×120 path; the experiment does not identify the
physical sensor's exact acquisition cadence or lost-frame count. Preview
startup/complete-image skipping is separate from the **90/90 verified S7 images**
in each live case. Receiver window rendering was software/Xvfb, not a logged-in
physical desktop or native Windows run.

Verification: the spike target built successfully, and **4/4 focused CTests
passed**, including nine no-device mode/codec cases plus explicit short-image
and cancellation rejection. The other three tests cover the existing frame
service, socket transfer and scalar frame elements. Python syntax, document
links and `git diff --check` were checked. No full GUI/platform/package suite was
rerun for these opt-in research tools. Raw development logs/artifacts are under
`/tmp/s7-video-v0-matrix*`, `/tmp/s7-video-v0-followup.log` and
`/tmp/s7-video-focused-ctest-final.log`; live pictures are not checked in.

## Data and execution paths

Application data is top-down BGRA8: four U8 channels, one pixel per ordinary
graph evaluation, `width * height * image_rate` exact model sample rate. The
first webcam configuration explicitly requests native YUY2 at 160×120/30 and
uses GStreamer `videoconvert` to produce BGRA. It does not pretend that YUY2
bytes already represent RGB. This adapter does not carry native frame metadata
through its FIFO, so it cannot qualify acquisition timestamps, native sequence
continuity or camera loss counts.

The driver has three deliberately different modes:

| Mode | Computation and transfer | Interpretation |
| --- | --- | --- |
| `scalar` | Real `Compiler`/`NodeRuntime`, spike raster element and existing `NetFrameSink` / `NetFrameSource`; a network API call for each pixel | Baseline cost of direct integration with the existing per-sample network elements |
| `staged` | Real `Compiler`/`NodeRuntime` steps each pixel between numeric raster ports; the explicit outer owner receives/publishes complete images in bounded packet batches | Candidate for the feedforward video path; transport calls are outside the per-pixel loop, while logical evaluations remain scalar |
| `bulk` | Existing frame endpoint transfers image pixels in bounded packet batches without a graph | Device/transport control only; its rate is not a scalar graph result |

Staged mode does not claim equivalence for arbitrary feedback or interactions
between multiple network streams. It changes when this explicit owner performs
external transfer relative to its pixel evaluations. It is a research candidate,
not a replacement for general `NetFrameSource`/`NetFrameSink` behavior. A later
production run-owner design must retain its explicit boundary, failed-prefix
and buffering contract. No callback or buffer is treated as causal memory.

Each image spans as many existing numeric packets as necessary. The initial
packet size is 4096 pixels with an eight-packet window; all payloads remain
within the existing 256 KiB bound. FIFO reads/writes have deadlines. A short
image fails rather than being padded or spliced into the following image.
Input/output/assembly storage, OS FIFOs and frame pools are bounded; this is not
yet production-wide admission or a complete measurement of backend queues.

Sender and receiver compute SHA-256 over **all reconstructed BGRA bytes**,
including alpha, and compare their final RGB snapshots. Synthetic trials also
check every receiver image against an independently generated pattern. Tiny
uneven-image tests exercise packets crossing image boundaries, a one-packet
window and the final partial packet.

The graph and transport stay alive until the receiver reports completion and
the final consumption ACK arrives. An early spike version closed its graph
after observing zero replay depth; that discarded its still-unsealed tail.
Zero replay depth alone does not prove an empty application staging buffer.
The corrected driver tests final-image completion without changing production
close/abort behavior.

## Capture, output and viewing

`--source camera` explicitly opens the selected webcam. The first source is
`al1`'s UVC camera; `win` is a Linux receiver. Each run creates a named PipeWire
video source on the receiving host when `--pipewire-output` is selected. A
separate GStreamer consumer decodes images from that node into a rolling PNG.
The display consumer can miss complete images during startup; its count is
reported separately from the verified S7 image count. It never drops pixels
from the graph or changes its hashes.

`--virtual-display` additionally starts an isolated Xvfb on the receiver,
renders the consumer's video window and captures its screen. `win` had no
logged-in user desktop during the initial inventory. This verifies software
window rendering on host B; it does **not** qualify a physical monitor, a native
desktop/GPU path or capture-to-display deadlines. Preview shutdown after the
PipeWire producer exits can report a target-not-found error; success requires
multiple completed consumer images before that shutdown. Normal data-path or
adapter failure is not accepted as a successful run.

For this spike, all raw webcam images, PNGs and screen captures stay in the
chosen private output directory under `/tmp`; do not commit them as fixtures.
Only metrics, source/binary/dependency hashes and certificate fingerprints
belong in the checked-in results. Fresh per-run mTLS certificates/keys live in
temporary directories; the CA key is not written. The remote stage and its
owned processes are removed after each run. No installed service or firewall
configuration is changed. The optional Xvfb binary/dependencies are staged in
the same temporary directory rather than installed on the remote host.

## Compression and performance interpretation

`--encoding raw|lz4|zstd` is a **user choice**, passed unchanged to the existing
frame codec. Both roles use the same immutable manifest. Unsupported codec,
mode, dimensions or rate fail before streaming. JPEG/H.264 are not implemented
by this spike and are never mislabeled as lossless numeric encodings. They
remain separately tunable later video-codec work in the plan.

`--unpaced` measures synthetic capacity without changing the declared model
rate. The default synthetic run is paced at the requested image rate. Actual
camera trials are paced by capture availability. Source and receiver processing
intervals differ, especially when starting a viewer; report both and use the
slower rate for conservative pipeline comparisons. These trials include only
one repetition per matrix case; they do not establish a general performance
envelope. Capture-side waits can include camera/server/conversion/queue effects.

The byte-counting proxy measures complete forward/reverse TLS flows, including
handshakes and control connections. It cannot see plaintext and excludes
TCP/SSH/link headers. This is a total trial byte count, not a codec-only size or
a direct-LAN capacity measurement: remote runs use a loopback-bound SSH reverse
tunnel. CPU seconds include the node process's transport threads; peak RSS is
process high-water usage, not the application's reserved frame budget. Output
GStreamer/Xvfb CPU and RSS are not included in the S7 process counters.

No cross-host one-way latency is inferred by subtracting unrelated timestamps.
The initial measurements record owner wall/CPU/graph-or-transfer time, step and
image counts, peak RSS, stream faults and total TLS bytes. Stage-latency
distributions and native camera continuity need subsequent instrumentation.

## Reproduction

Build the opt-in test driver (normal application builds do not include it):

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target s7tests_video_stream_spike --parallel 2
ctest --test-dir build-release -R '^(s7tests_video_stream_spike|s7net_frame_elements_smoke|s7transport_frame_smoke|s7transport_frame_socket_smoke)$' --output-on-failure
```

The no-device video test uses Python `cryptography` to generate isolated test
credentials and tests every built raw/LZ4/Zstd option in all three modes. It
skips explicitly if that Python dependency is absent. Existing net/transport
tests must already be built to run the complete focused selection.

Small two-process test, no device or GUI access:

```sh
python3 -B tools/run_video_stream_spike.py \
  --binary build-release/s7tests/s7tests_video_stream_spike \
  --output /tmp/s7-video-local-check --width 17 --height 13 --images 5 \
  --packet-pixels 19 --window-packets 1 --mode staged --encoding zstd
```

Explicit live webcam, two hosts, PipeWire output and a temporary receiver window:

```sh
python3 -B tools/run_video_stream_spike.py \
  --binary build-release/s7tests/s7tests_video_stream_spike \
  --output /tmp/s7-video-live-check --host win.local --key-alias win \
  --source camera --mode staged --encoding raw --images 90 \
  --camera-target v4l2_input.pci-0000_04_00.4-usb-0_3_1.0 \
  --pipewire-output --virtual-display
```

The remote host needs compatible Linux runtime libraries, SSH access and the
GStreamer PipeWire/rawparse/video-conversion/PNG/X11 plugins. The sending host
needs those capture plugins plus Xvfb for the optional temporary display.
No package install, interactive login or reboot is performed by the scripts.
The default camera selector is a specific observed test device, not a portable
device identity; explicitly change it for another machine. Initial camera
requests use the observed 30-fps YUY2 mode; arbitrary scaling/rate adaptation is
not silently inserted.

The sequential matrix runs nine synthetic capacity cases, optionally three
camera/output cases, and a paced 1800-image stability case:

```sh
python3 -B tools/run_video_stream_matrix.py \
  --binary build-release/s7tests/s7tests_video_stream_spike \
  --host win.local --key-alias win --output /tmp/s7-video-matrix \
  --camera --virtual-display
```

Every output directory must be new. `result.json`/`summary.json` retain failures
as well as successes; inspect the role/adapter logs instead of discarding a
failed attempt. Synthetic capacity cases use identical reference images across
codecs/modes. Separate live camera trials naturally have different contents.
The recorded matrix and subsequent larger-image checks retain their exact
driver/binary hashes. The later viewer check additionally asserts that its
consumer stays alive until the controller deliberately stops the producer.

## Sources and next boundary

The [video plan bibliography](VIDEO_IO_PLAN.md#9-sources-used-in-this-plan)
records the upstream PipeWire, GStreamer/codec and Windows research. The
[existing frame element contract](FRAME_ELEMENTS.md) and
[hardware contract](../../doc/S7_Hardware_IO_Contract.md) remain the baseline.
GStreamer element properties used here were also inspected on the actual hosts
with `gst-inspect-1.0`; no GStreamer or vendor source was modified.

Next is the reviewed V1 provider/run-owner implementation, using the measured
handoff choice. This spike does not add SDK/plugin registration, normal Studio
Run/service activation, portal screen capture, JPEG/H.264 framing or native
Windows camera capture. It supplies executable evidence before those changes.
