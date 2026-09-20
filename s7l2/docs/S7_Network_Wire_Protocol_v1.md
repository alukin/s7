# S7 Network Wire Protocol v1

## Status

Draft v1 baseline for implementation on Beast WebSocket transport.

This document defines the wire protocol carried over one TLS port with separate control and data WebSocket channels.

---

## Transport Envelope

- TLS: mutual TLS required
- Port: single external TLS port per node
- WebSocket paths:
  - `/control` (JSON frames)
  - `/data` (binary frames)

Identity source:

- SAN URI `spiffe://s7/node/<uuid>`
- role from Certificate Policies OID (`runtime-node` or `control-node`)

---

## Control Channel (`/control`)

Encoding: UTF-8 JSON object per message.

Base message envelope:

```json
{
  "type": "PREPARE_STREAM",
  "msg_id": "uuid",
  "session_id": "uuid",
  "from_node": "uuid",
  "to_node": "uuid",
  "ts_system_ns": 0,
  "payload": {}
}
```

Required behavior:

- only certs with control-node role may send mutating control messages
- every mutating operation must be transactional (`prepare` then `commit`)
- idempotency keyed by `msg_id`

### Control Message Types

Session and health:

- `HELLO`
- `HEARTBEAT`
- `GOODBYE`

Stream lifecycle:

- `PREPARE_STREAM`
- `PREPARE_OK`
- `PREPARE_FAIL`
- `COMMIT_STREAM`
- `COMMIT_OK`
- `COMMIT_FAIL`
- `ABORT_STREAM`

Flow control:

- `WINDOW_UPDATE`
- `PAUSE_STREAM`
- `RESUME_STREAM`

Observation lifecycle:

- `OBSERVE_PREPARE`
- `OBSERVE_PREPARE_OK`
- `OBSERVE_PREPARE_FAIL`
- `OBSERVE_COMMIT`
- `OBSERVE_COMMIT_OK`
- `OBSERVE_COMMIT_FAIL`
- `OBSERVE_ABORT`
- `OBSERVE_STOP`

Faults:

- `ERROR`
- `ALARM`

---

## Data Channel (`/data`)

Encoding: binary frame.

Max payload size: `256 KiB` per packet payload.

All integer and floating numeric metadata fields are encoded in network byte order (big-endian).

### Binary Packet Header (fixed)

```text
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-------------------------------+-------------------------------+
|           version             |             flags             |
+-------------------------------+-------------------------------+
|                         global_seq (u64)                      |
+---------------------------------------------------------------+
|                         stream_id (u64)                       |
+---------------------------------------------------------------+
|                        channel_id (u32)       | data_type(u16)|
+---------------------------------------------------------------+
| elem_count (u32)           | payload_size_bytes (u32)         |
+---------------------------------------------------------------+
|                        t_model_ns (u64)                       |
+---------------------------------------------------------------+
|                        t_system_ns (u64)                      |
+---------------------------------------------------------------+
| ext_len_bytes (u16)         | reserved (u16)                   |
+---------------------------------------------------------------+
| extension bytes ...                                             |
+---------------------------------------------------------------+
|                        payload bytes ...                      |
+---------------------------------------------------------------+
```

Header fields:

- `version`: protocol version (`1`)
- `flags`: bit flags (timestamp presence, control markers, etc.)
- `global_seq`: strict monotonically increasing sequence per connection
- `stream_id`: logical stream identifier
- `channel_id`: logical channel inside stream
- `data_type`: scalar type identifier
- `elem_count`: element count in payload
- `payload_size_bytes`: payload length
- `t_model_ns`: modeling timestamp in nanoseconds (required)
- `t_system_ns`: system/device timestamp in nanoseconds (required for hardware-origin streams)
- `ext_len_bytes`: extension area length for forward-compatible metadata
- `reserved`: must be zero in v1

Hardware-origin rule:

- `t_system_ns` must be present and validated during `PREPARE_STREAM`.

### Data Type IDs (v1)

- `I8`, `U8`, `I12`, `U12`, `I16`, `U16`, `I32`, `U32`, `I64`, `U64`
- `F32`, `F64`, `F128`

`F128` support is capability-gated during `PREPARE_STREAM`.

---

## Ordering and Reliability

Rules:

- global ordering applies across all channels for one data connection
- receiver rejects duplicate or regressed `global_seq`
- all samples must be delivered (no drop policy)

### Backpressure and Buffering

- sender and receiver maintain bounded queues
- receiver reports credits/windows via `WINDOW_UPDATE`
- sender must stop pushing when window exhausted
- buffering is allowed, but sustained overrun triggers explicit stream fault

Traffic priority policy (highest to lowest):

1. control channel traffic
2. element-connection data traffic (functional graph data transport)
3. observation traffic

Scheduling and arbitration must enforce this order under contention.

Observation streams are therefore lower priority than element-connection streams.

### Observation Data Streams

Observation data uses the same `/data` binary envelope and reliability guarantees.

Additional required observation metadata in packet extension area:

- `obs_source_node_uuid`
- `obs_source_schema_instance_uuid`
- `obs_source_element_instance_uuid`
- `obs_source_port_index`
- optional `obs_source_channel_index`

Observation is read-only and must not alter functional graph causality or execution order.

All observation samples must still be delivered; if pressure occurs, functional data keeps priority while observation flow is throttled first via window/backpressure.

---

## Prepare/Commit Transaction

A stream is usable only after successful transaction.

1. `PREPARE_STREAM`
2. receiver validates and responds `PREPARE_OK` or `PREPARE_FAIL`
3. initiator sends `COMMIT_STREAM`
4. receiver responds `COMMIT_OK` or `COMMIT_FAIL`
5. data packets may flow only after `COMMIT_OK`

Sanity checks in prepare stage:

- cert role authorization
- type compatibility (`data_type`, including `F128` support)
- timestamp mode compatibility
- stream and channel resource allocation
- routing and endpoint validation
- buffer/window policy compatibility

Observation subscriptions use the same transaction model with `OBSERVE_PREPARE` and `OBSERVE_COMMIT`.

Observation-specific sanity checks:

- probe existence and addressability (node/schema instance/element instance/port)
- control-node certificate role authorization
- observer stream resource budget
- timestamp and type compatibility

---

## Security Checks

Before accepting control or data frames:

- mTLS chain valid against trusted-CA manifest
- detached manifest signature file (`.sig`) valid against primary CA
- SAN URI node UUID matches declared node identity
- role policy allows requested operation

---

## Error Handling

Control channel errors use structured JSON payload:

```json
{
  "code": "PREPARE_RESOURCE_EXHAUSTED",
  "message": "Insufficient buffer budget",
  "details": {
    "stream_id": "123",
    "required_bytes": 262144,
    "available_bytes": 32768
  }
}
```

Data channel protocol violations (e.g., wrong `global_seq`) must raise `ALARM` and close stream/session depending on severity.

---

## Open Questions

No unresolved protocol questions remain for v1 baseline in this document.

