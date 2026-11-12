# s7transport

The data plane of S7 runtime nodes: typed, lossless streams between nodes
([wire protocol](../../doc/S7_Network_Wire_Protocol_v1.md)) and the WebSocket
connections that carry them and the control protocol.

| Header | What it is |
|---|---|
| `s7/wire_format.hpp` | Wire format v1: the 16-byte data packet header, header extensions (`TIMESTAMPS`), payloads of every signal type (12-bit packing included), and the `ACK` / `RESUME` stream control messages. Written and read explicitly little-endian. |
| `s7/stream_protocol.hpp` | Delivery independent of sockets: batching, the producer's send log, cumulative acknowledgement, resume and replay per consumer, backpressure, durable sequence reservations, timestamp profiles, and the consumer's in-order, exactly-once delivery with its faults. |
| `s7/frame_transport.hpp` | Opt-in frame source/sink service: named channels, immutable bounded leases, ordered source faults, consumption ACKs/replay, raw or independent LZ4/Zstandard encoding. [Implementation and limits](../docs/FRAME_TRANSPORT.md). Not yet integrated into runtime deployment. |
| `s7/transport.hpp` | Runtime endpoints (`IRuntimeNetEndpoint`): `InMemoryRuntimeNetEndpoint` (one process) and `BeastRuntimeNetEndpoint` (WebSocket and mTLS, asynchronous on one I/O thread): streams routed directly from producer to consumer, bound to their configured nodes, stream status and recovery, the control channel, and certificate-based node identity. |

`NetSink` / `NetSource` (`s7net`) are the graph elements using these
endpoints; their contract is [S7_Net_Element_Contract.md](../../doc/S7_Net_Element_Contract.md).

## Tests

```bash
cmake --build build --target build-tests --parallel
ctest --test-dir build -R s7transport --output-on-failure
```

- `s7transport_wire_format_smoke`: the codec against the shared fixtures `tests/fixtures/wire_v1.json` (see its README).
- `s7transport_frame_smoke`: frame codec, bounds, leases, replay, faults/recovery and concurrency; all available lossless codecs.
- `s7transport_frame_socket_smoke`: stereo F32 over real WebSocket/mutual-TLS sockets, consumption ACKs and ordered fault markers.
- `s7transport_wire_fixtures_python`, `s7transport_wire_fixtures_current`: a second decoder, in Python, passes the fixtures; the fixtures are what `tools/gen_wire_fixtures.py` writes.
- `s7transport_stream_protocol_smoke`: delivery rules over a simulated link, including a soak test with random connection resets.
- `s7transport_beast_stream_smoke`: streams over real WebSocket and mTLS connections: resume across cut connections, several consumer nodes, malformed input, node identity, stream binding.

Consumers enforce the descriptor's element count and global payload limit before
allocating decoded storage or changing sequence/recovery state. Explicit
per-element timestamps allow one element per packet. Violations fault the stream
with `MALFORMED_PACKET`; rejected values are neither buffered nor acknowledged.

## Capacity measurements

The opt-in [sample transport benchmark](../docs/SAMPLE_TRANSPORT_BENCHMARK.md)
checks every CI12/CI16/C32 value through codec, protocol, in-memory and loopback
WebSocket/TLS paths. Build `s7transport_sample_benchmark` explicitly; it is not a
normal build or correctness-test dependency. It supports existing bulk endpoints
and ordinary scalar graph fixtures, forward/reverse/duplex streams and optional
synthetic pacing. `s7transport_sample_benchmark_check` runs bounded integrity and
availability controls without speed assertions. No new sample-stream profile is
implemented by the benchmark.

`NetFrameSource` / `NetFrameSink` now adapt this service to scalar graph execution
and Studio; see [frame elements](../docs/FRAME_ELEMENTS.md). The runtime endpoint
batches frames, enforces a shared buffer budget and preserves consumption ACKs
over in-memory and WebSocket/TLS paths. Automatic frame-route deployment and
profile capability negotiation remain separate work.
