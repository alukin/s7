# C++ frame transport and lossless compression

Implemented 2026-11-12 as an opt-in, socket-independent service in
[`frame_transport.hpp`](../s7transport/include/s7/frame_transport.hpp).
This is the initial C++ transport slice of ADR-025 and the maintainer's approved
raw-audio/lossless-compression direction. The [scalar graph elements](FRAME_ELEMENTS.md) now expose this service in the
headless registry and Studio, with bounded endpoint batching and a failed-domain
notification that latches frame boundaries. Automatic deployment/capability
negotiation, OS audio devices, adaptive resampling and perceptual codecs remain
follow-up work. Logical sample evaluation and causal ordering are unchanged.

## Services and ownership

`FrameSink::publish_copy` seals interleaved native numeric values;
`publish_shared` takes one immutable block of portable numeric bytes and retains
its input lease until ACKs release the replay record. It still makes a sealed
packet copy and does not promise zero-copy networking. Partial publication returns
the exact whole-frame prefix and status. Callers retain/retry the remaining suffix,
rebasing events. Cancellation returns `Cancelled`, not success.

`FrameSource::front_packet` exposes a retained payload and frame metadata before
processing; `consume` advances consumption explicitly. `receive_copy` decodes to
caller-owned native storage and consumes a prefix of one packet. Positions/events
are rebased for partial reads. A read reaching a terminal returns the valid prefix,
typed fault and optional exact lost-frame count; absent means unknown.

Receipt advances `next_expected`; consumption advances `consumed_up_to`. Send
the latter as an ACK. RESUME does not turn receipt into consumption. Disconnected
consumers retain their ACK positions; slow consumers prevent trimming. Replay
keeps exact sealed bytes. This first API uses one fixed transport alias per
service instance, agreed by its connection owners, including all fan-out peers.

Producer loss freezes admission and appends an uncompressed terminal using a
separate 256-byte slot and preallocated extra log entry. The valid prefix drains
first. Recovery requires every registered peer to consume the terminal, release
outbound terminal leases, and an authorized owner to supply a new segment boundary
to both services. `recover` itself is not an authorization mechanism. Sink underflow
stops consumption locally and never injects producer packets.
Both services keep the latest 16 fault records across recovery. Recovery of a
stream whose peers cannot consume its terminal requires an explicit owner-level
replacement/abandonment decision; this API does not silently discard that log.

Publication is nonblocking: exhaustion returns `Backpressure`. The owner supplies
device deadlines and decides when actual continuity loss requires `fault`.
There is no hidden worker, drop policy, synthesis or automatic recovery. Close
rejects further operations; the owner stops/joins socket/device workers before
destroying the service. SequenceStore sharing requires owner-level serialization.

## Resources

Use dedicated send/receive `BufferPool`s with at least `window_packets` slots.
Send slots hold `kMaxMessageBytes`; receive slots hold `kMaxPayloadBytes`. This
reserves the worst-case outstanding window. Values remain in portable packed
storage until copied to caller-owned native storage; CI12 expansion occurs there.
Descriptors bound channels (64), frames, events (32), metadata and both encoded
and decoded payloads (256 KiB). Unsupported event schemas are rejected.

Share one `BufferBudget` across node pools. Service scratch, static codec workspaces,
fixed packet-ring storage and the terminal buffer also reserve this budget.
Retained slices keep allocations charged after consumption or pool-handle
destruction. Shared input allocations cannot exceed 256 KiB; their originating
pool owns that charge and should use the same node budget. All packet rings and
bulk codec workspaces are preallocated, with no cross-packet compression dictionary.

This measures reserved buffer/workspace capacity, not process RSS. Allocator and
control-block overhead, configured descriptor/peer bookkeeping, application
arrays and socket/TLS queues need separate owner-level bounds. Peers are capped
at 64. Runtime admission will need to account for those resources before the
profile is advertised. `BufferPool::wait_copy` has a deadline/stop token;
lease release or close wakes waiters, and existing leases remain readable.

## Initial encoding contract

Raw framing follows accepted `sample_frames_v1`, critical extension `0x8002`,
including its 56-byte fixed portion and clock-anchor/fault events. Type, channel
order, rate, clock interpretation and revisions are immutable. An initial terminal
may carry `SEGMENT_START` at position zero. Native-counter, quality and delay
event schemas are not implemented.

The separately selected profile is `sample_frames_encoded_v1`. Data packets add
critical TLV **`0x8003 FRAME_ENCODING`**, with 12 little-endian value bytes:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | u16 | Version 1 |
| 2 | u16 | Codec: 0 raw fallback, 1 independent LZ4 block, 2 independent Zstandard frame |
| 4 | u32 | Decoded bytes; exactly frames × channels × wire value width |
| 8 | u32 | CRC-32 of decoded numeric bytes, using zlib's CRC-32 convention |

The descriptor selects a codec. Each unit uses it only when the encoded payload
is smaller; otherwise raw fallback is explicit. LZ4 uses its **block format**,
not the LZ4 file/frame wrapper. Zstandard uses a single complete frame with known
content size and no dictionary; trailing/concatenated frames are rejected.
CRC also covers raw fallback; it detects corruption, not peer authenticity.
Terminal markers have no encoding extension or codec payload.

Validation checks dimensions, revisions, metadata and the available frame window
before decompression/pool allocation. Fixed output/workspace bounds prevent
oversized expansion. Duplicates are discarded before codec/pool work. Lossless
transport preserves floating-point bits without conversion or resampling.
Legacy decoders continue rejecting these new critical extensions.

Neither profile is advertised by RuntimeService deployment yet. The explicit
NetFrameSource/NetFrameSink path uses the existing authenticated runtime endpoint;
see [frame elements](FRAME_ELEMENTS.md). Other socket owners must configure
matching descriptors, authenticate peers, cap inbound messages at `kMaxMessageBytes`
and carry ACK/RESUME controls. The socket tests demonstrate transfer, not production
deployment integration. [Shared-language fixtures and compatible Python/Java
services](../../doc/S7_Frame_Interoperability.md) now exist; runtime deployment and
resource-admission integration remain prerequisites for advertisement.

## Build and tests

LZ4 (`S7_FRAME_LZ4`) and Zstandard (`S7_FRAME_ZSTD`) are optional. Zstandard needs
a **static** development library because its bounded-workspace API is static-link
only. Missing codecs report unavailable and their descriptors are rejected; raw
always builds. Fedora validation installed `lz4-devel`, `libzstd-static`, and
`libasan`/`libubsan` for sanitizer checks.

```sh
cmake -S . -B /tmp/s7-sdr-audit -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/s7-sdr-audit --target s7transport_frame_smoke s7transport_frame_socket_smoke s7transport_audio_benchmark --parallel 4
ctest --test-dir /tmp/s7-sdr-audit -R '^(s7transport_|s7net_smoke)' --output-on-failure
/tmp/s7-sdr-audit/s7transport/s7transport_audio_benchmark --codec lz4 --type F32 --frames 128 --channels 2 --pattern tone
```

Tests cover golden raw bytes, supported whole-byte types, bounds, malformed and
truncated compressed data, fallback, partial reads/events, fan-out, retained leases,
duplicate/reconnect handling, faults/recovery, concurrent publication, and pool
cancellation/close. Real loopback tests transfer stereo F32 over plaintext and
mutual-TLS WebSockets with all available codecs.

The opt-in audio benchmark uses silence, tone, impulse and seeded noise in I16/F32,
checks every block bitwise and verifies stable reserved capacity. Publication/sealing
and admission/decoding/copying have separate time histograms with bucket-upper-bound
percentiles. Wire/raw ratio includes headers. It is unpaced, process-local and
does not qualify hardware audio, graph performance or a real-time operating rate.
Remaining questions: [audio/compression research](../../doc/S7_Audio_and_Frame_Compression_Research.md).
Retained host results and validation: [audio/frame report](performance/transport-2026-11-12/AUDIO.md).
