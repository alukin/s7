# S7 Inter-Node Data Stream Protocol — MVP Draft

**Status:** Candidate wire-format draft; not yet a conformance standard.
**Draft revision:** 0.2
**Wire version:** 1 (proposed)
**Transport:** WebSocket binary messages over TCP; TLS is required for trusted deployments.

This document defines the data plane used to transfer typed samples between S7 runtime nodes. Control messages, node identity, configuration deployment, and stream setup are defined by [S7_Control_Protocol_MVP.md](S7_Control_Protocol_MVP.md). The protocol is intended to have the same meaning in C++, Rust, Python, and JVM implementations.

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** express protocol requirements and recommendations in this draft.

## 1. Design Goals

- Preserve causal sample order across explicit network boundaries.
- Carry model-time data and real-time data with their timestamp meaning identified.
- Detect duplicate packets and sequence gaps without adding a per-packet acknowledgement/retransmission protocol to the WebSocket/TCP MVP.
- Support different scalar and block signal types with a portable byte representation.
- Let a stream declare how gaps affect its operation.
- Keep future transports (including UDP or multicast) possible without changing signal meaning.

## 2. Scope and Guarantees

The MVP data transport is a WebSocket binary message per S7 data packet. A runtime node MAY maintain simultaneous connections to multiple peer nodes. Each connection is authenticated and associated with one peer identity; the `/data` channel can carry the configured streams exchanged with that peer. A stream has one configured producer at a time. TCP/WebSocket preserves byte/message order within a live connection. S7 does not add another acknowledgement or retransmission layer in this version.

Each packet carries a persistent per-stream sequence number. Receivers use it to detect duplicates, regressions, and gaps, including discontinuities caused by producer restart or connection loss. A sequence number does not by itself make a missing packet recoverable.

The MVP does not define UDP behavior, multicast membership, packet retransmission, forward error correction, global ordering between unrelated streams or peer connections, or a synchronized global clock. A receiver MUST NOT infer causal order between packets from different streams based on arrival order.

## 3. Stream Identity and Setup

Every stream has a 128-bit `stream_id`, assigned as part of the committed network configuration. It MUST be unique within the deployment and stable across runtime process restarts and transport reconnects while that logical stream configuration continues. The stream ID identifies the logical produced stream, independent of its consumer set or network route. A change to the producer, data meaning/type, timestamp profile, channel layout, or gap policy MUST assign a new `stream_id`; changing only consumers or routes MUST NOT reset stream identity or sequence state. This lets a receiver reject delayed packets from an old logical stream without putting the deployment revision in every packet.

The committed configuration MUST declare at least:

- `stream_id`, producer node, consumer node(s), and source/target boundary;
- signal data type and channel layout;
- timestamp profile and clock interpretation;
- permitted packet/block sizes and sampling profile, if applicable;
- gap policy;
- configuration revision to which the stream belongs.

An implementation MUST NOT infer stream type, timestamp semantics, or gap policy from payload contents.

## 4. Sequence Numbers

`sequence` is an unsigned 64-bit integer scoped to one `stream_id` and counts data packets, not individual scalar values inside a block.

- The first packet for a newly created stream starts at zero.
- The producer MUST persist sequence state and continue it across process restarts and reconnects for that `stream_id`, so it never reuses a sequence number after a crash. A crash may therefore leave a gap. Implementations MAY durably reserve ranges of sequence numbers and consume them in memory, avoiding a durable storage write for every packet; after restart, the producer begins at the first number beyond its previous reservation.
- A transport reconnect does not reset the sequence.
- A deliberate stream replacement uses a new `stream_id` and a new sequence beginning at zero.

The receiver tracks the next expected sequence per stream:

- `sequence == expected`: accept the packet and advance `expected`;
- `sequence < expected`: discard and report the duplicate/regressed packet;
- `sequence > expected`: report a gap, then apply the configured gap policy.

For a receiver's initial observation of a stream, the first received sequence establishes the initial expectation; the receiver MUST report that it joined mid-stream if the first sequence is non-zero. A new receiver need not be able to distinguish historical loss from joining late without additional state.

## 5. Gap and Duplicate Policy

The stream configuration declares the policy for a gap. The MVP defines:

- `CONTINUE_WITH_GAP`: report a discontinuity and continue processing later packets. The consumer must be able to observe the discontinuity; it must not silently fabricate missing values.
- `FAULT_STREAM`: report a discontinuity and mark the stream failed/stopped until an authorized recovery or reconfiguration.

This policy applies to gaps. Duplicate or regressed packets are always discarded and reported; the stream policy determines whether the condition also faults/stops the stream. Receivers MUST NOT process one sequence number twice.

On the current WebSocket/TCP transport, a gap is expected mainly after producer crash/restart, stream reconfiguration, or an implementation/connection fault. A future datagram transport may expose gaps and reordering during ordinary delivery; its profile must specify bounded reorder handling and loss recovery separately.

## 6. Time Semantics

Sequence defines order within a stream. Timestamps describe the time associated with data and are not a replacement for sequence numbers.

The stream declares one or more timestamp roles:

- `MODEL_TIME`: position on the model/simulation time axis. It does not imply UTC or a shared wall clock.
- `SOURCE_TIME`: time assigned at acquisition by the source, such as an SDR or audio device. It is interpreted in the declared source clock domain.
- `PROCESSING_TIME`: time at which a processing node emitted the packet, interpreted in that node's declared clock domain.

When processing preserves source timing, a packet MAY carry both the original `SOURCE_TIME` and a `PROCESSING_TIME`. Receivers MUST keep the roles distinct. Timestamps from different unsynchronized clock domains MUST NOT be directly compared as if they shared a clock.

Timestamp values use signed 64-bit integer nanoseconds from the origin defined by the stream profile. Zero is a valid timestamp; presence is represented by the timestamp record, never by a sentinel value. `MODEL_TIME` is relative to the model-time origin declared by the deployment. It is simulation/model time and MUST NOT be inferred from wall-clock time. `SOURCE_TIME` and `PROCESSING_TIME` are relative to clock domains declared in configuration. A clock-domain identifier identifies the clock and origin; it does not imply synchronization with another domain. Receivers MUST NOT directly compare timestamps from different domains unless configuration explicitly declares a conversion/synchronization relationship.

The stream descriptor declares the timestamp roles required on each packet and the clock domain for each role. A packet MUST contain exactly the roles required by its descriptor. It MAY contain optional roles only if the descriptor allows them. Timestamp presence and role are encoded by the extension in Section 7; no timestamp field is mandatory for a stream whose descriptor declares no timestamp roles.

For block data, a timestamp role describes the first logical sample in the packet. Uniformly sampled blocks declare a rational sample rate in stream setup; later sample times are derived from the first-sample time and sample index. Irregular/event blocks MUST carry per-item times or offsets in a separately specified payload/profile; receivers MUST NOT assume uniform spacing for them.

## 7. Binary Packet Format

Each WebSocket binary message contains one packet. All multi-byte header fields and numeric payload values use network byte order (big-endian). The packet begins with this fixed 48-byte header:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `magic` | ASCII `S7DP` |
| 4 | 1 | `version` | Packet format version; `1` in this draft |
| 5 | 1 | `flags` | Reserved; MUST be zero in this draft |
| 6 | 2 | `header_length` | Total header bytes, including extensions; at least 48 |
| 8 | 16 | `stream_id` | Stream UUID bytes in RFC 4122 network order |
| 24 | 8 | `sequence` | Per-stream packet sequence number |
| 32 | 4 | `channel_id` | Channel within the configured stream; zero if not multiplexed |
| 36 | 2 | `data_type` | Numeric identifier from Section 8 |
| 38 | 4 | `element_count` | Number of logical values in payload; complex values count as one each |
| 42 | 4 | `payload_length` | Payload size in bytes |
| 46 | 2 | `reserved` | MUST be zero in version 1 |

The message layout is:

```text
fixed header (48 bytes)
header extensions (header_length - 48 bytes)
payload (payload_length bytes)
```

The total WebSocket message length MUST equal `header_length + payload_length`. `payload_length` MUST NOT exceed 256 KiB in this draft. Receivers MUST reject truncated messages, unsupported versions, nonzero reserved bits/fields, invalid lengths, unknown mandatory extensions, and payload lengths inconsistent with the data type and element count.

Header extensions use this TLV form:

```text
type: u16 big-endian
length: u16 big-endian
value: length bytes
```

Bit 15 of `type` marks a critical extension. Unknown critical extensions MUST be rejected; unknown noncritical extensions MUST be ignored. An extension MUST NOT appear more than once unless that extension explicitly allows repetition.

### 7.1 Timestamp records

Extension type `0x8001` is the critical `TIMESTAMPS` extension. Its value is:

```text
record_count: u8
records: record_count repetitions of:
    role: u8
    timestamp_ns: i64 big-endian, two's complement
```

Role IDs are `1 = MODEL_TIME`, `2 = SOURCE_TIME`, and `3 = PROCESSING_TIME`. Records MUST be ordered by ascending role ID, MUST NOT repeat a role, and the value length MUST equal `1 + 9 * record_count`. An unknown role ID MUST be rejected. Clock-domain identifiers and timestamp origins are declared in the committed stream/node configuration and are not repeated per packet. A receiver MUST verify that the roles present match the stream descriptor.

## 8. Numeric Data Types

The MVP supports fixed-width integers and IEEE 754 interchange binary floating-point values. The following IDs are proposed normative v1 IDs; implementations MUST NOT transmit language-specific object layouts.

Integer values use two's-complement representation for signed types and unsigned representation for unsigned types. IEEE values use their IEEE 754 interchange bit patterns. Multi-byte values are big-endian.

The initial catalog includes:

| ID | Name | Width | Payload representation |
|---:|---|---:|---|
| `0x0001` | `I8` | 8 | Two's-complement signed integer |
| `0x0002` | `U8` | 8 | Unsigned integer |
| `0x0003` | `I12` | 12 | Packed two's-complement signed integer |
| `0x0004` | `U12` | 12 | Packed unsigned integer |
| `0x0005` | `I16` | 16 | Two's-complement signed integer, big-endian |
| `0x0006` | `U16` | 16 | Unsigned integer, big-endian |
| `0x0007` | `I32` | 32 | Two's-complement signed integer, big-endian |
| `0x0008` | `U32` | 32 | Unsigned integer, big-endian |
| `0x0009` | `I64` | 64 | Two's-complement signed integer, big-endian |
| `0x000A` | `U64` | 64 | Unsigned integer, big-endian |
| `0x000B` | `I128` | 128 | Two's-complement signed integer, big-endian |
| `0x000C` | `U128` | 128 | Unsigned integer, big-endian |
| `0x000D` | `F16` | 16 | IEEE 754 binary16 interchange bits, big-endian |
| `0x000E` | `F32` | 32 | IEEE 754 binary32 interchange bits, big-endian |
| `0x000F` | `F64` | 64 | IEEE 754 binary64 interchange bits, big-endian |
| `0x0010` | `F128` | 128 | IEEE 754 binary128 interchange bits, big-endian |
| `0x0101` | `C16` | 32 | Pair of F16 values: real then imaginary |
| `0x0102` | `C32` | 64 | Pair of F32 values: real then imaginary |
| `0x0103` | `C64` | 128 | Pair of F64 values: real then imaginary |
| `0x0104` | `C128` | 256 | Pair of F128 values: real then imaginary |

For `I12` and `U12`, values are packed most-significant bit first, with no padding between values. If the final value leaves unused low-order bits in the final byte, those bits MUST be zero. Signed `I12` uses two's-complement range `-2048..2047`; `U12` uses range `0..4095`. All other multi-byte values use network byte order (big-endian). `element_count` counts logical values; for complex types, it counts complex pairs, not individual real/imaginary components. Payload length MUST equal the encoded size implied by `data_type` and `element_count`.

IEEE NaNs, infinities, and signed zero are transported as their interchange bit patterns without canonicalization. An implementation MAY lack arithmetic support for a type while still transporting it as opaque bytes, but stream setup MUST fail if a receiver cannot consume the declared type. Capability negotiation is defined by the control protocol.

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

Implementations SHOULD reject unsupported types and extensions explicitly rather than reinterpret them. The control protocol's stream descriptor advertises data types, timestamp profiles, gap policy, and protocol versions supported by each participant.

## 12. Items Requiring Review Before Interoperability Freeze

1. Confirm that model time is simulation/model time and that real-time source/processing timestamps use separately declared clock domains.
2. Confirm the timestamp extension and configuration-level clock-domain mapping in Section 7.1.
3. Confirm whether `I128`/`U128` and `F128` are required to be consumable by every runtime or may be advertised as unsupported while still being transported opaquely.
4. Confirm whether packed `I12`/`U12` are required in the initial MVP profile.
5. Define the stream descriptor's exact serialization and the point at which its timestamp domains are updated after a source/runtime restart.
6. Define receiver join/rejoin state and how expected sequence is restored after receiver restart.
7. Define control-plane fault/alarm messages for sequence gaps and malformed packets.
8. Define protocol negotiation for supported packet versions, types, and extensions.

## 13. Relationship to Implementation

The C++ reference transport currently uses a different 54-byte packet layout, a 64-bit stream identifier, a packet-level stream name, and a limited scalar F64 profile. It does not implement the packet format proposed here, persistent sequence behavior, gap handling, or timestamp-role records. The codec MUST NOT be treated as the source of truth. Before interoperability freeze, this candidate must be reviewed and accompanied by byte-exact fixtures for independent C++, Rust, Python, and JVM implementations. A finalized v1 implementation must either implement this format exactly or use a different explicitly versioned format; it MUST NOT silently call the existing layout v1-compatible.
