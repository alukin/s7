# S7 transport improvements for high-rate sample streams

Status: proposed implementation plan, 2026-11-12. This document does not approve
new wire or causal semantics, assign release scope, or claim measured throughput.
The maintainer wants software ready for faster SDR/network hardware; the current
Gigabit Ethernet bench is not a design ceiling. FPGA work is outside this plan.

Coordinate protocol decisions with the ownership boundaries in the
[0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md). C++ is the reference implementation;
shared protocol behavior must agree with Python/Java and the language-neutral
specification. Implement in small, independently reviewable changes.

## Baseline and invariants

**Causality first is S7's primary rule.** Throughput, batching and physical
clock handling must preserve the meaning of the causal graph.

Reviewed revision: `5214416c0f65d504da787affbdae710c58297ca9`.

- Wire v1 already supports packet batching, CI16/CI12/C32, a 256 KiB payload
  limit, consumption ACKs, reconnect replay and configured stream faults.
- `IRuntimeNetEndpoint::publish/receive` already accept multiple elements.
  `NetSinkT::step` and `NetSourceT::step` use one element per call
  (`s7net/include/s7/net.hpp:90`, `:131`). Bulk transport is not block execution.
- A fresh headless Release run passed wire-format, stream-protocol and Beast
  stream tests, 3/3, on 2026-11-11. This is correctness evidence, not a speed test.
- A source-built probe confirmed that a CI16 packet of 1025 elements is accepted
  with `max_elements_per_packet=1024`; 1024 passes and empty payload is rejected.
  `StreamConsumer::on_packet` omits the descriptor-limit comparison
  (`s7transport/src/stream_protocol.cpp:392`). Python/Java have similar consumer
  paths; their behavior needs its own regression checks.

Preserve exact sample formats and explicit conversions, rational rate domains,
causal publication/state rules, stream identity/order, mTLS control authorization
and the explicit data-security policy. Network arrival order does not establish
cross-stream causality. A software frame index does not prove hardware continuity.
ACK means graph consumption under the delivery contract, not socket arrival.
Process-crash stream recovery remains outside the current guarantee.

## Implementation sequence

| Stage | Deliverable | Completion gate |
| --- | --- | --- |
| T0 — Validation | Enforce the existing per-stream packet limits before allocation/decoding | Boundary and failure tests pass in C++, Python and Java |
| T1 — Baseline | Reproducible synthetic sample-stream benchmark | Separate codec, transport and graph results, with correctness and memory evidence |
| T2 — Contract | Reviewed language-neutral frame/metadata and resource contract | Maintainer decision recorded before implementing new semantics |
| T3 — Data path | Bounded block ownership, transport limits and measured copy/wakeup improvements | Stable memory, correct replay/backpressure and interruptible shutdown |
| T4 — Graph boundary | Block-aware sample execution and network/hardware-facing boundaries | Equivalent logical sample results across arbitrary batch partitions |
| T5 — Qualification | Published performance envelope and cross-language conformance | Sustained stress/failure tests pass on stated hardware and security profiles |

### T0 — Correct the existing validation defect

Reject an element count above the stream descriptor's maximum before growing
receiver storage; preserve existing sequencing/fault behavior. Cover exact limit,
limit+1, malformed lengths, empty payload, packed CI12 and the global payload
limit. Check that a rejected packet does not advance sequence/consumption state
or become visible to the graph. Exercise both direct consumers and the real
endpoint path; add shared cases where the fixture format permits it.

This implements the existing wire section 7 requirement. No new protocol version
or architecture decision is needed to repair the discrepancy. Review cancellation
and failure tests relevant to any code touched, without broad transport refactors.

### T1 — Measure before selecting optimizations

Add an opt-in benchmark under `s7transport/benchmarks/` with a documented command
and machine-readable results. Use deterministic native integer IQ patterns and
known floating values; check every delivered sample, not only packet counts.

Measure separately:

1. Element codec and packet framing.
2. Producer/consumer with send log, ACK and replay.
3. Bulk endpoint over in-memory, WebSocket and TLS loopback.
4. Existing scalar NetSource/NetSink graph boundary as a separate baseline.

Sweep CI12/CI16/C32; 1, 2 and several channels; packet sizes such as 16/64/256 KiB;
RX-like, TX-like and full-duplex traffic. Before a paired-frame profile exists,
use separately identified channel streams and a deterministic common source grid
in the harness; do not claim atomic paired delivery from the old protocol.

Include sample-derived offered loads near 250 MB/s, 500 MB/s and 1 GB/s on capable
hosts. These are workload targets, not promised supported rates. Report offered
and achieved frames/s and payload bytes/s, CPU time, RSS and queue maxima,
allocation/copy evidence where measured, latency distribution and stop time.
State CPU, compiler/build, affinity, duration, security profile and verification
cost. Separate unpaced capacity tests from paced latency/backpressure tests.

Bounded smoke workloads belong in CI; longer stress/performance runs are opt-in.
Do not impose machine-independent wall-time performance assertions. Establish
host software capacity before requiring new radios or faster Ethernet.

### T2 — Agree on the sample-stream contract

Propose a language-neutral sample-stream profile and an ADR, both explicitly
marked proposed until reviewed. Settle these decisions together:

| Subject | Proposed direction / decision to settle |
| --- | --- |
| Units | One frame contains one simultaneous value from each declared channel; a processing block and a wire packet contain multiple frames. Keep samples/s, frames/s and values/s unambiguous. |
| Layout | Declare sample type, channel identities/order and planar/interleaved representation. Preserve native widths; conversion and rate changes remain explicit. |
| Position | Declare source segment/epoch and first-frame coordinate, independently of transport packet sequence and buffer address. Distinguish unknown, software-counted and hardware-counted positions. |
| Timing | Declare clock domain/origin, nominal/configured rate versus any measured clock model, timing validity and filter/resampler delay. Reuse rational rates and timestamp roles. |
| Metadata | Typed, size-bounded events at frame offsets: configuration changes, timing anchors, clipping, source loss and calibration revisions. Define rate-change/delay propagation and packet-splitting rules. |
| Ownership | Bounded buffer pools and explicit borrowed/retained lifetimes; partial consumption/production, cancellation and replay retention rules. Local reuse does not promise network zero-copy. |
| Failure | Keep strict lossless behavior by default. Distinguish network loss, RX source overflow and TX device underflow. Define fault/restart/segment behavior without hiding missing samples or inventing their count. |
| Resources | Per-stream and per-node byte/frame limits, translated to buffered duration; send log, receive queue, retained buffers and metadata all count. Decide whether receiver credits are needed beyond the current log/ACK mechanism. |
| Wire evolution | Choose critical v1 extensions/profile capability versus a new wire version after the field design is concrete. Define negotiation and rejection of unsupported profiles; no compatibility shims are required for this pre-MVP project. |
| Graph representation | Decide bulk execution of existing scalar sample domains versus explicit frame-valued ports. Do not reinterpret a scalar step as a packet, or change model rate when packet size changes. |

Record native sample loss explicitly. Replay can recover transmitted packets;
it cannot restore samples the device lost before packetization. A stream event
affecting sample validity must not depend on an asynchronous status message
arriving before its samples. Status counters complement ordered stream metadata.

### T3 — Implement the bounded data path

Implement the reviewed ownership/resource contract in `s7transport` and its
runtime lifecycle wiring. Keep queue bounds in bytes as well as packet counts.
Reserve storage outside the steady-state hot path where practical. Use T1 to
select changes such as removing redundant payload copies, reusing codec buffers
or coalescing pump wakeups. Preserve portable encoding, per-consumer replay and
the exact authentication boundary. Benchmark any lock/threading change separately.

Test slow/stalled consumers, multiple consumers including one disconnected peer,
partial blocks, reconnect/replay, limits reached mid-block, close during blocked
publish/receive and exhausted pools. Cancellation must release waits and leases;
report retained data and failure rather than silently dropping it. Add sanitizer
coverage for ownership and concurrency changes.

### T4 — Integrate graph execution and sample boundaries

Give `s7net` a block-aware path using the bulk endpoint and approved graph
representation. Add synthetic device-like sources/sinks before a real SDR
adapter. RX acquisition and TX device pacing have their own bounded queues and
loss policies; neither is governed by network packet arrival alone.

Prove results are independent of batch partition: sizes 1, small/odd blocks,
large blocks and changing sizes. Include stateful filters, Delay, feedback,
flattened nested schemas and rate boundaries in the supported set. Unsupported
regions must have an explicit scalar path or be rejected; optimization must not
change causality. Preserve per-step publication and the current rollback limits;
do not silently make a whole block one atomic step. Specify how observers and
plugins see completed samples and where partial progress is reported.

Use bitwise comparisons where operation order is preserved; document numerical
tolerances where an approved optimization changes floating operation order.
Within-node parallel scheduling is a separate project, not a prerequisite here.

### T5 — Qualify and publish the operating envelope

Repeat the T1 matrix after each optimization and graph integration. Require
verified sample order/content, channel alignment, timing metadata and explicit
faults, bounded RSS/queues, and measured cancellation latency. Include reconnect,
slow peers, source overflow, sink underflow, clock/epoch/configuration changes and
unsupported profile rejection. Ordinary transport faults must not bypass control
authorization or trigger a security downgrade.

Add shared profile fixtures and C++/Python/Java interoperability checks for each
implementation's advertised support. A node without the new profile rejects its
deployment explicitly; implementing every new compute feature in every language
is not required. CI covers short deterministic cases; run longer stress tests on
the benchmark host and two-host/faster-link tests when hardware is available.
Publish measured supported rates and resource budgets, not a universal speed claim.

## GNU Radio ideas to adapt

- Buffer spans with consumed/produced counts, history and declared rate/delay:
  [block API](https://www.gnuradio.org/doc/doxygen/classgr_1_1block.html).
- Metadata attached to absolute stream positions and explicit propagation:
  [stream tags](https://wiki.gnuradio.org/index.php/Stream_Tags).
- Hardware timing anchors and source overflow reporting:
  [UHD source](https://github.com/gnuradio/gnuradio/blob/main/gr-uhd/lib/usrp_source_impl.cc).

These are design references, not a decision to adopt GNU Radio's scheduler,
metadata serialization or PUB/SUB delivery policy. S7 retains its causal model
and explicit stream contract.

## First implementation slice

Start with T0, then T1. Prepare T2 using their evidence and review it before
implementing T3/T4. This allows useful transport work immediately while keeping
new sample/clock/graph semantics concrete and reviewable. GUI redesign, new FPGA
firmware, protocol replacement, distributed scheduling and durable sample replay
across process crashes are outside this workstream.

## Implementation record — 2026-11-12

T0 is implemented locally in C++, Python and Java: receiver descriptor/global
payload bounds, malformed lengths and the one-element explicit-timestamp rule
are checked before decoded storage and recovery/sequence changes. Tests cover
exact/partial bounds, CI12/CI16/C32, rejected recovery-position changes and a
real WebSocket descriptor-limit violation. This enforces the existing contract;
no new graph or wire semantics are introduced.

The first T1 increment adds an opt-in
[sample capacity benchmark](SAMPLE_TRANSPORT_BENCHMARK.md): codec, protocol,
in-memory, WebSocket and TLS paths; sequential matrix runner; complete value and
counter verification; configurable payloads, channels and endpoint chunk sizes.
It does not measure graph execution, paced latency, full duplex, exact queue
maxima or cross-host throughput. Those T1/T5 measurements remain open. T2 and
subsequent implementation still require the reviewed semantic contract.

Retained checks and first capacity results:
[2026-11-12 report](performance/transport-2026-11-12/README.md). C++ transport
tests pass 5/5, Python tests 73/73, and Java fixture/delivery/node/net/transport
checks pass. All 135 repeated matrix cases and six endpoint-chunk cases verify
their complete sample streams. These short host measurements do not establish
a sustained operating envelope or accelerate production graph execution.

The second T1 increment adds existing scalar graph fixtures, forward/reverse/duplex
traffic, synthetic source pacing, process-local first-frame latency and optional
block-boundary queue observations. Its opt-in integrity target also injects
incorrect samples to prove verification is active. Observed queues are not exact
resource accounting; allocation, cancellation/stress and two-host qualification
remain open.

The [extended report](performance/transport-2026-11-12/EXTENDED.md) retains 153
verified performance cases, 99 benchmark integrity/negative controls and 6/6
transport/net CTest checks. Scalar graph throughput is substantially below bulk
endpoint throughput, with packetization differences recorded explicitly; no
optimized graph speedup or sustained offered rate is claimed.

T2 is now concrete in the accepted
[sample-frame profile](../../doc/S7_Sample_Stream_Profile.md) and ADR-025.
The maintainer approved the transport-only contract on 2026-11-12; T3 implementation
now has an initial socket-independent C++ service. Production graph execution,
retry behavior and deployed scalar wire behavior remain unchanged. The contract separates the transport service from
the later graph commit/failure/bulk-execution gate.

Follow-up [audio and compression research](../../doc/S7_Audio_and_Frame_Compression_Research.md)
examines device clocks, small latency budgets and explicit lossless/perceptual
source/sink boundaries. The maintainer subsequently approved implementation of
raw audio and independent lossless compression. The [C++ service](FRAME_TRANSPORT.md)
now includes those paths, bounded ownership, replay and fault handling; runtime
deployment/admission, shared-language fixtures and graph integration remain open.

The maintainer then requested element-library and Studio integration. The initial
[scalar frame adapters](FRAME_ELEMENTS.md) expose named channel ports and explicit
raw/LZ4/Zstandard configuration, using bounded in-memory/WebSocket/TLS runtime
endpoints. A failed rate-domain sample latches those boundaries; it does not
change logical rates, causal ordering, legacy element retries or introduce bulk
execution. Automatic frame route generation, capability negotiation and deployment
admission remain open; this is not completion of the full T4/T5 qualification gate.
