# S7 Inter-Node Data Stream Protocol — MVP Draft

**Status:** Candidate wire-format draft; not yet a conformance standard.
**Draft revision:** 0.5
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
header extensions (only if flags bit 0: u16 total_extension_length, then TLV records, padded with zero bytes to a multiple of 8)
payload
```

Fields that the committed stream descriptor fixes are not repeated per packet: the payload length is the remainder of the WebSocket message, and the element count follows from it and the wire type (Section 8). A receiver MUST reject a packet whose `data_type` differs from the descriptor's wire type, a payload that is not a whole number of elements, a payload larger than the descriptor's `max_elements_per_packet` allows, an unknown `stream_alias`, unsupported versions, nonzero reserved flag bits, and unknown critical extensions. There is no implicit type conversion anywhere on the data path: a type change is an explicit `Convert` element in the graph.

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

Irregular streams, and streams whose descriptor requires explicit timestamps, carry extension type `0x8001`, the critical `TIMESTAMPS` extension:

```text
record_count: u8
records: record_count repetitions of:
    role: u8
    timestamp_ns: i64, two's complement
```

Role IDs are `1 = MODEL_TIME`, `2 = SOURCE_TIME`, and `3 = PROCESSING_TIME`. Records MUST be ordered by ascending role ID, MUST NOT repeat a role, and the value length MUST equal `1 + 9 * record_count`. An unknown role ID MUST be rejected. A timestamp describes the first element of the packet. Clock-domain identifiers and timestamp origins are declared in the committed stream/node configuration and are not repeated per packet. A receiver MUST verify that the roles present match the stream descriptor.

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

Complex integer types exist so that raw SDR I/Q samples travel at their native width: converting 8-bit I/Q to floating point before the wire would multiply the data rate by 4 to 8.

**12-bit packing.** Two consecutive 12-bit values occupy three bytes as one little-endian 24-bit word: the first value in bits 0–11, the second in bits 12–23. An odd final value occupies two bytes, in bits 0–11 of a little-endian 16-bit word with bits 12–15 zero. The element count of a payload of `b` bytes is therefore `floor(8b / 12)`.

**Support.** Every conforming implementation MUST transport and preserve every listed type. Arithmetic is a separate capability: a node advertises the types it can compute with, and Studio MUST NOT deploy a graph, or a network connection, that requires a type a participating node cannot support. In particular, nodes without binary128 support MUST NOT be assigned F128, C128, I128 or U128 streams. The C++ reference runtime supports all listed types (C++26, GCC: `std::float16_t`, `std::float128_t`, `__int128`).

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

## 12. Items Requiring Review Before Interoperability Freeze

1. Define the stream descriptor's exact serialization and the point at which its timestamp domains are updated after a source/runtime restart.
2. Specify the delivery guarantee, acknowledgement and resume behavior (decided, see 12.1 item 2) and the durable format of the producer's sequence-range reservation.
3. Define control-plane fault/alarm messages for sequence gaps and malformed packets.
4. Define protocol negotiation for supported packet versions and extensions; all Section 8 type IDs are mandatory in the MVP profile.

### 12.1 Resolutions

Items 1, 2, 5 and 6 were decided on 2026-10-05 (item 1 as proposed). Items 3 and 4 are proposals awaiting approval.

**1. Stream descriptor (decided 2026-10-05).** Each stream is a JSON object in the deployed diagram's `streams` array, so it is covered by the configuration digest and versioned with the deployment revision. It is derived from a connection between elements placed on different nodes (Section 12.1, item 6), and Studio stores its `stream_id` in the diagram so that it survives redeployments:

```json
{
  "stream_id": "<UUID, CSPRNG-generated when the stream is created>",
  "producer": { "node_uuid": "...", "element_instance_uuid": "...", "port": 0 },
  "consumers": [ { "node_uuid": "...", "element_instance_uuid": "...", "port": 0 } ],
  "data_type": "F64",
  "channels": 1,
  "max_elements_per_packet": 1024,
  "max_batch_latency_ms": 10,
  "sample_rate": { "numerator": 48000, "denominator": 1 },
  "timestamps": [ { "role": "MODEL_TIME", "clock_domain": "model" } ],
  "send_log": { "max_packets": 4096, "max_block_ms": 2000 }
}
```

`data_type` is the exact wire type (Section 8) and equals the producing and consuming ports' type. `max_elements_per_packet` and `max_batch_latency_ms` come from the network boundary element. `sample_rate` is present only for uniformly sampled streams; without it, packets carry explicit timestamps (Section 7.1). A `clock_domain` names a domain declared by the producing node: a node advertises its domains in `NODE_INFO` (`clock_domains: [{ "id", "kind", "continuous_across_restart" }]`). A domain that is not continuous across restart gets a new `id` at each process start (for example `node:<uuid>:monotonic:<boot-uuid>`); the descriptor refers to it as `node:<uuid>:monotonic`, and receivers resolve the current boot-specific id from the producer's `NODE_INFO`.

**2. Delivery: lossless across connection loss (decided 2026-10-05).**
- *Guarantee:* while both processes run, each packet reaches the consumer graph exactly once, in order, across disconnects and reconnects. Process crashes are detected and fault the stream; they are not recovered in the MVP.
- *Send log:* the producer keeps every packet of a stream in memory until the consumer acknowledges it, up to `send_log.max_packets`.
- *Backpressure:* when the send log is full, publishing blocks (the producer's graph stops stepping) for at most `send_log.max_block_ms`; if the log is still full, the stream faults with `SEND_LOG_OVERFLOW`. A source that cannot pause (for example SDR hardware) then overflows at the device, and the node reports that as a source fault.
- *Acknowledgement:* the consumer sends a cumulative `ACK {stream_id, next_expected}` after delivering packets to its graph, at least every 64 packets or 50 ms while data arrives. The producer frees the acknowledged packets.
- *Resume:* after every (re)connect the consumer sends `RESUME {stream_id, next_expected}` for each stream it consumes from that peer; the producer replays from its log and continues. If `next_expected` is older than the oldest logged packet, or newer than the next sequence the producer will assign, the stream faults with `STREAM_STATE_LOST`. A restarted consumer has no expectation and sends `RESUME_NO_STATE`, which also faults the stream.
- *Producer restart:* sequence-range reservation (Section 4) makes the restart visible as a gap, which faults the stream.
- *Wire form:* acknowledgements travel on the same `/data` connection, consumer to producer, as 32-byte little-endian messages. They are distinguished from data packets by `version = 0x80 | 1`: `version` (1), `kind` (1: `ACK`, 2: `RESUME`, 3: `RESUME_NO_STATE`), `reserved` (2, zero), `stream_alias` (4), `next_expected` (8), `stream_id` (16; zero in `ACK`). `RESUME` binds the alias to the `stream_id` for that connection.

**3. Fault reporting (proposal).** The MVP control protocol stays request/response. Add `GET_STREAM_STATUS` → `STREAM_STATUS`, returning per stream: `state` (`active`, `blocked`, `faulted`), `next_expected` / `next_sequence`, send-log depth, counters (`packets`, `replayed`, `duplicates_discarded`, `malformed`, `unauthorized`, `blocked_ms_total`), and a bounded list of recent faults with codes `SEQUENCE_GAP`, `SEND_LOG_OVERFLOW`, `STREAM_STATE_LOST`, `MALFORMED_PACKET`, `UNAUTHORIZED_PRODUCER`, `TIMESTAMP_PROFILE_MISMATCH`, `SOURCE_OVERFLOW`. Studio polls it; pushed events are a later extension. `RECOVER_STREAM {stream_id}` (authorized like a configuration change) restarts a faulted stream with a new sequence expectation; the data lost in the fault stays lost and stays reported.

**4. Negotiation (proposal): none in band for the MVP.** `NODE_INFO` advertises `data_protocol: { "packet_versions": [1], "extensions": ["0x8001"] }`. Studio checks every producer/consumer pair before `PREPARE_DEPLOYMENT` and refuses deployments with no common version; a node also rejects packets whose version byte it does not support.

**6. Deployment unit and placement (decided 2026-10-05).** Studio deploys the diagram itself (`.s7diagram`, including layout), not an extracted schema; each node extracts its part of the graph and keeps the whole diagram, so Studio can request the deployed diagram (`GET_NETWORK_CONFIGURATION`) and display it. Every element carries a `node` placement attribute. A node runs the elements placed on it; each connection between elements on different nodes becomes a stream with its own descriptor. The diagram format will be extended with whatever further deployment data is needed.

**5. Routing (decided 2026-10-05).** A stream follows the explicit network connection in the schema: the producer node sends it only on its connection to each configured consumer node, identified by the authenticated peer certificate, and a consumer accepts it only from the configured producer. Nodes do not relay streams in the MVP. Relaying through intermediate nodes may be needed later for unstable networks; to keep that possible, `stream_id`, sequence numbers and acknowledgements are end-to-end between producer and consumer, and packets carry no hop-specific fields.

## 13. Relationship to Implementation

The C++ reference transport currently uses a different 54-byte big-endian prototype layout, a 64-bit stream identifier, a packet-level stream name, and a scalar F64 profile. It does not yet implement the packet format of Section 7, acknowledgements and replay, sequence-range reservation, or timestamp-role records. The codec MUST NOT be treated as the source of truth. Before interoperability freeze, this candidate must be reviewed and accompanied by byte-exact fixtures for independent C++, Rust, Python, and JVM implementations. A finalized v1 implementation must either implement this format exactly or use a different explicitly versioned format; it MUST NOT silently call the existing layout v1-compatible.
