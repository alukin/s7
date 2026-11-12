# Initial sample transport measurements — 2026-11-12

Causality first: production graph execution and wire semantics are unchanged.
This increment repairs receiver validation and measures existing transport code.

Host: AMD Ryzen 7 4700U, eight hardware threads, host Release build. Runs were
sequential, unpinned and not thermally isolated. Each case includes endpoint
setup/shutdown, source pattern generation and exact verification of every value.
Sequence reservations use memory; send-time stamping is off. No source timing,
hardware continuity, graph throughput or full-duplex claim is made.

## Capacity baseline

All **135 matrix cases** passed: five modes × three sample types × three payload
limits × three repeats; two independent channel streams, 256 input blocks per
channel. Below are median aggregate sample-payload MB/s at approximately 64 KiB
per packet (CI12 rounds down to 65,535 bytes). Values exclude header bytes.

| Path | CI12 MB/s | CI16 MB/s | C32 MB/s |
| --- | ---: | ---: | ---: |
| codec | 380.0 | 557.5 | 558.0 |
| protocol | 289.1 | 532.8 | 505.1 |
| memory | 325.5 | 408.5 | 365.3 |
| ws | 240.9 | 234.5 | 308.4 |
| tls | 169.5 | 178.1 | 198.9 |

These short runs establish a reproducible baseline, **not sustained supported
rates** or a pure network limit. Different paths include generation/verification
and different scheduling/codec costs. Longer runs and profiling are required
before choosing optimizations or publishing an operating envelope.

## Endpoint chunk comparison

Six additional verified CI16 in-memory cases used two streams, 64 input blocks
per channel and a 32 KiB packet limit, with three repeats per chunk size.
Whole-packet calls delivered a median 367.4 MB/s and 128 packets. One-element
calls delivered 0.464 MB/s and 1,048,576 packets because the in-memory endpoint
flushes pending data when its reader starves. The packet-size/latency settings
were identical; actual packetization was not. This is **not an isolated scalar
API cost or a measured graph speedup**. The first harness incorrectly assumed
input-block count equaled packet count; that assertion was corrected and these
results retain actual packet counts. No production flushing behavior changed.

## Correctness and reproduction

- C++ Release transport checks: 5/5 passed, including real WebSocket descriptor-limit rejection.
- Python suite: 73 tests passed.
- Java: 78 wire fixtures and 59 delivery, 30 node, 14 net and 8 transport checks passed; strict Java 21 compilation succeeded.
- Six invalid benchmark option sets were rejected; existing result files were refused without modification.

Aggregate results, commands, host details, source hashes, chunk cases and
validation evidence are in [baseline.json](baseline.json). The full raw matrix
is temporary; its hash and location are retained. The retained aggregates
include min/median/max, CPU use and peak process RSS for every matrix group.
Build/run instructions and limits: [benchmark guide](../../SAMPLE_TRANSPORT_BENCHMARK.md).

The [extended measurements](EXTENDED.md) add scalar graph, reverse/duplex and
paced workloads with optional queue observations. Exact resource/allocation,
cancellation/stress and cross-host qualification remain open, along with the
T2 frame/ownership contract review. No scheduler or new wire profile is implemented.
