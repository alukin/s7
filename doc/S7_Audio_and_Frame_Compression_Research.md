# Audio transport and frame compression

Status: **research and recommendations, 2026-11-12**.
The maintainer accepted the transport-only [sample-frame contract](S7_Sample_Stream_Profile.md)
and ADR-025 on this date. Audio devices, adaptive clock matching, compressed
payloads and graph codec elements were follow-up designs outside that initial
approval. The maintainer subsequently approved the implementation direction.
Raw PCM/IQ and independent LZ4/Zstandard C++ services are now implemented; see the
[service/encoding guide](../s7l2/docs/FRAME_TRANSPORT.md). OS devices, adaptive clocks,
perceptual codecs and graph/deployment integration remain follow-up work.

## Findings

The frame model fits audio: one sample-frame contains one value for each named
channel at a source-grid position. A codec frame instead contains many such
sample-frames; neither is a graph step or necessarily a network packet.

Start with raw PCM for audio correctness and latency. Investigate optional
lossless block compression for exact numeric transport, and explicit audio
encoding/decoding boundaries for perceptual codecs. Keep their guarantees visible.
Codec selection should follow measurements on representative input, not a
universal compression ratio or the current Ethernet link speed.

## 1. Audio workloads and budgets

Calculated payload rates, excluding headers, security and replay:

| Workload | S7 values | Payload Mbit/s | Bytes per 1 ms |
| --- | --- | ---: | ---: |
| 48 kHz stereo PCM | I16 | 1.536 | 192 |
| 48 kHz stereo processing | F32 | 3.072 | 384 |
| 96 kHz, eight channels | I32 | 24.576 | 3072 |
| 48 kHz, 64 channels | F32 | 98.304 | 12288 |

Formula: rate × channels × bytes/value; double for symmetric full duplex.
The last row reaches the approved profile's initial channel-count limit.
Stereo F32 needs only 384 bytes for 1 ms, but a full 64 KiB buffer spans about
170.7 ms. Waiting for an SDR-sized buffer to fill is unsuitable for low-latency
audio. The existing wire contract's default maximum batching wait is 10 ms;
audio needs an explicit smaller time budget, independent of byte capacity.
Reasonable experiment points are 48, 128, 256 and 480 frames at 48 kHz, covering
1, 2.667, 5.333 and 10 ms. These are workload choices, not promised deadlines.

I16 and F32 already exist in S7. Packed 24-bit PCM has no scalar I24 wire type:
an adapter can explicitly unpack it to I32 while declaring valid bits,
alignment, sign extension and amplitude scale. Do not truncate it to I16 or
silently normalize integer values. Physical speaker roles and arbitrary
measurement-channel identities also need an unambiguous mapping.

| Application | Proposed starting point | Main qualification question |
| --- | --- | --- |
| Recording, acoustic measurement, distributed DSP | Exact PCM; optional lossless encoding | Are source samples, positions and gaps preserved? |
| Buffered listening and remote monitoring | Raw PCM first; optional Opus later | Is measured end-to-end latency acceptable? |
| Interactive instruments or network feedback | Small PCM blocks; explicit deadline/clock design | Can the complete loop meet its deadline and remain stable? |

Reliable ordered delivery and timely playback are separate properties. WebSocket
runs over TCP; ordered recovery can delay later media. Our inference is that
S7's current backend can be explored for buffered audio, but cannot promise a
hard playback deadline under arbitrary loss or stalls.
[WebSocket specification](https://www.rfc-editor.org/rfc/rfc6455.html).

RTP offers useful examples of separate packet sequence, media timestamps and
delivery reports, but does not itself guarantee timely or reliable delivery.
An RTP gateway would be a separate backend and security/semantic design, not an
automatic improvement to the accepted S7 delivery contract.
[RTP specification](https://www.rfc-editor.org/rfc/rfc3550.html).

## 2. Device clocks, callbacks and faults

Equal nominal rates do not make two devices synchronous. At 48 kHz, a relative
100 ppm mismatch accumulates 4.8 frames/s: 480 frames of buffer headroom are used
in roughly 100 seconds without correction. A jitter buffer absorbs finite
scheduling variation; it cannot permanently absorb frequency error. PipeWire
documents adaptive resampling when device and graph clocks differ.
[PipeWire resampling properties](https://docs.pipewire.org/page_man_pipewire-props_7.html).

Candidate policies are a shared physical clock, one explicitly selected device
master, or an explicit adaptive resampler between clock domains. PTP/UTC timing
alone does not prove that independent ADC/DAC oscillators are frequency-locked.
Keep acquisition position/time separate from host arrival and predicted playout.
Report any OS audio-server conversion/resampling; a no-conversion request still
needs validation against the actual negotiated device format and clock path.

Proposed device arrangement:

```text
capture callback -> bounded capture pool -> worker / transport
transport / worker -> bounded playout queue -> playback callback
```

PipeWire requires realtime-safe functions in an RT process callback. Therefore
S7's proposed callback path uses preallocated storage and bounded handoff;
network waits, codec work, heap growth and blocking locks belong outside it.
Keep OS-owned buffers only for their documented lifetime, copying if necessary.
[PipeWire stream API](https://docs.pipewire.org/group__pw__stream.html).

ALSA distinguishes capture overrun and playback underrun through its XRUN state.
An OS recovery helper restores device operation; it does not recover missing
samples. Translate these events into explicit S7 source/sink faults and segment
recovery, with unknown loss counts remaining unknown.
[ALSA PCM interface](https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html).

The approved strict profile faults on missing data. It does not silently drop
late frames, insert silence, repeat audio or enable packet-loss concealment.
Continuing playback with synthesized content is a possible *separate* opt-in
application policy requiring provenance and a reviewed discontinuity contract.
Physical device behavior after software stops also needs reporting.

Consumption ACKs remain consumption ACKs. Queue admission, graph consumption,
submission to the OS and actual DAC presentation are distinct milestones.
An audio adapter must define its consumption boundary and account its own queue;
receiving or decoding compressed bytes alone cannot acknowledge unconsumed PCM.
Do not claim that a transport ACK proves that sound was rendered.

## 3. Compression choices

| Candidate | Exact original sample bits? | Proposed use and limits |
| --- | --- | --- |
| Raw PCM/native IQ | Yes | Required baseline; predictable size and no codec work |
| LZ4 independent frames | Yes, for encoded input bytes | First general-purpose candidate to benchmark; bounded independent recovery units |
| Zstandard independent frames | Yes, for encoded input bytes | Compare ratio, CPU and workspace with LZ4; initially avoid dictionaries/history across packets |
| FLAC | Exact supported integer PCM | Audio recording/transfer candidate; no direct arbitrary F32-bit preservation; channel/format limits apply |
| Opus | No | Explicit perceptual audio path for voice/listening; unsuitable as transparent scientific/IQ compression |

LZ4 framing specifies independent/dependent blocks and an uncompressed block
representation. S7 should initially prefer independently decodable units and a
declared raw fallback when compression increases size. Do not equate an LZ4
frame or block with an S7 sample-frame.
[LZ4 frame format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md).

Zstandard exposes a worst-case compression-size bound and decoder window limits.
Reserve bounded output/workspace before admission. A streaming window limit
alone does not bound every decoder allocation or one-shot output buffer.
[Zstandard API](https://github.com/facebook/zstd/blob/dev/lib/zstd.h).

FLAC supports integer PCM with 1–8 channels and 4–32 bits/value. It does not
directly preserve arbitrary floating-point bit patterns. At 48 kHz a block of
4096 frames spans 85.3 ms; FLAC also permits much smaller blocks, trading
compression efficiency against buffering. Test short blocks rather than treating
typical file settings as a live-audio requirement. More than eight channels needs
a separately designed grouping/mapping; the 64-channel S7 profile does not make
a single FLAC stream support 64 channels.
[FLAC specification](https://www.rfc-editor.org/rfc/rfc9639.html).

Opus supports codec-frame durations from 2.5 to 60 ms and packets containing up
to 120 ms of media; mode choices constrain available frame durations. These
durations are not total end-to-end delay.
[Opus specification](https://www.rfc-editor.org/rfc/rfc6716.html).
Query codec lookahead instead of hard-coding it; encoder configuration and
implementation affect delay. Track priming and final padding separately from
valid media frames. Waiting for needed input changes availability; timestamp
compensation never makes output available earlier.
[Opus encoder controls](https://www.opus-codec.org/docs/opus_api-1.5/group__opus__encoderctls.html).

Opus decoding is stateful and requires ordered serial packet processing per
decoder. Reconnect cannot restart a fresh decoder at an arbitrary packet and
promise equivalent output. Its concealment facility is optional behavior and
does not override S7's strict fault policy.
[Opus decoder API](https://www.opus-codec.org/docs/opus_api-1.5/group__opus__decoder.html).
An RTP/Opus gateway also needs explicit timestamp conversion: RFC 7587 uses a
48 kHz RTP timestamp clock independent of the audio mode. Do not overwrite S7's
declared source-grid rate with a protocol-specific timestamp unit.
[Opus RTP payload](https://www.rfc-editor.org/rfc/rfc7587.html).

Noise-like SDR input may offer little lossless compression; correlated audio
may behave very differently. Treat this as a benchmark hypothesis. Preserve
NaNs, signed zero and all numeric bits for general lossless transport; converting
F32 audio to integer before FLAC is a separate explicit operation with its own
precision, clipping and dither policy. Do not promise a fixed compression ratio.

## 4. Source/sink architecture to investigate

The names below describe conceptual boundaries, not existing classes or new
graph port types:

```text
typed PCM/IQ -> compression sink -> encoded transport -> decompression source -> same typed values
typed PCM   -> Opus encode sink  -> encoded transport -> Opus decode source   -> reconstructed PCM
```

Both can reuse bounded block ownership, sequence/replay, resource accounting
and device workers. Keep capture/playback, codec state and transport lifecycle
separate internally; expose codec selection and its semantic effects explicitly
on a source/sink pair. A pure encoder/decoder graph element would additionally
need a reviewed variable-length encoded-block value contract; scalar U8 ports
do not supply that contract by themselves.

The accepted `sample_frames_v1` payload is numeric frame-interleaved data. It
cannot carry compressed bytes without changing its decoding/length contract.
Investigate a separate explicitly advertised encoded-frame profile or reviewed
encoding extension; this research assigns **no wire IDs or final field schema**.
Before implementation decide:

- Immutable codec/configuration, decoded type, valid bits, channel map and rate;
  distinguish exact and perceptual fidelity. Meaning changes require a new stream.
- Media first-frame coordinate and decoded frame count, independent of compressed
  byte length, codec-frame count and packet sequence. Define priming, padding,
  codec delay, event offsets and restart dependencies explicitly.
- Validate encoded bytes, decoded dimensions/products, workspace/window and total
  retained bytes before allocation. Cap both encoded and decoded sizes; a small
  compressed packet cannot bypass the current 256 KiB decoded-payload budget.
  Any future larger decoded unit needs its own explicit limit decision.
- Reserve capacity for the whole chosen codec unit before advancing encoder
  state. Report accepted whole sample-frames; never advance state for a suffix
  that was not accepted. A partially consumed decoded unit stays retained.
- Retain exact sealed encoded bytes for replay. Deduplicate before stateful
  decoding, never decode a replay twice, and never silently recompress a replay.
  Decoder reset requires an agreed restart point/new segment and required state.
- Account encoded replay, decoded PCM, codec history/workspace, in-flight worker
  output and retained leases. Receiver admission must cover worst-case decoded
  outstanding data, not an optimistic average compression ratio.
- Bound codec job queues and preserve per-stream ordering. Define cancellation,
  shutdown and failure after codec-state advancement; graph effects still need
  the separate T4 commit/fault decision.
- Keep terminal fault markers outside the data codec so a source failure can be
  signalled without inventing padding audio to finish a codec unit. Define the
  valid-prefix rule if acquired input has not yet become a sealed encoded unit.
  Finite end/flush/trimming is a separate contract, still open in the raw profile.

## 5. Small research sequence

1. Extend the synthetic transport harness for I16/F32, 48/96 kHz, multiple channel
   counts, small blocks and duplex. Keep packet batching, device quantum and
   logical frame rate distinct. Report first/last-frame latency and queue age,
   including capture accumulation, codec delay and playout buffering.
2. Benchmark raw, LZ4 and Zstandard on silence, tones, impulses, correlated signals
   and seeded noise. Measure ratio including framing, encode/decode p50/p99/max,
   CPU, allocation/workspace, raw fallback and cancellation. No speed claims yet.
3. Compare FLAC for exact integer audio and Opus for explicit listening use.
   Use bitwise round trips for lossless paths; Opus needs timing, waveform-error
   and listening/quality criteria, not equality to its original PCM.
4. Use virtual independent device clocks first: positive/negative drift, bounded
   jitter, stalls beyond queue capacity, missed deadlines and clock resets. Then
   try an OS audio backend with measured negotiated formats and latency.
5. Test malformed/truncated/oversized compressed data, expansion, partial reads,
   duplicate replay, reconnect with/without decoder state, faults mid-codec-unit,
   channel mapping and shutdown during blocked work. Add shared fixtures before
   any cross-language capability advertisement.

Recommended order: raw audio validation, independent lossless compression,
then perceptual audio boundaries. Adaptive resampling, deadline-loss policies,
encoded graph values and new wire fields each need a concrete follow-up decision.
This research does not assign release scope. The linked C++ implementation leaves
production graph semantics and device settings unchanged.
