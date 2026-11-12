# Frame sources and sinks

`NetFrameSink` and `NetFrameSource` are built-in `s7net` elements, available in
Studio's Network library and through the headless element registry. They use the
[frame service](FRAME_TRANSPORT.md) with raw, LZ4 or Zstandard encoding. Optional
codecs must be available on both endpoints; unavailable selections are errors.
These are network/model boundaries, not OS audio capture/playback elements.

## Ports and settings

`channels` is an ordered list of 1–64 unique names (default `["L", "R"]`). The
sink has one input per name; the source has one output per name. All channels
share a numeric type, inferred from connections or pinned with `type`. Scalar
I12/U12 are excluded; CI12 and the other whole-byte wire types are supported.
Studio renders the channel names and updates the connectors when this list is
edited. Its properties panel and element information expose all settings.

Each graph evaluation transfers **one complete frame**. `frame_rate` is an exact
positive `[numerator, denominator]` in logical frames/second, default `[48000,1]`.
It pins the graph rate and never changes with packet size. Delay and feedback
continue to follow ordinary scalar causal rules. This first adapter uses a
synthetic model-time grid and software frame positions; it creates no hardware
clock, resampling, sample conversion or clock-anchor events.

Both ends need the same `stream_id`, channel type/order, rate, encoding,
`max_frames_per_packet` and `window_packets`. The default encoding is `raw`;
choose `lz4` or `zstd` explicitly on both ends. A new descriptor meaning requires
a new stream UUID. `producer_node` and `consumer_nodes` optionally restrict peers
using the existing endpoint transport identity policy. They do not authorize
control operations. Socket connections use the node's existing WebSocket/TLS
configuration and data-security policy.

The network endpoint accumulates published scalar frames into bounded packets
and flushes partial packets after `max_batch_latency_ms`. In-memory consumers
also request a partial flush when waiting. `max_frames_per_packet` defaults to
128; the decoded numeric payload must fit 256 KiB. `window_packets` defaults to
4, with worst-case send/receive pool reservations. A producer has one additional
bounded native staging block. All frame pools, workspaces and staging blocks
share an endpoint budget (64 MiB by default; C++ `TransportLimits.frame_buffer_bytes`).
That counter excludes element port storage, socket/TLS queues and allocator overhead. Allocation is
admitted when a boundary opens; deployment-wide admission remains follow-up work.

`max_block_ms` bounds waits for publish capacity, and `max_wait_ms` bounds waits
for a complete input frame (both default 2000). Exhaustion/underflow fails the
sample instead of inventing values. Closing a stream wakes its frame waits.
Consumption advances ACKs; receipt and RESUME do not. The endpoint resends a
consumption ACK after reconnect. Fixed frame aliases use CRC-32 of the UUID with
the high bit set; a local alias collision is rejected, requiring a different UUID.
Scalar aliases occupy the lower half of the alias space.

## Failed samples and lifecycle

If a graph sample fails, the runtime restores its published outputs as before
and notifies the elements in that failed rate domain. Frame boundaries latch a
fault and refuse another evaluation. Already consumed frames and already sealed
network effects cannot be rolled back; retry must not silently skip or duplicate
a frame. Earlier completed rate-domain samples retain their existing progress.
This notification does not change legacy elements' retry behavior.

Rebuild the affected graph with a new stream UUID, coordinating both endpoints,
after a boundary fault. Legacy `RECOVER_STREAM` deliberately refuses frame
streams; coordinated frame-segment recovery needs a later control contract.
Closed frame state remains owned by its endpoint and budget until the endpoint
is destroyed. Repeated reconfiguration can exhaust that bounded reservation;
restart the owner to release abandoned identities. Nothing silently evicts replay.

A local sink and source with the same UUID are still explicit network boundaries.
An unconnected source is not causally ordered after its sink by packet arrival.
Use separate node graphs, or an explicitly ordered test driver; placing both
ends into one local Studio graph does not establish a same-sample dependency.

## Two-node example

From `s7l2`, use separate terminals and state directories:

```sh
./build-release/s7runtime/s7runtime --schema s7runtime/examples/schema_frame_sink.json --state-dir /tmp/s7-frame-tx --serve-forever
./build-release/s7runtime/s7runtime --schema s7runtime/examples/schema_frame_source.json --state-dir /tmp/s7-frame-rx --serve-forever
```

The examples use repository development certificates on loopback. Start both
within the configured timeout, or increase the timeout on both ends for manual
startup. Their nominal 48 kHz rate is model configuration, not a qualified
real-time operating rate. To exercise compression, select the same encoding in
both files and use new stream IDs/state directories after a fault.

Studio can add, configure and save the elements. Explicit frame streams use
preconfigured peer connections. Automatic grouping of cross-node scalar wires
into channel frames, profile capability negotiation, aggregate deployment resource
admission and OS audio devices are separate work. No frame capability is advertised
by the runtime deployment protocol yet.

Validation covers registry metadata/type inference, exact stereo values, Delay
causality, raw/LZ4/Zstandard transfer over actual WebSocket/mTLS endpoints,
failed-step retry refusal, headless schema loading and Studio library/port/save
behavior. Bulk graph execution remains outside this change.
