# Full HD whole-frame experiment with FFmpeg

Measured 2026-11-18, following the maintainer's request to try Full HD and use
FFmpeg. Native 1920×1080 synthetic video sustained the nominal 30 fps input
between `al1` and `win`, including a 1,800-frame / 60-second-media run. A separate
real-camera run captured 1280×720 MJPEG and explicitly upscaled to 1920×1080;
it did **not** establish native Full HD camera capture or 30 fps camera delivery.

These are external media experiments for the
[whole-frame design](VIDEO_IO_FRAME_DESIGN.md), not implemented S7 frame ports,
native `VideoSource`/`VideoSink`, a Studio viewer or a new S7 transport backend.
Production [GStreamer adapters are a 0.3.0 goal](RELEASE_0.3.0.md#gstreamer-adapters).
The temporary PipeWire capture/screenshot bridges below remain test tooling.

## Configuration and verification

Both hosts run Fedora 45, kernel `7.3.3-300.fc45.x86_64`, FFmpeg 9.0.2. Sender
`al1` has a Ryzen 7 4700U / eight CPUs; receiver `win` has an Intel N100 / four
CPUs. `win` is currently Linux despite its hostname. The two-host path was:

```text
FFmpeg testsrc2 OR PipeWire MJPEG bridge -> FFmpeg scale if requested
  -> libx264 -> MPEG-TS -> SSH stdin -> libopenh264 -> whole-frame verification
                                                -> optional FFplay window
```

H.264 settings: user-selected 6,000 kbit/s target, `ultrafast`, `zerolatency`,
baseline, GOP 30, no B frames, two encoder threads; decoded format YUV420P.
The receiver checks every decoded frame's dimensions and strictly increasing
PTS, computes SHA-256 frame hashes, and saves the final frame as PNG. Sender
and receiver hash the entire encoded stream independently and compare byte
counts and hashes. FFmpeg reported zero duplicated/dropped frames in all five
successful runs. This covers the admitted encoded/decoded sequence; it does
not count frames lost before admission by the camera or presented by FFplay.

The viewer uses software SDL in a private Xvfb display on `win`, which has no
logged-in desktop. Screenshots confirmed the color test image and grayscale
receiver output. This establishes virtual-window rendering, not physical-screen
presentation or presentation of every frame. The checker and viewer decode
separately, adding work compared with a shared production frame pipeline.

## Results

All five cases passed: **3,000 complete decoded frames**, matching stream
digests and exact output dimensions. Raw records, commands, run-specific tool
digests, final progress reports and private artifact locations are in
[VIDEO_IO_FULL_HD_RESULTS.json](VIDEO_IO_FULL_HD_RESULTS.json).

| Input / receiver operation | Frames | Sender wall, s | Receiver wall, s | Encoded MB | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Native 1080p synthetic / checker | 300 | 9.630 | 9.695 | 7.926 | Passed |
| Native 1080p synthetic / checker + viewer | 300 | 9.637 | 10.955 | 7.926 | Passed |
| Native 1080p synthetic / checker, stability | 1,800 | 59.637 | 59.716 | 46.843 | Passed |
| 720p camera upscaled / checker + viewer | 300 | 20.526 | 20.697 | 7.787 | Passed; below 30 fps |
| Native 1080p synthetic / grayscale + checker + viewer | 300 | 9.632 | 10.800 | 7.926 | Passed |

The 60-second-media run used 28.044 CPU seconds for encoding and 35.623 CPU
seconds for decoding/checking: approximately 0.47 and 0.60 CPU cores averaged
over their respective wall intervals. Peak RSS was 137.6 MB for the sender and
144.6 MB for the checker. Encoded bitrate was 6.246 Mbit/s per nominal media
second, including MPEG-TS framing and excluding SSH overhead. The checker
includes frame hashing and final PNG generation; viewer/SSH/bridge CPU is not
included in those process figures. These are software results on these hosts,
not a hardware-codec or zero-copy measurement.

FFmpeg's paced synthetic source emits short runs slightly faster than their
nominal duration. Report this as keeping up with a nominal 30 fps input, not a
31 fps capacity result. Receiver progress for this two-output checker appeared
only near EOF because the PNG output selects the final frame; it cannot provide
a steady-window FPS or per-frame latency trace. The final counts and elapsed
intervals qualify only this bounded throughput experiment.

The camera bridge requested 720p/30, but the run delivered 300 images in
20.526 seconds including startup (**14.62 images/s**); FFmpeg reported 16.52 fps
over its active interval. A separate PipeWire-to-fakesink control, without
FFmpeg/encoding/network, accepted 150 buffers in 10.458 seconds (**14.34/s**
including startup). This is consistent with capture limiting the real-camera
case; it does not establish whether exposure, device or PipeWire behavior is
responsible. MJPEG over this temporary pipe loses original capture timestamps
and assigns nominal 30 fps PTS; those PTS must not be treated as camera time.

The receiving grayscale operation ran on complete decoded frames. An independent
PNG check found identical R/G/B values throughout the final 1920×1080 image and
a nonconstant intensity range. Numeric-result bridges remain unimplemented.

## Reproduction and local checks

No installed services, S7 schemas, listener/firewall settings or packages are
modified. Scripts use private temporary directories and owned process groups;
the optional virtual display is staged temporarily on the remote host. Live
camera use occurs only with the explicit `camera-upscaled` option. The currently
selected camera node is specific to this lab. Camera snapshots stay in `/tmp`
and are not repository fixtures.

```bash
python3 -B tools/run_ffmpeg_video_spike.py --host win.local --key-alias win \
  --output /tmp/s7-fullhd-new --images 1800
python3 -B tools/run_ffmpeg_video_spike.py --output /tmp/s7-fullhd-gray-new \
  --images 300 --processing grayscale --view
python3 -B tools/run_ffmpeg_video_spike.py --output /tmp/s7-fullhd-camera-new \
  --source camera-upscaled --images 300 --view
python3 -B tools/test_ffmpeg_video_spike.py
```

Output directories must be new. The driver also exposes explicit raw/JPEG/H.264,
bitrate and JPEG-quality choices. The no-device test exercises all three codecs
on this installation at 320×240, checks raw pixels against a separate generated
reference, validates whole-frame grayscale, and rejects invalid geometry,
incomplete streams and wrong decoded dimensions. Raw/JPEG are not qualified as
Full HD remote performance profiles by these checks. Tests require FFmpeg;
H.264 requires the explicitly selected `libx264` encoder and `libopenh264`
decoder, and is skipped if those are unavailable. FFmpeg remains an optional
experiment dependency, not a new build requirement.

Two unsuccessful approaches informed the current driver. A GStreamer GDP pilot
did not complete receiver EOS and was not accepted. On these Fedora FFmpeg
builds, H.264-in-NUT failed with the selected `libopenh264` decoder; MPEG-TS
worked, so H.264 explicitly uses MPEG-TS while raw/JPEG use NUT. There is no
silent codec fallback. Failed pilot logs remain private in `/tmp`; the abandoned
GDP driver was removed.

## Limits and next implementation decisions

This experiment bypasses numeric per-pixel graph stepping. It supports the
whole-frame direction, but is not an isolated, like-for-like speedup comparison
against the [V0 numeric path](VIDEO_IO_SPIKE.md). SSH provides this experiment's
transport/security; no S7 media-session authorization or wire contract has been
implemented or qualified. D0/D1/D2 ownership stays with teren_ukr.

Before production, review the frame ownership/execution and authorized media
session contracts, preserve actual capture timing, measure bounded queue
behavior/copies and cancellation/failure recovery, and qualify a real viewer.
Quality thresholds, PSNR/SSIM, hardware acceleration, screen capture and native
Windows capture/viewing remain open. No cross-host one-way latency is claimed;
that requires a qualified clock mapping or common-time measurement.

## Sources

Upstream references consulted 2026-11-18; local measurements above are separate
from upstream API facts. FFmpeg provides the device, codec and whole-frame filter
interfaces evaluated here:
[devices](https://ffmpeg.org/ffmpeg-devices.html),
[codecs](https://ffmpeg.org/ffmpeg-codecs.html),
[filters](https://ffmpeg.org/ffmpeg-filters.html).
The installed FFmpeg build exposes no PipeWire input, hence the temporary
GStreamer/PipeWire capture bridge rather than an invented FFmpeg device.
The future production adapter goal cites
[appsink](https://gstreamer.freedesktop.org/documentation/app/appsink.html),
[appsrc](https://gstreamer.freedesktop.org/documentation/app/appsrc.html) and
[buffer ownership](https://gstreamer.freedesktop.org/documentation/additional/design/buffer.html).
