# S7 Inter-Node Data Stream Protocol — MVP Draft

**Status:** Approved MVP contract (2026-10-05), implemented by the C++ runtime; conformance fixtures for other implementations in `s7l2/s7transport/tests/fixtures/wire_v1.json` (Section 12).
**Draft revision:** 0.7
**Wire version:** 1 (proposed)
**Transport:** WebSocket binary messages over TCP; TLS is required for trusted deployments.

This document defines the data plane used to transfer typed samples between S7 runtime nodes. Control messages, node identity, configuration deployment, and stream setup are defined by [S7_Control_Protocol_MVP.md](S7_Control_Protocol_MVP.md). The protocol is intended to have the same meaning in C++, Rust, Python, and JVM implementations.

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** express protocol requirements and recommendations in this draft.

## 1. Design Goals

- Preserve causal sample order across explicit network boundaries.
- Carry model-time data and real-time data with their timestamp meaning identified.
- Lose and duplicate no data across connection loss: cumulative acknowledgements, resume and replay (Section 12.1).
- Support different scalar and block signal types with a portable byte representation.
- Let a stream declare how gaps affect its operation.
- Keep future transports (including UDP or multicast) possible without changing signal meaning.

## 2. Scope and Guarantees

The MVP data transport is a WebSocket binary message per S7 data packet. A runtime node MAY maintain simultaneous connections to multiple peer nodes. Each connection is authenticated and associated with one peer identity; the `/data` channel carries the configured streams exchanged with that peer. A stream has one configured producer at a time and follows an explicit network connection of the committed configuration: the producer node sends it directly to its consumer node(s). Nodes do not relay streams for other nodes in the MVP (Section 12.1, item 5).

Each packet carries a per-stream sequence number. **Delivery guarantee (MVP):** while producer and consumer processes keep running, every packet of a stream is delivered to the consumer graph exactly once and in order, across connection loss and reconnects. This is achieved with an in-memory send log, cumulative acknowledgements and replay on resume (Section 12.1, item 2). A crash or restart of either process is not covered: it MUST be detected and reported as stream state loss, never silently skipped.

The MVP does not define UDP behavior, multicast membership, forward error correction, global ordering between unrelated streams or peer connections, or a synchronized global clock. A receiver MUST NOT infer causal order between packets from different streams based on arrival order.

## 3. Stream Identity and Setup

Every stream has a 128-bit `stream_id`, assigned as part of the committed network configuration. It MUST be unique within the deployment and stable across runtime process restarts and transport reconnects while that logical stream configuration continues. The stream ID identifies the logical produced stream, independent of its consumer set or network route. A change to the producer, data meaning/type, timestamp profile, channel layout, or send-log limits MUST assign a new `stream_id`; changing only consumers or routes MUST NOT reset stream identity or sequence state. This lets a receiver reject delayed packets from an old logical stream without putting the deployment revision in every packet.

The committed configuration MUST declare at least:

- `stream_id`, producer node, consumer node(s), and source/target boundary;
- signal data type and channel layout;
- timestamp profile and clock interpretation;
- permitted packet/block sizes and sampling profile, if applicable;
- send-log limits (`max_packets`, `max_block_ms`);
- configuration revision to which the stream belongs.

An implementation MUST NOT infer stream type or timestamp semantics from payload contents.

## 4. Sequence Numbers

`sequence` is an unsigned 64-bit integer scoped to one `stream_id` and counts data packets, not individual scalar values inside a block.

- The first packet for a newly created stream starts at zero.
- The producer MUST never reuse a sequence number for a `stream_id`, including after its own restart. It durably reserves ranges of sequence numbers and consumes them in memory, so it needs no storage write per packet; after restart it begins beyond its previous reservation. The resulting jump is how a producer restart becomes visible to the consumer.
- A transport reconnect does not reset the sequence; the consumer resumes from its next expected sequence and the producer replays from its send log (Section 12.1, item 2).
- A deliberate stream replacement uses a new `stream_id` and a new sequence beginning at zero.

The receiver tracks the next expected sequence per stream:

- `sequence == expected`: accept the packet and advance `expected`;
- `sequence < expected`: discard it silently (counted). Duplicates are normal after a replay;
- `sequence > expected`: a gap. Since the protocol replays everything, a gap means data was lost (for example a producer restart). The stream faults.

A new stream starts at sequence zero, and a receiver's expectation for it starts at zero. A first packet with a non-zero sequence is a gap.

## 5. Gap and Duplicate Policy

Losing data is not acceptable, so there is no gap policy in the MVP: every gap faults the stream. A faulted stream delivers nothing further to the consumer graph and its producer stops sending it, until an authorized recovery or reconfiguration. Receivers MUST NOT process one sequence number twice.

A gap occurs only when the delivery guarantee could not be kept: a producer or consumer restart, a send log that overflowed past its blocking limit, or an implementation fault. A future datagram transport may expose loss and reordering during ordinary delivery; its profile must specify bounded reorder handling and retransmission separately.

## 6. Time Semantics

Sequence defines order within a stream. Timestamps describe the time associated with data and are not a replacement for sequence numbers.

The stream declares one or more timestamp roles:

- `MODEL_TIME`: position on the model/simulation time axis. It does not imply UTC or a shared wall clock.
- `SOURCE_TIME`: time assigned at acquisition by the source, such as an SDR or audio device. It is interpreted in the declared source clock domain.
- `PROCESSING_TIME`: time at which a processing node emitted the packet, interpreted in that node's declared clock domain.

When processing preserves source timing, a packet MAY carry both the original `SOURCE_TIME` and a `PROCESSING_TIME`. Receivers MUST keep the roles distinct. Timestamps from different unsynchronized clock domains MUST NOT be directly compared as if they shared a clock.

Timestamp values use signed 64-bit integer nanoseconds from the origin defined by the stream profile. Zero is a valid timestamp; presence is represented by the timestamp record, never by a sentinel value. `MODEL_TIME` is relative to the model-time origin declared by the deployment. It is simulation/model time and MUST NOT be inferred from wall-clock time. `SOURCE_TIME` and `PROCESSING_TIME` are relative to clock domains declared in configuration. A clock-domain identifier identifies the clock and origin; it does not imply synchronization with another domain. Receivers MUST NOT directly compare timestamps from different domains unless configuration explicitly declares a conversion/synchronization relationship.

Every conforming MVP implementation MUST support all three roles. The stream descriptor declares which roles are required on each packet and the clock domain for each role. A configured stream MUST declare at least one role, and a packet MUST contain exactly the roles required by that stream descriptor. Timestamp presence and role are encoded by the extension in Section 7.

Every conforming MVP implementation MUST encode, decode, and preserve all numeric types listed in Section 8. Arithmetic support is a separate element/schema capability: a node MUST reject a graph assignment that requires an operation it cannot implement, while still being able to transport every listed type through compatible network boundaries.

Each non-model clock domain MUST have a stable configured identifier and an origin/units definition. A node MUST declare whether that clock remains continuous across restart. If it cannot guarantee continuity, it MUST use a new clock-domain identifier after restart. Model time uses the deployment's model-time domain and is not reset by a process restart unless a new model run/configuration explicitly establishes a new origin.

For block data, a timestamp role describes the first logical sample in the packet. Uniformly sampled blocks declare a rational sample rate in stream setup; later sample times are derived from the first-sample time and sample index. Irregular/event blocks MUST carry per-item times or offsets in a separately specified payload/profile; receivers MUST NOT assume uniform spacing for them.

## 7. Binary Packet Format

Each WebSocket binary message contains one message. All multi-byte header fields and numeric payload values are **little-endian**: it is the native layout of common hosts (x86-64, ARM) and of most sample sources (SDR I/Q buffers, sound devices, ADC/DSP boards), so payloads move between devices and the wire without byte swapping. Data packets begin with this fixed 16-byte header; the payload starts 8-byte aligned:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 1 | `version` | Packet format version; `1` in this draft |
| 1 | 1 | `flags` | Bit 0: header extensions present. Other bits MUST be zero |
| 2 | 2 | `data_type` | Wire type from Section 8; MUST equal the stream descriptor's type |
| 4 | 4 | `stream_alias` | Per-connection alias bound to a 128-bit `stream_id` by `RESUME` (Section 12.1, item 2) |
| 8 | 8 | `sequence` | Per-stream packet sequence number |

The message layout is:

```text
fixed header (16 bytes)
header extensions (only if flags bit 0)
payload
```

The extension area, present only when flags bit 0 is set, is a u16 giving the total length in bytes of the TLV records that follow, then those records, then zero bytes so that the area's length (including the u16) is a multiple of 8. The payload therefore starts 8-byte aligned. Non-zero padding is rejected.

Values are encoded explicitly as little-endian, never by copying host memory, so an implementation on a big-endian host produces the same bytes.

Fields that the committed stream descriptor fixes are not repeated per packet: the payload length is the remainder of the WebSocket message, and the element count follows from it and the wire type (Section 8). A receiver MUST reject a packet whose `data_type` differs from the descriptor's wire type, an empty payload, a payload that is not a whole number of elements, a payload larger than the descriptor's `max_elements_per_packet` allows, an unknown `stream_alias`, unsupported versions, nonzero reserved flag bits, and unknown critical extensions. There is no implicit type conversion anywhere on the data path: a type change is an explicit `Convert` element in the graph.

Header extensions use this TLV form:

```text
type: u16
length: u16
value: length bytes
```

Bit 15 of `type` marks a critical extension. Unknown critical extensions MUST be rejected; unknown noncritical extensions MUST be ignored. An extension MUST NOT appear more than once unless that extension explicitly allows repetition.

**Batching.** A producer packs up to `max_elements_per_packet` consecutive elements into one packet and sends a partial packet when the oldest unsent element has waited `max_batch_latency_ms` (default 10 ms). Both are set on the network boundary element (`NetSink`) and recorded in the stream descriptor.

### 7.1 Timestamp records

Uniformly sampled streams carry no per-packet timestamps. Delivery is lossless and ordered, so the consumer counts elements: the element with cumulative index `n` has time `origin + n / sample_rate` in each declared timestamp role, with the origin and rational sample rate from the stream descriptor.

Streams without a `sample_rate` carry extension type `0x8001`, the critical `TIMESTAMPS` extension, on every packet:

```text
record_count: u8
records: record_count repetitions of:
    role: u8
    timestamp_ns: i64, two's complement
```

Role IDs are `1 = MODEL_TIME`, `2 = SOURCE_TIME`, and `3 = PROCESSING_TIME`. Records MUST be ordered by ascending role ID, MUST NOT repeat a role, and the value length MUST equal `1 + 9 * record_count`. An unknown role ID MUST be rejected. A timestamp describes the first element of the packet. Clock-domain identifiers and timestamp origins are declared in the committed stream/node configuration and are not repeated per packet. A receiver MUST verify that the roles present match the stream descriptor: exactly the declared roles on a stream without `sample_rate`, none on a uniformly sampled stream. A mismatch faults the stream with `TIMESTAMP_PROFILE_MISMATCH`.

In the MVP a packet with `TIMESTAMPS` carries one element, so every element has its own times; per-element times inside a block need a separately specified payload profile (Section 6).

### 7.2 Send time

Extension type `0x0002`, the noncritical `SEND_TIME` extension, is transmission metadata, not the data's time: an `i64` (two's complement), the UTC time in nanoseconds since the Unix epoch at which the producer sealed the packet (added it to its send log). A producer includes it while its clock is synchronized (clock domain `utc`, `S7_Control_Protocol_MVP.md` section 10.1) and omits it otherwise; a replayed packet keeps its original value. The value length MUST be 8 and the extension MUST NOT repeat. Extensions are written in ascending type order (`SEND_TIME` before `TIMESTAMPS`).

A consumer measures each packet it delivers to its graph, `arrival − SEND_TIME` over the two synchronized clocks: end-to-end latency including batching, queuing and any replay, accurate to the clocks' combined error. `STREAM_STATUS` reports it per consumer stream (`latency`: samples, min, mean, p50, p99, max, last, in nanoseconds; percentiles are upper bounds from a log2 histogram of microseconds) and counts packets over the descriptor's optional `max_latency_ms` as `late`, recording `LATE` in the stream's recent faults at most once a second. Lateness does not stop the stream.

## 8. Numeric Data Types

Every port of a graph has exactly one of these types, and a stream's wire type is the type of the producing port. Values are carried with no implicit conversion or canonicalization. Integer values use two's complement for signed and plain binary for unsigned types; IEEE values use their interchange bit patterns (NaNs, infinities and signed zero are preserved). All multi-byte values are little-endian.

| ID | Name | Bits per element | Payload representation |
|---:|---|---:|---|
| `0x0001` | `I8` | 8 | Signed integer |
| `0x0002` | `U8` | 8 | Unsigned integer |
| `0x0003` | `I12` | 12 | Packed signed integer (see below) |
| `0x0004` | `U12` | 12 | Packed unsigned integer (see below) |
| `0x0005` | `I16` | 16 | Signed integer |
| `0x0006` | `U16` | 16 | Unsigned integer |
| `0x0007` | `I32` | 32 | Signed integer |
| `0x0008` | `U32` | 32 | Unsigned integer |
| `0x0009` | `I64` | 64 | Signed integer |
| `0x000A` | `U64` | 64 | Unsigned integer |
| `0x000B` | `I128` | 128 | Signed integer |
| `0x000C` | `U128` | 128 | Unsigned integer |
| `0x000D` | `F16` | 16 | IEEE 754 binary16 |
| `0x000E` | `F32` | 32 | IEEE 754 binary32 |
| `0x000F` | `F64` | 64 | IEEE 754 binary64 |
| `0x0010` | `F128` | 128 | IEEE 754 binary128 |
| `0x0101` | `C16` | 32 | Pair of F16: real, imaginary |
| `0x0102` | `C32` | 64 | Pair of F32: real, imaginary |
| `0x0103` | `C64` | 128 | Pair of F64: real, imaginary |
| `0x0104` | `C128` | 256 | Pair of F128: real, imaginary |
| `0x0201` | `CI8` | 16 | Pair of I8: in-phase, quadrature |
| `0x0202` | `CU8` | 16 | Pair of U8: in-phase, quadrature (offset binary, as delivered by RTL-SDR) |
| `0x0203` | `CI16` | 32 | Pair of I16: in-phase, quadrature |
| `0x0204` | `CI32` | 64 | Pair of I32: in-phase, quadrature |
| `0x0205` | `CI64` | 128 | Pair of I64: in-phase, quadrature |
| `0x0206` | `CI128` | 256 | Pair of I128: in-phase, quadrature |

Complex integer types exist so that raw SDR I/Q samples travel at their native width (converting 8-bit I/Q to floating point before the wire would multiply the data rate by 4 to 8), so that hardware integer arithmetic can be simulated exactly, and, for CI32–CI128, so that integer processing such as correlation can run without saturation.

**12-bit packing.** Two consecutive 12-bit values occupy three bytes as one little-endian 24-bit word: the first value in bits 0–11, the second in bits 12–23. An odd final value occupies two bytes, in bits 0–11 of a little-endian 16-bit word with bits 12–15 zero. A payload of `b` bytes is valid only if `b mod 3` is 0 or 2; it holds `2(b div 3)` values, plus one if `b mod 3` is 2. The unused top 4 bits of an odd final value MUST be zero.

**Support.** Every conforming implementation MUST transport and preserve every listed type. Arithmetic is a separate capability: a node advertises the types it can compute with, and Studio MUST NOT deploy a graph, or a network connection, that requires a type a participating node cannot support. In particular, nodes without binary128 or 128-bit integer support MUST NOT be assigned F128, C128, I128, U128 or CI128 streams. The C++ reference runtime supports all listed types (C++26 on GCC or Clang with libstdc++: binary16, binary128 and `__int128`; see `s7l2/s7core/include/s7/float_types.hpp`).

## 9. Validation and Fault Behavior

Before accepting a packet, a receiver MUST check:

1. the authenticated peer is the configured producer for `stream_id`;
2. the stream exists and is active in the receiver's committed configuration;
3. packet structure, lengths, and type match the negotiated stream descriptor;
4. sequence behavior follows Section 4;
5. timestamp roles and clock domains match the stream profile.

A malformed or unauthorized packet MUST NOT be delivered to graph elements. The receiver reports a stream/protocol fault through the control/observation interface. Exact alarm message names are defined by the control protocol's future fault-message extension.

## 10. Security and Integrity

Trusted data streams MUST use the authenticated mTLS WebSocket connection described in the control protocol. TLS protects packets in transit and authenticates the peer certificate. The receiver MUST bind the stream producer to that authenticated node identity.

The sequence number is public and predictable by design. It is an ordering and discontinuity detector, not a secret or authentication token. The stream ID SHOULD be generated with a cryptographically secure random source when created. This draft does not add Proof of History, a hash chain, or a separate per-packet authenticator.

## 11. Compatibility and Evolution

The fixed header version identifies incompatible framing changes. Additive optional metadata uses noncritical TLV extensions. New mandatory extensions or changed timestamp/type semantics require explicit negotiation and a new compatible protocol profile or version.

Implementations SHOULD reject unsupported types and extensions explicitly rather than reinterpret them. The control protocol's stream descriptor advertises data types, timestamp profiles, send-log limits, and protocol versions supported by each participant.

## 12. Remaining Work Before Interoperability Freeze

1. ~~Byte-exact fixtures shared by all implementations.~~ `s7l2/s7transport/tests/fixtures/wire_v1.json` (see its `README.md`): packets of every type of Section 8, timestamp records, stream control messages, malformed messages, and valid and invalid stream descriptors. Written by the Python reference encoder `s7l2/tools/gen_wire_fixtures.py`; checked by the C++ codec and descriptor reader and by `s7l2/tools/check_wire_fixtures.py`, a separate Python decoder written from this specification that is the reference for decoders in other languages.
2. The C++ reference implementation of Sections 7, 8 and 12.1.
3. The durable format of the producer's sequence-range reservation.
4. ~~Exact JSON schemas for `GET_STREAM_STATUS`, `RECOVER_STREAM` and the `NODE_INFO` additions (`clock_domains`, `data_protocol`).~~ Specified in `S7_Control_Protocol_MVP.md` sections 5.2 and 10.1.

### 12.1 Resolutions

All items were decided on 2026-10-05 (items 1, 3 and 4 as proposed).

**1. Stream descriptor (decided 2026-10-05).** Each stream is a JSON object in the deployed diagram's `streams` array, so it is covered by the configuration digest and versioned with the deployment revision. It is derived from a connection between elements placed on different nodes (Section 12.1, item 6), and Studio stores its `stream_id` in the diagram so that it survives redeployments:

```json
{
  "stream_id": "<UUID, CSPRNG-generated when the stream is created>",
  "producer": { "node_uuid": "...", "element": "<element id in the diagram>", "port": 0 },
  "consumers": [ { "node_uuid": "...", "element": "...", "port": 0 } ],
  "data_type": "F64",
  "max_elements_per_packet": 1024,
  "max_batch_latency_ms": 10,
  "send_log": { "max_packets": 4096, "max_block_ms": 2000 },
  "max_wait_ms": 2000,
  "sample_rate": { "numerator": 48000, "denominator": 1 },
  "timestamps": [ { "role": "MODEL_TIME", "clock_domain": "model" } ],
  "max_latency_ms": 50
}
```

There is one stream per producing output port with consumers on other nodes (a fan-out to several nodes is one stream with several consumers). The implementation (`s7l2/s7net/include/s7/diagram_deployment.hpp`) reads and writes every member above; `timestamps` must declare at least one role, in ascending role order. A stream keeps its `stream_id` and settings while its producer stays on the same node with the same type and timing; otherwise Studio assigns a new id.

*Timing (implemented 2026-10-06).* Studio derives a stream's timing from the diagram. A graph steps once per model-time step and a `NetSink` publishes one element per step, so with a model-time step (`dt` in the diagram, seconds) every stream is uniformly sampled: `sample_rate` is `1 / dt` in exact form (`1e9 / dt_ns`, reduced; model time is kept in whole nanoseconds) and the only role is `MODEL_TIME` in clock domain `model`, the deployment's model time (element `n` at `n · dt`, from the start of the stream). Each node also steps its graph in that model time. Without `dt` a stream carries `PROCESSING_TIME` in the clock domain `utc` (the producer's NTP-synchronized system clock, nanoseconds since the Unix epoch, comparable between nodes within their error bounds) on every packet. Nodes are deployed only while their clocks are synchronized (`S7_Control_Protocol_MVP.md`, section 10.1, Clock). `SOURCE_TIME` needs a source element that knows its acquisition time and is not produced by `NetSink` yet.

`data_type` is the exact wire type (Section 8) and equals the producing and consuming ports' type. `max_elements_per_packet` and `max_batch_latency_ms` come from the network boundary element. `sample_rate` is present only for uniformly sampled streams; without it, packets carry explicit timestamps (Section 7.1). A `clock_domain` names a domain declared by the producing node: a node advertises its domains in `NODE_INFO` (`clock_domains: [{ "id", "kind", "continuous_across_restart" }]`). A domain that is not continuous across restart gets a new `id` at each process start (for example `node:<uuid>:monotonic:<boot-uuid>`); the descriptor refers to it as `node:<uuid>:monotonic`, and receivers resolve the current boot-specific id from the producer's `NODE_INFO`.

**2. Delivery: lossless across connection loss (decided 2026-10-05).**
- *Guarantee:* while both processes run, each packet reaches the consumer graph exactly once, in order, across disconnects and reconnects. Process crashes are detected and fault the stream; they are not recovered in the MVP.
- *Send log:* the producer keeps every packet of a stream in memory until the consumer acknowledges it, up to `send_log.max_packets`.
- *Backpressure:* when the send log is full, publishing blocks (the producer's graph stops stepping) for at most `send_log.max_block_ms`; if the log is still full, the stream faults with `SEND_LOG_OVERFLOW`. A source that cannot pause (for example SDR hardware) then overflows at the device, and the node reports that as a source fault.
- *Acknowledgement:* the consumer sends a cumulative `ACK {stream_id, next_expected}` after delivering packets to its graph, at least every 64 packets or 50 ms while data arrives. The producer frees the acknowledged packets.
- *Resume:* after every (re)connect the consumer sends `RESUME {stream_id, next_expected}` for each stream it consumes from that peer; the producer replays from its log and continues. If `next_expected` is older than the oldest logged packet, the packets that consumer needs are gone: the producer counts a lost resume (`lost_resumes`) and continues that consumer from its oldest logged packet, so the consumer sees the gap and faults with `SEQUENCE_GAP`. The loss is that consumer's alone; other consumers of the stream are unaffected. If `next_expected` is newer than the next sequence the producer will assign (the producer's sequence state was lost), the producer stream faults with `STREAM_STATE_LOST`. A consumer that starts (or restarts) without stream state resumes from 0: if the producer still holds the whole stream from sequence 0 it replays all of it, which is lossless for a freshly started consumer graph; otherwise that consumer faults on the gap. `RESUME_NO_STATE` is equivalent to `RESUME` with `next_expected` 0.
- *Consumer identity:* the producer keeps one position per consumer identity: the authenticated peer certificate over mTLS; over plain WebSocket the node UUID the consumer announces in the WebSocket handshake (HTTP header `S7-Node`, sent in both directions), or one anonymous identity if it announces none. A disconnected consumer keeps its position, so packets it has not acknowledged stay in the send log until it resumes. The send log is trimmed to the lowest acknowledgement of all consumers.
- *Consumer starvation:* a consumer graph step that needs a value waits for data up to the stream's `max_wait_ms`, then the stream faults with `UNDERRUN`; values are never repeated or invented.
- *Producer restart:* sequence-range reservation (Section 4) makes the restart visible as a gap, which faults the stream.
- *Wire form:* acknowledgements travel on the same `/data` connection, consumer to producer, as 32-byte little-endian messages. They are distinguished from data packets by `version = 0x80 | 1`: `version` (1), `kind` (1: `ACK`, 2: `RESUME`, 3: `RESUME_NO_STATE`), `reserved` (2, zero), `stream_alias` (4), `next_expected` (8), `stream_id` (16; zero in `ACK`). `RESUME` binds the alias to the `stream_id` for that connection.

**3. Fault reporting (decided 2026-10-05).** The MVP control protocol stays request/response. Add `GET_STREAM_STATUS` → `STREAM_STATUS`, returning per stream: `state` (`active`, `blocked`, `faulted`), `next_expected` / `next_sequence`, send-log depth, counters (`packets`, `replayed`, `duplicates_discarded`, `malformed`, `unauthorized`, `blocked_ms_total`), and a bounded list of recent faults with codes `SEQUENCE_GAP`, `SEND_LOG_OVERFLOW`, `STREAM_STATE_LOST`, `MALFORMED_PACKET`, `UNAUTHORIZED_PRODUCER`, `TIMESTAMP_PROFILE_MISMATCH`, `SOURCE_OVERFLOW`. Studio polls it; pushed events are a later extension. `RECOVER_STREAM {stream_id}` (authorized like a configuration change) restarts a faulted stream with a new sequence expectation; the data lost in the fault stays lost and stays reported.

**4. Negotiation (decided 2026-10-05): none in band for the MVP.** `NODE_INFO` advertises `data_protocol: { "packet_versions": [1], "extensions": ["0x0002", "0x8001"] }`. Studio checks every producer/consumer pair before `PREPARE_DEPLOYMENT` and refuses deployments with no common version; a node also rejects packets whose version byte it does not support. *Implemented 2026-10-06* (`deploy_diagram`, used by Studio and `s7ctl`): it also refuses a stream with per-packet timestamps when an end does not list the `TIMESTAMPS` extension. `NODE_INFO.clock_domains` lists `model` and `utc` (or the node's own `node:<uuid>:utc` while its clock is not synchronized).

**6. Deployment unit and placement (decided 2026-10-05).** Studio deploys the diagram itself (`.s7diagram`, including layout), not an extracted schema; each node extracts its part of the graph and keeps the whole diagram, so Studio can request the deployed diagram (`GET_NETWORK_CONFIGURATION`) and display it. Every element carries a `node` placement attribute. A node runs the elements placed on it; each connection between elements on different nodes becomes a stream with its own descriptor. The diagram format will be extended with whatever further deployment data is needed.

*Implemented 2026-10-05.* The diagram lists the nodes in `runtime_nodes` (`node_uuid`, `name`, `endpoint`) and the streams in `streams`. Every node receives the whole diagram in `PREPARE_DEPLOYMENT` and runs its part: its elements and their local connections, with a `NetSink` (producer end) or `NetSource` (consumer end), pinned to the stream type, for each stream. A consumer node connects a `/data` connection to each producer node's endpoint. A redeployment that keeps a stream continues it without loss, because a node keeps a stream's state while its graph is replaced. Each stream end knows the nodes at the other end (`NetSink.consumer_nodes`, `NetSource.producer_node`), so the stream is bound to them (item 5).

**5. Routing (decided 2026-10-05).** A stream follows the explicit network connection in the schema: the producer node sends it only on its connection to each configured consumer node, identified by the authenticated peer certificate, and a consumer accepts it only from the configured producer. Nodes do not relay streams in the MVP. Relaying through intermediate nodes may be needed later for unstable networks; to keep that possible, `stream_id`, sequence numbers and acknowledgements are end-to-end between producer and consumer, and packets carry no hop-specific fields.

*Implemented 2026-10-06.* A connection's peer node is the node identity of its verified certificate over mTLS. Over plain WebSocket it is the node the peer announces in the handshake (`S7-Node` header) or, for a data peer the node connects to, the node configured for that endpoint; a data peer announcing another node is refused (`identity mismatch`). Plain WebSocket identities are claims and do not make a deployment trusted, but they keep consumers apart and streams on their configured connections. A consumer sends `RESUME` for a stream only on connections to its producer node and refuses packets for that stream on any other connection (counted as `unauthorized`, recorded as `UNAUTHORIZED_PRODUCER`, connection closed). A producer serves a stream only to its consumer nodes and ignores other `RESUME`s (`unauthorized`, `UNAUTHORIZED_CONSUMER`). Refusals do not fault the stream: a peer that has no part in a stream cannot stop it.

## 13. Relationship to Implementation

The C++ reference implementation (2026-10-05) implements:

- the packet format of Section 7, the control messages and 12-bit packing (`s7l2/s7transport/include/s7/wire_format.hpp`), checked against byte-exact fixtures (`s7l2/s7transport/tests/fixtures/wire_v1.json`) produced by an independent Python encoder (`s7l2/tools/gen_wire_fixtures.py`);
- the delivery rules of Section 12.1 item 2: batching, send log, cumulative ACK, RESUME and replay, per-consumer positions, backpressure with `SEND_LOG_OVERFLOW`, `UNDERRUN`, and durable sequence-range reservation (`stream_protocol.hpp`);
- the WebSocket transport carrying them (`BeastRuntimeNetEndpoint`): one `/data` and one `/control` connection per peer pair, reconnect with resume, mTLS consumer identity;
- `GET_STREAM_STATUS` and `RECOVER_STREAM` (`S7_Control_Protocol_MVP.md` section 10.1);
- binding of each stream to its producer and consumer nodes (Section 9 item 1, Section 12.1 item 5);
- timestamp profiles (Sections 6 and 7.1): derived times on uniformly sampled streams, `TIMESTAMPS` records on the others, checked by the consumer (`TIMESTAMP_PROFILE_MISMATCH`), and each received element's times reported to the graph side (`ReceiveResult::timestamps`, `NetSource::last_timestamps()`);
- `NODE_INFO.data_protocol` and `clock_domains`, checked by the deployment client before preparing any node;
- `SEND_TIME` and end-to-end latency per consumer stream (Section 7.2).

Not yet implemented: `SOURCE_TIME` production and relaying. The previous 54-byte big-endian prototype codec has been removed. The fixtures (Section 12 item 1) are what a Rust, JVM or other decoder is checked against; the Python decoder passes them.
