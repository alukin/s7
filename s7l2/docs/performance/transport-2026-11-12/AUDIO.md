# Initial C++ audio/frame validation — 2026-11-12

The opt-in frame source/sink service passed **1222 checks** with raw, LZ4 and
Zstandard enabled. Transport/net CTest passed **8/8**, including stereo F32 over
real WebSocket and mutual-TLS sockets. The new service/test harness also passed
AddressSanitizer (leak detection enabled) and UndefinedBehaviorSanitizer.
External libraries were not rebuilt with sanitizers. A separate compile without
either codec passed **768 checks**, explicitly reporting both codecs unavailable.

All **120 audio/compression cases** passed bitwise verification and stable buffer
reservation checks. Workloads cover I16/F32; silence, tones, impulses and seeded
noise; 48/128-frame stereo, 480-frame eight-channel and 128-frame 64-channel
blocks at 48 kHz, plus eight-channel 480-frame blocks at 96 kHz. Each case uses
200 blocks. The services are in-process and unpaced; their declared media rate
does not prove real-time device operation.

Host: AMD Ryzen 7 4700U, GCC 16.2.1, Release, Linux x86-64. Cases ran sequentially,
unpinned, without thermal isolation, **one repeat**. These are initial functional
and resource observations, not a sustained performance envelope or a universal
codec comparison.

## Example wire sizes

Stereo F32, 128 frames/block at nominal 48 kHz. Values are total wire bytes divided
by original numeric bytes, **including framing and encoding metadata**:

| Encoding | Silence | Tone | Seeded noise |
| --- | ---: | ---: | ---: |
| Raw | 1.078 | 1.078 | 1.078 |
| LZ4 | 0.107 | 0.848 | 1.094 |
| Zstandard | 0.112 | 0.349 | 0.899 |

Raw has framing overhead. Encoded profiles add another 16 bytes of metadata per
data packet; raw fallback prevents payload expansion, not that fixed overhead.
The F32 noise workload scales a 16-bit random amplitude and therefore contains
structure. It does not represent arbitrary 32-bit entropy or noisy SDR IQ.
Codec selection needs representative application data and latency/CPU evidence.

Reserved buffer/workspace capacities in this fixture were approximately 2.91 MB
for raw, 2.94 MB for LZ4 and 4.27 MB for Zstandard, including both services and
their pools/rings. These deliberately use maximum-size preallocated slots even
for small audio blocks; they are not minimal audio-buffer recommendations or RSS.
Application arrays, allocator overhead and network/TLS resources are outside this
capacity counter. Socket tests are separate from this in-process matrix.

## Reproduction and retained evidence

The [service guide](../../FRAME_TRANSPORT.md) gives build and run commands.
[audio-frames.json](audio-frames.json) retains every benchmark command/result,
compiler/host details, source hashes and validation logs. Publish/seal and
admit/decode/copy timing histograms include protocol work; they are not isolated
codec timings. No hardware timing, graph execution or playout deadline is qualified.

The source checks cover golden raw bytes, all supported whole-byte numeric wire
types, NaN bit patterns, CI12, malformed/truncated packets, decoded bounds, raw
fallback, retained leases, consumption versus receipt/RESUME, multiple consumers,
ordered faults with known/unknown counts, explicit recovery with retained fault
history, concurrent transfer, blocked-wait cancellation and close.

At this measurement stage, runtime deployment/capability integration, shared
new-profile language fixtures, Windows qualification, ThreadSanitizer, OS audio
and adaptive clocks remained open. The subsequent
[cross-language report](FRAME_CONFORMANCE.md) records compatible Python/Java
services and shared fixtures; the other qualification limits still apply.
