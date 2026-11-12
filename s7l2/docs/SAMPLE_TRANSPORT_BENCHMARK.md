# Sample transport capacity baseline

The opt-in `s7transport_sample_benchmark` measures existing wire v1 code without
changing S7 graph semantics. **Causality first:** no scheduler, element evaluation,
model time or publication behavior is changed by this benchmark.

Build in Release:

```sh
cmake -S . -B /tmp/s7-samples -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/s7-samples --target s7transport_sample_benchmark --parallel 4
```

Normal builds and correctness CTest do not build/run the benchmark. Modes:

| Mode | Measured path |
| --- | --- |
| `codec` | Elements → encoded payload → packet → decoded payload → elements |
| `protocol` | Producer/send log → packet codec → consumer/take/consumption ACK |
| `memory` | Bulk endpoint in one process, concurrent producer/consumer |
| `ws` | Two local Beast endpoints over plaintext loopback WebSocket |
| `tls` | Two local Beast endpoints over mutually authenticated TLS/WebSocket |

Endpoint modes also accept `--execution graph`: ordinary compiled scalar
NodeRuntime graphs with typed benchmark sources, NetSink, NetSource and exact
sample checkers. One step remains one logical sample/frame per channel. No
optimized executor or production element changes are used.
`--direction forward|reverse|duplex` selects server→client, client→server or
simultaneous independent streams in both directions. This does not create causal
feedback between the two graphs.

Supported types are native CI12, CI16 and C32. Every delivered complex value is
checked exactly against a deterministic channel/index pattern. Endpoint runs also
check packet/sequence/consumption counters and fault/duplicate/malformed state.
No checksum-only or unchecked throughput mode is provided.

```sh
/tmp/s7-samples/s7transport/s7transport_sample_benchmark \
  --mode ws --type CI16 --packet-bytes 65536 --packets 1024 --channels 2

python3 tools/benchmark_sample_streams.py \
  --executable /tmp/s7-samples/s7transport/s7transport_sample_benchmark \
  --output /tmp/s7-capacity.jsonl \
  --modes codec protocol memory ws tls \
  --cert-dir s7runtime/examples/certs
```

TLS uses the specified repository test credentials for both local endpoints;
this is not an external-host connection or production credential setup. Send-time
stamping is disabled in all modes: the synthetic model grid is not synchronized
UTC or device time. Optional paced measurements use only a shared, process-local
steady clock, as described below; they do not measure RF or cross-host latency.
Sequence reservations use in-memory storage; production sequence-file fsync
costs are not measured by this baseline.

The runner executes cases sequentially and records commands, repeats, requested
and actual payload sizes and host information. CI12 payloads round down to a
whole number of three-byte complex values. Existing output paths are refused.
The executable rejects malformed/oversized payload and chunk options.

The reported wall/process CPU time includes endpoint setup/shutdown, synthetic
input generation and full verification. Endpoint runs additionally report setup
and active-phase time. Graphs are compiled and lazily bound serially before their
workers start: RuntimeNetContext is process-global, and worker graphs must use
already cached endpoints. One verified priming frame per channel is included in
setup and excluded from active-phase throughput. This is a benchmark fixture,
not a change to runtime endpoint binding or graph semantics.
Peak RSS is process-wide where available,
not an exact measurement of stream-queue occupancy. Use sufficiently long runs
and several repeats; compare the same settings and machine. Record CPU model,
build, affinity and background load with results. No machine-independent speed
assertion belongs in correctness CI.

Each channel is an independent scalar-complex stream, with a common deterministic
source grid in the harness. `frames_per_channel_per_second` is a workload rate,
not proof of atomic paired transport, device timing or an advertised SDR rate.
`payload_bytes_per_second` sums the channels/directions and excludes headers.
Frame counts/rates are per channel in each direction; duplex totals are explicitly
twice a single direction's payload. Per-direction counters remain separate.

Bulk endpoint modes accept `--chunk-elements` to compare bulk versus scalar **endpoint
API** calls with unchanged packet-size and latency settings. Actual batches can
change: the in-memory endpoint flushes pending values when its consumer starves,
and network endpoints flush on latency deadlines. `--packets` sets the number
of input blocks of the configured payload size per channel; `packets_received`
records the actual total packet count. For example, run otherwise identical
short `memory` cases with chunk 1 and the default whole-packet chunk. This does
not measure NetSource/NetSink graph execution or model-time scheduling.

## Graph, pacing and queue observations

```sh
# Start with small scalar graph workloads, then increase deliberately.
python3 tools/benchmark_sample_streams.py \
  --executable /tmp/s7-samples/s7transport/s7transport_sample_benchmark \
  --output /tmp/s7-graphs.jsonl --modes memory ws tls --types CI16 C32 \
  --execution graph --directions forward reverse duplex \
  --payloads 384 --packets 64 --cert-dir s7runtime/examples/certs

/tmp/s7-samples/s7transport/s7transport_sample_benchmark \
  --mode ws --type CI16 --channels 2 --direction duplex \
  --packet-bytes 65536 --packets 128 --rate-frames 31250000 \
  --queue-sample-every-blocks 16

cmake --build /tmp/s7-samples --target s7transport_sample_benchmark_check
```

`--rate-frames` is a positive integer rate per channel **in each direction**,
available for bulk endpoint modes. A source waits until its block's last frame
would exist before publishing it. Graph mode remains unpaced; its logical rate
is not replaced by host packet arrival or a sleep timer. Paced workload duration
is capped at one hour; the runner also has a per-case timeout.

For paced cases, `first_frame_delivery_latency` samples the time at each receive
call's completion minus the scheduled time of its first frame. It includes source
block accumulation, generation, transport/queuing and receiver scheduling, uses
one process-local clock, and is not a pure network or device-time measurement.
Counts are receive calls, not all individual frames. Histogram p50/p99 values
are upper bucket bounds from the existing microsecond/log2 histogram.
Source lateness and blocks more than 1 ms late are recorded independently.
The synthetic generator can fall behind without discarding its known pattern;
`verified` means sample integrity, **not that the offered rate was sustained** or
that a real hardware source would have avoided overflow.

`--queue-sample-every-blocks N` optionally samples aggregate log packet count and
buffered numeric values at producer block boundaries for each direction. These
are observed maxima, not exact high-water marks or allocated-byte counts. Sampling
adds locks and work and is included in the measurement. Zero samples means queue
observation was disabled, not that no buffering occurred.

The explicit benchmark-check target runs bounded integrity/direction/graph cases,
paced availability checks and deliberate mismatch controls. `--corrupt-frame N`
is a negative-control option (positive in-range index on channel zero); successful
verification must fail on that injected wrong value. The check target remains
opt-in and imposes no throughput threshold on arbitrary CI hardware.

Exact queue/allocation profiling, cancellation-under-load, reconnect performance,
longer stress and two-host qualification remain in the
[transport plan](SDR_TRANSPORT_IMPROVEMENT_PLAN.md). Existing production correctness
tests separately cover replay, backpressure and faults. These measurements do not
implement the [accepted sample-frame profile](../../doc/S7_Sample_Stream_Profile.md).

The [initial 2026-11-12 measurements](performance/transport-2026-11-12/README.md)
retain the first verified matrix, the partial-batch counter correction, exact
commands and current qualification limits.
The [extended report](performance/transport-2026-11-12/EXTENDED.md) retains graph,
reverse/duplex and paced results, comparison limits and the 99-control check log.
