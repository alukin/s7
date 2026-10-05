# S7 Net Element Contract

## Status

Implemented MVP contract for the `s7net` boundary elements (`s7net/include/s7/net.hpp`) and the runtime endpoint they use (`s7transport/include/s7/transport.hpp`). The data protocol is [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md).

---

## Purpose

`NetSink` and `NetSource` are the explicit network boundary of a graph: a `NetSink` on one node and a `NetSource` on another carry one typed stream, matched by `stream_id`.

Design rules:

- the elements do not own sockets or sessions; they use the process's `IRuntimeNetEndpoint` (set through `RuntimeNetContext`), which the runtime service creates;
- a stream has exactly one wire type; nothing converts implicitly (use `Convert` before a `NetSink` or after a `NetSource`);
- no sample is dropped or invented: a stream that cannot keep its guarantee faults, and the step fails.

---

## Elements

Both elements are generic over the stream type `T` (all signal types). The type is inferred from the connected ports like any generic element, or pinned with `type`.

| Parameter | Default | Meaning |
|---|---|---|
| `stream_id` | (required) | UUID of the stream; the producer's `NetSink` and the consumer's `NetSource` use the same value |
| `max_elements_per_packet` | 1024 | Batch size limit |
| `max_batch_latency_ms` | 10 | A partial batch is sent after this delay |
| `send_log_max_packets` | 4096 | Producer: packets kept until every consumer acknowledged them |
| `max_block_ms` | 2000 | Producer: how long a step may block on a full send log before the stream faults (`SEND_LOG_OVERFLOW`) |
| `max_wait_ms` | 2000 | Consumer: how long a step may wait for data before the stream faults (`UNDERRUN`) |

`NetSink` publishes its input value each step. `NetSource` outputs the next received value each step, waiting for it if necessary. The stream (handle) is opened lazily on the first step.

---

## Endpoint interface

```cpp
class IRuntimeNetEndpoint {
public:
    virtual auto open_producer(const stream::StreamConfig&) -> StreamHandle = 0;
    virtual auto open_consumer(const stream::StreamConfig&) -> StreamHandle = 0;
    virtual void close_stream(StreamHandle) = 0;
    virtual auto publish(StreamHandle, const void* elements, std::size_t count) -> PublishResult = 0;
    virtual auto receive(StreamHandle, void* elements, std::size_t count) -> ReceiveResult = 0;
    virtual auto stream_statuses() const -> std::vector<StreamStatus> = 0;
    virtual auto recover_stream(const wire::StreamId&) -> bool = 0;
    // ...
};
```

Elements are host storage of the stream type (`signal_storage_t<T>`). Implementations:

- `InMemoryRuntimeNetEndpoint`: producer and consumer in one process (tests, Studio's local runtime);
- `BeastRuntimeNetEndpoint`: WebSocket / TLS (mTLS) connections, one `/data` and one `/control` connection per peer pair, all I/O on one Asio thread.

---

## Delivery semantics

- Exactly once and in order per stream while both processes run, across dropped connections: the producer keeps sent packets in its send log until the consumer acknowledges them, and a reconnecting consumer RESUMEs from the next sequence it expects.
- A consumer is identified by its peer certificate fingerprint over mTLS, or as one anonymous consumer over plain WebSocket; a disconnected consumer keeps its place in the send log.
- A process restart loses stream state. A restarted producer continues beyond its sequence reservation, so its consumers fault with `SEQUENCE_GAP`; a consumer whose resume position is no longer in the send log continues from the oldest logged packet and faults on that gap, without affecting other consumers.
- A full send log is backpressure: `publish` blocks, up to `max_block_ms`.

## Faults

| Code | Cause |
|---|---|
| `SEQUENCE_GAP` | Packets missing (e.g. producer restart) |
| `SEND_LOG_OVERFLOW` | Backpressure longer than `max_block_ms` |
| `STREAM_STATE_LOST` | Producer: a consumer resumed beyond anything this producer assigned (its sequence state was lost) |
| `MALFORMED_PACKET` | A packet could not be decoded |
| `TYPE_MISMATCH` | A packet's type differs from the stream type |
| `UNDERRUN` | No data within `max_wait_ms` |

A faulted stream makes its element's step throw (`<type> stream <id> faulted: <CODE>.`); the runtime stops stepping and keeps serving the control plane. Studio reads stream state with `GET_STREAM_STATUS` and clears a fault with `RECOVER_STREAM` (`S7_Control_Protocol_MVP.md`). After recovery a consumer accepts the next packet it receives as its new start; data lost in the fault stays lost and reported.

---

## Integration with Existing Architecture

This contract aligns with:

- explicit network boundary principle (`S7_Architecture_Overview.md`)
- distributed addressing (`DISTRIBUTED_ADDRESSING.md`)
- runtime orchestration (`S7_Runtime_Architecture.md`)
