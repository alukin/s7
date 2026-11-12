# Graph, duplex and paced sample measurements — 2026-11-12

This extends the [initial baseline](README.md) with **153 verified cases**:
54 bulk direction cases, 54 ordinary scalar graph cases, 18 matching short bulk
cases and 27 paced duplex cases. Every delivered value is checked. No production
graph executor, element, ownership API or wire profile was changed.

The same AMD Ryzen 7 4700U host ran Release builds sequentially, unpinned and
without thermal isolation. Each configuration has three repeats. Rates below
are median **active aggregate sample-payload MB/s**, excluding headers and
setup/shutdown but including generation and verification. Graph setup compiles,
binds and verifies one priming frame per channel; that frame is excluded from
active throughput. Inclusive timing is retained separately in the JSON.

## Bulk endpoints

CI16, two channel streams per direction, 64 KiB input blocks, 256 blocks per
channel; duplex totals both directions. Forward means server to client; reverse
means client to server. All endpoints are process-local or loopback.

| Path | Forward MB/s | Reverse MB/s | Duplex aggregate MB/s |
| --- | ---: | ---: | ---: |
| memory | 391.6 | 430.8 | 397.2 |
| WebSocket | 228.0 | 228.6 | 349.5 |
| TLS/WebSocket | 196.2 | 202.3 | 259.5 |

These are short host measurements, not a supported sustained rate or an Ethernet
capacity limit. The retained results also cover C32.

## Existing scalar graph boundary

Ordinary compiled source/NetSink and NetSource/checker graphs evaluate one
logical value per channel per step. These smaller workloads use 384-byte input
blocks, 64 blocks per channel and two channels per direction. The last column
uses the same input size and count with bulk endpoints in the forward direction.

| Path | Graph forward MB/s | Graph reverse MB/s | Graph duplex MB/s | Bulk forward MB/s |
| --- | ---: | ---: | ---: | ---: |
| memory | 0.437 | 0.494 | 0.497 | 21.043 |
| WebSocket | 0.983 | 0.991 | 2.420 | 21.357 |
| TLS/WebSocket | 1.224 | 1.021 | 2.313 | 20.356 |

The gap identifies the existing scalar boundary as an important investigation
target. It does **not isolate graph evaluation cost**: forward in-memory graph
runs receive 12,288 packets versus 128 for bulk because scalar calls trigger
starvation flushing. WebSocket/TLS graphs receive 130 versus 128 packets, including
the initial prime and final partial packets. No optimized graph implementation
has been measured, and packet boundaries must never replace logical steps.

## Paced offered-load probes

CI16, two channels in each direction, 64 KiB blocks and 128 blocks per channel.
The source waits until its entire block should exist. Offered rates of 31.25,
62.5 and 125 million frames/channel/s correspond to 250, 500 and 1000 MB/s **per
direction**; aggregate duplex offers are 500, 1000 and 2000 MB/s.

| Path | Achieved aggregate MB/s at 500 MB/s offer | At 1000 MB/s offer | At 2000 MB/s offer |
| --- | ---: | ---: | ---: |
| memory | 283.9 | 283.7 | 332.4 |
| WebSocket | 398.4 | 302.6 | 263.9 |
| TLS/WebSocket | 292.7 | 241.6 | 237.4 |

All samples verified, but these workloads did **not qualify the offered rates**.
The synthetic source can fall behind while retaining its known pattern; real
hardware would need an explicit overflow policy. Source lateness alone is also
insufficient: a source can publish on schedule while the receiver drains a
backlog later. Short finite runs fit within the configured outstanding window.

Raw paced rows retain source lateness, process-local first-frame delivery
histograms and queue observations every 16 blocks. Queue values are sampled
observations, not exact high-water marks or allocated-byte accounting. Latency
includes block accumulation and is neither RF nor cross-host latency.

## Evidence and reproduction

[extended-baseline.json](extended-baseline.json) retains grouped ranges/medians,
commands, all 27 paced rows, source hashes and test logs. Temporary raw direction
matrices are identified by path and hash; they are not durable artifacts.
Build and option details are in the [benchmark guide](../../SAMPLE_TRANSPORT_BENCHMARK.md).

```sh
cmake -S . -B /tmp/s7-sdr-audit -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/s7-sdr-audit --target s7transport_sample_benchmark_check --parallel 4
ctest --test-dir /tmp/s7-sdr-audit -R '^(s7transport_|s7net_smoke)' --output-on-failure
```

The explicit benchmark target passed **99 integrity, graph, direction, pacing
and negative controls**, including deliberate sample corruption. Transport/net
CTest passed **6/6**. No machine-independent speed threshold is asserted.
The earlier cross-language validation remains recorded in the initial report.

Exact resource/allocation profiling, cancellation under load, longer stress,
two-host runs and hardware continuity remain open. The next implementation gate
is implementation of the subsequently
[accepted frame/ownership contract](../../../../doc/S7_Sample_Stream_Profile.md),
with graph commit/failure semantics reserved for a separate decision.
