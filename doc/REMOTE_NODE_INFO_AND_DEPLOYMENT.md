# Remote Node Information Retrieval and Diagram Deployment

## Status

Discussion baseline captured on 2026-09-20.

For a compact implementer view of the control message contract, see:

- `docs/S7_Control_Protocol_MVP.md`

This document records the current design direction for two related concerns:

1. how `s7studio` / `s7ctl` retrieve trustworthy information about a remote headless node;
2. how a diagram/schema is deployed to heterogeneous remote nodes.

It refines the control-plane and deployment work that uses the `s7transport` data/control channel scaffolding. The MVP message and wire-format drafts are maintained in `S7_Control_Protocol_MVP.md` and `S7_Network_Wire_Protocol_v1.md`; this document provides additional capability-descriptor and deployment-model detail.

---

## Why This Matters

A remote `s7runtime` node is not just an addressable process.

It may differ by:

- operating system (`Linux`, `Windows`, `FreeBSD`, `macOS`)
- CPU architecture (`x86_64`, `arm32`, `arm64`, others later)
- available element libraries/plugins
- locally installed hardware adapters
- currently attached hardware devices
- runtime limits and security policy

Because of that, deployment cannot be treated as "send one diagram JSON everywhere and hope it runs".

The control plane must answer two questions before deployment:

1. **Can this node run the assigned part of the diagram?**
2. **If not, exactly what is missing or incompatible?**

---

## Decision Summary

### 1. Remote node information must be capability-based, not hostname-based

A node must expose a machine-readable descriptor of:

- identity
- software version
- transport endpoints
- platform/ABI
- installed element libraries
- element catalog
- hardware capabilities
- current runtime state

This descriptor must be rich enough that a remote controller can answer:

- which libraries are installed?
- which exact elements come from each library?
- what versions/API levels are they built against?
- which elements are deployable now on this node?
- which elements are blocked by missing hardware or policy?

### 2. Deployment must use a two-phase flow

The minimum useful deployment sequence is:

1. `HELLO`
2. `GET_NODE_INFO`
3. local compatibility/preflight in `s7studio` or `s7ctl`
4. `PREPARE_DEPLOYMENT`
5. if all target nodes prepared successfully: `COMMIT_DEPLOYMENT`
6. otherwise: `ABORT_DEPLOYMENT`

### 3. Deployment units must be source-level/capability-level by default, not binary-plugin-level

A deployment bundle should primarily contain:

- canonical schema/diagram definition
- placement manifest
- stream bindings
- deployment metadata
- required capabilities

It should **not** assume that one compiled plugin binary can be copied unchanged to all targets.

### 4. Hardware-backed elements must be advertised as capabilities

Examples such as `PlutoSDR` or `LibreSDR` should appear as deployable capabilities with metadata, not as opaque surprises discovered only at runtime start.

### 5. Node identity must be certificate-bound and permanent

`node_uuid` is part of the node certificate identity and is permanent for the life of that logical node.

This means:

- the certificate SAN identity and runtime-configured `node_uuid` must match;
- replacing a crashed process on the same logical node should reuse the same persistent identity material;
- creating a second node on the same host must create a distinct identity, certificate set, config directory, and deployment state directory.

### 6. The system must tolerate dynamic membership without a centralized registry

Nodes may:

- appear at runtime;
- disappear unexpectedly because of power loss;
- become temporarily unreachable because of network partitions;
- return later with the same persistent identity but a new process instance.

Therefore S7 should not depend on any centralized registry as a correctness requirement.

---

## Control-Plane Model

The existing transport architecture already separates:

- `/control` for JSON messages
- `/data` for binary sample traffic

This document defines what the control path should report.

### Core Messages

#### `HELLO`

Purpose:

- verify reachability
- negotiate protocol version
- retrieve minimal identity

Request:

```json
{
  "type": "HELLO",
  "request_id": "a3d7b808-89f7-4a4f-96ef-4dfc4ab97aa4",
  "protocol_version": 1
}
```

Response:

```json
{
  "type": "HELLO_OK",
  "request_id": "a3d7b808-89f7-4a4f-96ef-4dfc4ab97aa4",
  "protocol_version": 1,
  "node_uuid": "3e89b2be-1a23-4bc8-a73d-7a99ae9c0df2",
  "node_name": "rf-edge-01",
  "runtime_version": "0.1.0",
  "status": "idle"
}
```

#### `GET_NODE_INFO`

Purpose:

- retrieve the full deployability descriptor for a node

Request:

```json
{
  "type": "GET_NODE_INFO",
  "request_id": "dc78bd66-2dca-4d8e-9a41-dda10fbf1ef5"
}
```

Response shape:

```json
{
  "type": "NODE_INFO",
  "request_id": "dc78bd66-2dca-4d8e-9a41-dda10fbf1ef5",
  "node": {
    "node_uuid": "3e89b2be-1a23-4bc8-a73d-7a99ae9c0df2",
    "node_name": "rf-edge-01",
    "display_name": "RF Edge 01",
    "runtime_version": "0.1.0",
    "protocol_version": 1,
    "instance_id": "b64d1eb7-1fad-49a5-b381-1c2d79ed6c6d",
    "started_at": "2026-09-20T10:11:12Z"
  },
  "platform": {
    "os_family": "linux",
    "os_version": "6.8",
    "architecture": "arm64",
    "abi": "gnu",
    "target_triple": "aarch64-linux-gnu",
    "endianness": "little"
  },
  "network": {
    "listen_url": "wss://rf-edge-01.local:8443",
    "control_path": "/control",
    "data_path": "/data"
  },
  "libraries": [
    {
      "library_id": "s7core",
      "kind": "builtin",
      "version": "0.1.0",
      "api_version": 1,
      "target_triple": "aarch64-linux-gnu",
      "origin": "builtin",
      "status": "ready",
      "elements": ["Gain", "Adder", "Delay"]
    },
    {
      "library_id": "s7sdr",
      "kind": "plugin",
      "version": "1.2.0",
      "api_version": 1,
      "target_triple": "aarch64-linux-gnu",
      "origin": "/opt/s7/plugins/libs7sdr.so",
      "status": "degraded",
      "status_reason": "LibreSDR backend unavailable, Pluto backend ready",
      "elements": ["PlutoSource", "PlutoSink", "LibreSdrSource"]
    }
  ],
  "elements": {
    "summary": {
      "count": 27,
      "categories": ["basic", "network", "spectral", "sdr"]
    },
    "catalog": [
      {
        "type_name": "PlutoSource",
        "library_id": "s7sdr",
        "version": "1.2.0",
        "category": "sdr",
        "capability_tags": ["hardware", "source", "hw:plutosdr"],
        "deployable": true,
        "deployable_reason": "required device class available",
        "ports": {
          "inputs": [],
          "outputs": [
            {"name": "out", "signal_type": "REAL"}
          ]
        },
        "parameters": [
          {"name": "sample_rate", "type": "real", "required": false}
        ]
      },
      {
        "type_name": "LibreSdrSource",
        "library_id": "s7sdr",
        "version": "1.2.0",
        "category": "sdr",
        "capability_tags": ["hardware", "source", "hw:libresdr"],
        "deployable": false,
        "deployable_reason": "library present but no compatible device detected"
      }
    ]
  },
  "hardware": {
    "adapters": [
      {
        "adapter_id": "plutosdr",
        "status": "ready",
        "device_count": 1,
        "devices": [
          {
            "device_id": "usb:1-2",
            "model": "ADALM-PLUTO",
            "serial": "1044735f960c",
            "rx_channels": 2,
            "tx_channels": 2
          }
        ]
      }
    ]
  },
  "resources": {
    "cpu_logical_cores": 4,
    "memory_bytes": 4294967296,
    "realtime_hint": false
  },
  "state": {
    "mode": "idle",
    "loaded_deployments": [],
    "active_deployment": null
  }
}
```

#### `PREPARE_DEPLOYMENT`

Purpose:

- validate a candidate deployment against local node capabilities
- reserve resources if validation succeeds
- produce a deterministic result for later commit/abort

#### `COMMIT_DEPLOYMENT`

Purpose:

- activate a previously prepared deployment

#### `ABORT_DEPLOYMENT`

Purpose:

- release reservations and discard prepared state

---

## MVP Discovery and Membership Wire Protocol

This section defines exact JSON message shapes for decentralized peer discovery.

All messages travel on `/control`.

### Common Envelope

All requests and responses use this base shape:

```json
{
  "type": "MESSAGE_TYPE",
  "request_id": "uuid-v4",
  "protocol_version": 1
}
```

Rules:

- `request_id` is mandatory for request/response correlation;
- `protocol_version` must be `1` for MVP;
- unknown fields must be ignored for forward compatibility.

### Trust State Enum (MVP)

Allowed trust states:

- `verified`
- `unverified`
- `rejected`
- `expired`
- `identity_mismatch`

### Node Reachability State Enum (MVP)

Allowed reachability states:

- `online`
- `offline`

### Message: `GET_PEERS`

Purpose:

- ask a node for the peers it currently knows/saw.

Request:

```json
{
  "type": "GET_PEERS",
  "request_id": "8f7d9775-31f1-4d1c-9e65-2f9ce3acdcde",
  "protocol_version": 1,
  "include_unverified": true,
  "max_entries": 512
}
```

Field semantics:

- `include_unverified` (optional, default `true`): include unverified/rejected endpoints in response.
- `max_entries` (optional, default `256`, max `4096`): response size cap.

Response:

```json
{
  "type": "PEERS",
  "request_id": "8f7d9775-31f1-4d1c-9e65-2f9ce3acdcde",
  "protocol_version": 1,
  "generated_at": "2026-09-20T14:10:20Z",
  "peers": [
    {
      "endpoint": "wss://node-b.example.net:8443",
      "node_uuid": "4b9be754-4c0f-4ff5-b048-897f77f1c294",
      "node_name": "node-b",
      "platform": {
        "os_family": "linux",
        "architecture": "arm64"
      },
      "trust_state": "verified",
      "reachability": "online",
      "last_seen_at": "2026-09-20T14:09:01Z",
      "last_failure_at": null,
      "cert_fingerprint_sha256": "9a71c6e6f35a6f8ed55c2cc582a5e111a4ab43bc0f8b6db55f7fcf8e4d4d0b2f"
    },
    {
      "endpoint": "wss://10.0.2.15:8443",
      "node_uuid": null,
      "node_name": null,
      "platform": null,
      "trust_state": "unverified",
      "reachability": "offline",
      "last_seen_at": null,
      "last_failure_at": "2026-09-20T14:08:17Z",
      "cert_fingerprint_sha256": null
    }
  ]
}
```

### Message: `PEER_ANNOUNCE`

Purpose:

- allow a node to proactively announce itself to a known peer.

Request:

```json
{
  "type": "PEER_ANNOUNCE",
  "request_id": "fbb454be-95d9-477d-a4d9-a4439f8cecb8",
  "protocol_version": 1,
  "self": {
    "endpoint": "wss://node-a.example.net:8443",
    "node_uuid": "7fbf65b0-2ac4-427c-8f55-a826d2f89d11",
    "node_name": "node-a",
    "platform": {
      "os_family": "linux",
      "architecture": "x86_64"
    },
    "instance_id": "6de8ef16-f924-4502-a486-8bcb4ce35e9e"
  }
}
```

Response:

```json
{
  "type": "PEER_ANNOUNCE_OK",
  "request_id": "fbb454be-95d9-477d-a4d9-a4439f8cecb8",
  "protocol_version": 1,
  "accepted": true
}
```

### Message: `ERROR`

Purpose:

- return machine-readable control-plane failures.

Response shape:

```json
{
  "type": "ERROR",
  "request_id": "8f7d9775-31f1-4d1c-9e65-2f9ce3acdcde",
  "protocol_version": 1,
  "code": "INVALID_REQUEST",
  "message": "max_entries exceeds allowed limit",
  "details": {
    "field": "max_entries",
    "max": 4096
  }
}
```

Error code set (MVP):

- `INVALID_REQUEST`: malformed payload or invalid field values.
- `UNSUPPORTED_PROTOCOL_VERSION`: protocol mismatch.
- `UNAUTHORIZED`: request not authenticated.
- `CERTIFICATE_REQUIRED`: authenticated channel is not mTLS.
- `CERTIFICATE_INVALID`: certificate chain or validity failure.
- `IDENTITY_MISMATCH`: certificate identity and payload identity disagree.
- `FORBIDDEN`: authenticated identity lacks permission.
- `RATE_LIMITED`: temporary throttling.
- `INTERNAL_ERROR`: unexpected runtime failure.

### JSON Schema: `GET_PEERS` Request (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/get_peers.request.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "GET_PEERS" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "include_unverified": { "type": "boolean", "default": true },
    "max_entries": { "type": "integer", "minimum": 1, "maximum": 4096, "default": 256 }
  }
}
```

### JSON Schema: `PEERS` Response (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/peers.response.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "generated_at", "peers"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "PEERS" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "generated_at": { "type": "string", "format": "date-time" },
    "peers": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["endpoint", "trust_state", "reachability", "last_seen_at", "last_failure_at"],
        "additionalProperties": true,
        "properties": {
          "endpoint": { "type": "string", "minLength": 1 },
          "node_uuid": { "type": ["string", "null"] },
          "node_name": { "type": ["string", "null"] },
          "platform": {
            "type": ["object", "null"],
            "additionalProperties": true,
            "properties": {
              "os_family": { "type": "string" },
              "architecture": { "type": "string" }
            }
          },
          "trust_state": {
            "type": "string",
            "enum": ["verified", "unverified", "rejected", "expired", "identity_mismatch"]
          },
          "reachability": { "type": "string", "enum": ["online", "offline"] },
          "last_seen_at": { "type": ["string", "null"], "format": "date-time" },
          "last_failure_at": { "type": ["string", "null"], "format": "date-time" },
          "cert_fingerprint_sha256": { "type": ["string", "null"], "pattern": "^[A-Fa-f0-9]{64}$" }
        }
      }
    }
  }
}
```

### JSON Schema: `ERROR` Response (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/error.response.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "code", "message"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "ERROR" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "code": {
      "type": "string",
      "enum": [
        "INVALID_REQUEST",
        "UNSUPPORTED_PROTOCOL_VERSION",
        "UNAUTHORIZED",
        "CERTIFICATE_REQUIRED",
        "CERTIFICATE_INVALID",
        "IDENTITY_MISMATCH",
        "FORBIDDEN",
        "RATE_LIMITED",
        "INTERNAL_ERROR"
      ]
    },
    "message": { "type": "string", "minLength": 1 },
    "details": { "type": "object" }
  }
}
```

---

## MVP Deployment Control Wire Protocol

This section defines exact control messages for deployment lifecycle on `/control`.

### Deployment State Enum (Node-Local)

Allowed deployment states:

- `idle`
- `prepared`
- `running`
- `error`

### Prepare Result Enum

Allowed `prepare_status` values:

- `READY`
- `NOT_READY`

### Message: `LIST_DEPLOYMENTS`

Purpose:

- retrieve node-local deployment inventory (active, prepared, history summary).

Request:

```json
{
  "type": "LIST_DEPLOYMENTS",
  "request_id": "9fd967cb-5d53-4f93-a9de-f3750e70700c",
  "protocol_version": 1
}
```

Response:

```json
{
  "type": "DEPLOYMENTS",
  "request_id": "9fd967cb-5d53-4f93-a9de-f3750e70700c",
  "protocol_version": 1,
  "node_uuid": "7fbf65b0-2ac4-427c-8f55-a826d2f89d11",
  "state": "running",
  "active_deployment": {
    "deployment_id": "dep-rf-pipeline-20260920-001",
    "revision": 3,
    "committed_at": "2026-09-20T14:30:31Z"
  },
  "prepared_deployments": [
    {
      "deployment_id": "dep-rf-pipeline-20260920-002",
      "prepare_token": "ptok_2a9b3d1af1b7420e",
      "prepared_at": "2026-09-20T14:32:11Z",
      "expires_at": "2026-09-20T14:37:11Z"
    }
  ],
  "history": [
    {
      "deployment_id": "dep-rf-pipeline-20260920-001",
      "revision": 3,
      "status": "committed",
      "updated_at": "2026-09-20T14:30:31Z"
    }
  ]
}
```

### Message: `PREPARE_DEPLOYMENT`

Purpose:

- validate node-local deployability;
- reserve resources if deployable;
- return deterministic incompatibilities if not deployable.

Request:

```json
{
  "type": "PREPARE_DEPLOYMENT",
  "request_id": "4e4aafca-b100-42da-9529-a5d6735e81f9",
  "protocol_version": 1,
  "deployment": {
    "deployment_id": "dep-rf-pipeline-20260920-010",
    "revision": 1,
    "schema": {
      "name": "rf_pipeline",
      "elements": [],
      "connections": []
    },
    "placement": {
      "node_uuid": "7fbf65b0-2ac4-427c-8f55-a826d2f89d11",
      "assigned_elements": ["src", "fft", "sink"],
      "requires": {
        "libraries": [
          {"library_id": "s7sdr", "version": ">=1.2.0"}
        ],
        "elements": ["PlutoSource", "Fft", "NetSink"],
        "hardware": [
          {"capability": "hw:plutosdr", "min_devices": 1}
        ]
      }
    },
    "stream_bindings": [
      {
        "stream_name": "rf.samples",
        "channel_id": 0,
        "direction": "outbound"
      }
    ],
    "constraints": {
      "max_prepare_seconds": 30
    }
  }
}
```

Success response:

```json
{
  "type": "PREPARE_DEPLOYMENT_RESULT",
  "request_id": "4e4aafca-b100-42da-9529-a5d6735e81f9",
  "protocol_version": 1,
  "deployment_id": "dep-rf-pipeline-20260920-010",
  "revision": 1,
  "prepare_status": "READY",
  "prepare_token": "ptok_3e40cb7bb09f48b2",
  "prepared_at": "2026-09-20T14:35:20Z",
  "expires_at": "2026-09-20T14:40:20Z",
  "reservations": {
    "hardware": [
      {"capability": "hw:plutosdr", "count": 1}
    ],
    "streams": ["rf.samples"]
  },
  "incompatibilities": []
}
```

Failure response:

```json
{
  "type": "PREPARE_DEPLOYMENT_RESULT",
  "request_id": "4e4aafca-b100-42da-9529-a5d6735e81f9",
  "protocol_version": 1,
  "deployment_id": "dep-rf-pipeline-20260920-010",
  "revision": 1,
  "prepare_status": "NOT_READY",
  "prepare_token": null,
  "prepared_at": null,
  "expires_at": null,
  "reservations": {
    "hardware": [],
    "streams": []
  },
  "incompatibilities": [
    {
      "code": "MISSING_ELEMENT",
      "scope": "element",
      "target": "PlutoSource",
      "message": "Required element type is not deployable on this node",
      "details": {
        "library_id": "s7sdr",
        "reason": "device missing"
      }
    },
    {
      "code": "HARDWARE_BUSY",
      "scope": "hardware",
      "target": "hw:plutosdr",
      "message": "Required hardware is currently reserved",
      "details": {
        "reserved_by": "dep-rf-pipeline-20260920-009"
      }
    }
  ]
}
```

### Message: `COMMIT_DEPLOYMENT`

Purpose:

- atomically activate a previously prepared deployment.

Request:

```json
{
  "type": "COMMIT_DEPLOYMENT",
  "request_id": "f18dc469-b7e3-4ebb-b4ef-14c3f0fd294d",
  "protocol_version": 1,
  "deployment_id": "dep-rf-pipeline-20260920-010",
  "revision": 1,
  "prepare_token": "ptok_3e40cb7bb09f48b2"
}
```

Response:

```json
{
  "type": "COMMIT_DEPLOYMENT_RESULT",
  "request_id": "f18dc469-b7e3-4ebb-b4ef-14c3f0fd294d",
  "protocol_version": 1,
  "deployment_id": "dep-rf-pipeline-20260920-010",
  "revision": 1,
  "committed": true,
  "state": "running",
  "committed_at": "2026-09-20T14:36:02Z"
}
```

### Message: `ABORT_DEPLOYMENT`

Purpose:

- release prepared reservations and discard staged deployment state.

Request:

```json
{
  "type": "ABORT_DEPLOYMENT",
  "request_id": "9ce1aa2e-b34a-4d5b-aa34-3336720cbfca",
  "protocol_version": 1,
  "deployment_id": "dep-rf-pipeline-20260920-010",
  "revision": 1,
  "prepare_token": "ptok_3e40cb7bb09f48b2"
}
```

Response:

```json
{
  "type": "ABORT_DEPLOYMENT_RESULT",
  "request_id": "9ce1aa2e-b34a-4d5b-aa34-3336720cbfca",
  "protocol_version": 1,
  "deployment_id": "dep-rf-pipeline-20260920-010",
  "revision": 1,
  "aborted": true,
  "state": "idle",
  "aborted_at": "2026-09-20T14:36:44Z"
}
```

### Incompatibility Code Set (MVP)

Allowed incompatibility `code` values:

- `PLATFORM_MISMATCH`
- `MISSING_LIBRARY`
- `LIBRARY_VERSION_MISMATCH`
- `MISSING_ELEMENT`
- `ELEMENT_BLOCKED`
- `HARDWARE_MISSING`
- `HARDWARE_BUSY`
- `STREAM_CONFLICT`
- `RESOURCE_LIMIT`
- `POLICY_DENIED`

Recommended `scope` values:

- `platform`
- `library`
- `element`
- `hardware`
- `stream`
- `resource`
- `policy`

### Idempotency and Concurrency Rules

- Replaying `PREPARE_DEPLOYMENT` with same `deployment_id` + `revision` may return the same `prepare_token` while still valid.
- `COMMIT_DEPLOYMENT` and `ABORT_DEPLOYMENT` require matching `prepare_token`.
- Unknown or expired `prepare_token` must return `ERROR` with `code: INVALID_REQUEST`.
- Node may keep only one active prepare per `deployment_id` + `revision` for MVP.

### JSON Schema: `PREPARE_DEPLOYMENT_RESULT` Response (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/prepare_deployment.result.v1",
  "type": "object",
  "required": [
    "type",
    "request_id",
    "protocol_version",
    "deployment_id",
    "revision",
    "prepare_status",
    "prepare_token",
    "prepared_at",
    "expires_at",
    "reservations",
    "incompatibilities"
  ],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "PREPARE_DEPLOYMENT_RESULT" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "deployment_id": { "type": "string", "minLength": 1 },
    "revision": { "type": "integer", "minimum": 1 },
    "prepare_status": { "type": "string", "enum": ["READY", "NOT_READY"] },
    "prepare_token": { "type": ["string", "null"] },
    "prepared_at": { "type": ["string", "null"], "format": "date-time" },
    "expires_at": { "type": ["string", "null"], "format": "date-time" },
    "reservations": {
      "type": "object",
      "required": ["hardware", "streams"],
      "properties": {
        "hardware": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["capability", "count"],
            "properties": {
              "capability": { "type": "string", "minLength": 1 },
              "count": { "type": "integer", "minimum": 1 }
            }
          }
        },
        "streams": {
          "type": "array",
          "items": { "type": "string", "minLength": 1 }
        }
      }
    },
    "incompatibilities": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["code", "scope", "target", "message"],
        "properties": {
          "code": {
            "type": "string",
            "enum": [
              "PLATFORM_MISMATCH",
              "MISSING_LIBRARY",
              "LIBRARY_VERSION_MISMATCH",
              "MISSING_ELEMENT",
              "ELEMENT_BLOCKED",
              "HARDWARE_MISSING",
              "HARDWARE_BUSY",
              "STREAM_CONFLICT",
              "RESOURCE_LIMIT",
              "POLICY_DENIED"
            ]
          },
          "scope": {
            "type": "string",
            "enum": ["platform", "library", "element", "hardware", "stream", "resource", "policy"]
          },
          "target": { "type": "string", "minLength": 1 },
          "message": { "type": "string", "minLength": 1 },
          "details": { "type": "object" }
        }
      }
    }
  }
}
```

### JSON Schema: `LIST_DEPLOYMENTS` Request (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/list_deployments.request.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "LIST_DEPLOYMENTS" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 }
  }
}
```

### JSON Schema: `PREPARE_DEPLOYMENT` Request (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/prepare_deployment.request.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "deployment"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "PREPARE_DEPLOYMENT" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "deployment": {
      "type": "object",
      "required": ["deployment_id", "revision", "schema", "placement", "stream_bindings"],
      "additionalProperties": true,
      "properties": {
        "deployment_id": { "type": "string", "minLength": 1 },
        "revision": { "type": "integer", "minimum": 1 },
        "schema": {
          "type": "object",
          "required": ["name", "elements", "connections"],
          "additionalProperties": true,
          "properties": {
            "name": { "type": "string", "minLength": 1 },
            "elements": { "type": "array" },
            "connections": { "type": "array" }
          }
        },
        "placement": {
          "type": "object",
          "required": ["node_uuid", "assigned_elements", "requires"],
          "additionalProperties": true,
          "properties": {
            "node_uuid": { "type": "string", "minLength": 1 },
            "assigned_elements": {
              "type": "array",
              "items": { "type": "string", "minLength": 1 }
            },
            "requires": {
              "type": "object",
              "additionalProperties": true,
              "properties": {
                "libraries": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "required": ["library_id", "version"],
                    "properties": {
                      "library_id": { "type": "string", "minLength": 1 },
                      "version": { "type": "string", "minLength": 1 }
                    }
                  }
                },
                "elements": {
                  "type": "array",
                  "items": { "type": "string", "minLength": 1 }
                },
                "hardware": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "required": ["capability"],
                    "properties": {
                      "capability": { "type": "string", "minLength": 1 },
                      "min_devices": { "type": "integer", "minimum": 1 }
                    }
                  }
                }
              }
            }
          }
        },
        "stream_bindings": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["stream_name", "channel_id", "direction"],
            "properties": {
              "stream_name": { "type": "string", "minLength": 1 },
              "channel_id": { "type": "integer", "minimum": 0 },
              "direction": { "type": "string", "enum": ["inbound", "outbound"] }
            }
          }
        },
        "constraints": {
          "type": "object",
          "additionalProperties": true,
          "properties": {
            "max_prepare_seconds": { "type": "integer", "minimum": 1, "maximum": 600 }
          }
        }
      }
    }
  }
}
```

### JSON Schema: `COMMIT_DEPLOYMENT` Request (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/commit_deployment.request.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "deployment_id", "revision", "prepare_token"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "COMMIT_DEPLOYMENT" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "deployment_id": { "type": "string", "minLength": 1 },
    "revision": { "type": "integer", "minimum": 1 },
    "prepare_token": { "type": "string", "minLength": 1 }
  }
}
```

### JSON Schema: `ABORT_DEPLOYMENT` Request (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/abort_deployment.request.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "deployment_id", "revision", "prepare_token"],
  "additionalProperties": true,
  "properties": {
    "type": { "const": "ABORT_DEPLOYMENT" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "deployment_id": { "type": "string", "minLength": 1 },
    "revision": { "type": "integer", "minimum": 1 },
    "prepare_token": { "type": "string", "minLength": 1 }
  }
}
```

### JSON Schema: `COMMIT_DEPLOYMENT_RESULT` Response (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/commit_deployment.result.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "deployment_id", "revision", "committed", "state", "committed_at"],
  "properties": {
    "type": { "const": "COMMIT_DEPLOYMENT_RESULT" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "deployment_id": { "type": "string", "minLength": 1 },
    "revision": { "type": "integer", "minimum": 1 },
    "committed": { "type": "boolean" },
    "state": { "type": "string", "enum": ["idle", "prepared", "running", "error"] },
    "committed_at": { "type": "string", "format": "date-time" }
  }
}
```

### JSON Schema: `ABORT_DEPLOYMENT_RESULT` Response (Draft 2020-12)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "s7/control/abort_deployment.result.v1",
  "type": "object",
  "required": ["type", "request_id", "protocol_version", "deployment_id", "revision", "aborted", "state", "aborted_at"],
  "properties": {
    "type": { "const": "ABORT_DEPLOYMENT_RESULT" },
    "request_id": { "type": "string", "minLength": 1 },
    "protocol_version": { "const": 1 },
    "deployment_id": { "type": "string", "minLength": 1 },
    "revision": { "type": "integer", "minimum": 1 },
    "aborted": { "type": "boolean" },
    "state": { "type": "string", "enum": ["idle", "prepared", "running", "error"] },
    "aborted_at": { "type": "string", "format": "date-time" }
  }
}
```

---

## MVP Implementation Conformance Checklist

Use this checklist to verify an implementation matches this document.

### Transport and Security

- `/control` uses strict mTLS for trusted membership decisions.
- certificate chain validation is enforced against configured trust roots.
- certificate identity and `node_uuid` binding are checked and enforced.

### Discovery and Membership

- supports manual seed input from headless config and Studio form.
- persists canonical seed endpoints as full URLs (`wss://host:port`).
- accepts hostname/IP shorthand and normalizes to canonical URL.
- supports `GET_PEERS` and returns `PEERS` payload with required fields.
- tracks trust states: `verified`, `unverified`, `rejected`, `expired`, `identity_mismatch`.
- tracks reachability states: `online`, `offline`.
- maintains last-seen cache with configurable retention (default `24h`).
- does not require centralized registry for correctness.

### Node Information

- `GET_NODE_INFO` returns identity, platform, network, libraries, element catalog summary, hardware summary, and runtime state.
- distinguishes element availability from element deployability.
- includes per-library status and status reason when degraded/blocked.

### Deployment Lifecycle

- supports `LIST_DEPLOYMENTS`, `PREPARE_DEPLOYMENT`, `COMMIT_DEPLOYMENT`, `ABORT_DEPLOYMENT`.
- `PREPARE_DEPLOYMENT` returns deterministic `READY`/`NOT_READY` result.
- `NOT_READY` includes structured incompatibilities with `code`, `scope`, `target`, `message`.
- `COMMIT_DEPLOYMENT` and `ABORT_DEPLOYMENT` require valid matching `prepare_token`.
- prepare token expiry/unknown token returns `ERROR` (`INVALID_REQUEST`).

### Error Semantics

- `ERROR` response uses standardized error code set from this document.
- protocol mismatch returns `UNSUPPORTED_PROTOCOL_VERSION`.
- unauthenticated/invalid certificate paths return security-specific error codes.

### Studio UX

- trusted and untrusted nodes are shown in separate groups.
- untrusted nodes are diagnostics-only and not deploy targets.
- separate per-node and network topology views are present.
- double-clicking a node reuses existing node tab if already open.
- node tab shows deployed diagram with live side panel context.

---

## Node Information Model

A remote node descriptor should be divided into stable sections.

### 1. Identity

Stable identity fields:

- `node_uuid`: persistent runtime-node UUID
- `node_name`: operator-friendly stable identifier
- `display_name`: optional GUI label
- `runtime_version`
- `protocol_version`
- `instance_id`: ephemeral boot/session ID

Permanent identity rule:

- `node_uuid` is anchored in the node certificate identity;
- it must survive process restarts, host reboots, and transient network outages;
- it must not be regenerated on every runtime launch.

Why both `node_uuid` and `instance_id`:

- `node_uuid` identifies the logical node across restarts
- `instance_id` tells operators they are talking to a fresh process after a reboot/crash/restart

### Certificate Binding

Recommended validation rule:

- the node certificate SAN URI contains `spiffe://s7/node/<node_uuid>`;
- the runtime refuses to start control-plane service if configured `node_uuid` does not match certificate identity;
- controllers treat certificate identity as authoritative and treat JSON identity fields as claims that must agree with certificate identity.

### Per-Node State on Shared Hosts

If multiple nodes run on one host, each node requires its own persistent directory tree.

Recommended structure:

```text
<base>/nodes/
  <node_uuid>/
    config/
      runtime.json
      peers.json
    certs/
      ca.pem
      node.crt
      node.key
    deployments/
      active/
      prepared/
      history/
    state/
      runtime-state.json
      health.json
    logs/
```

Rules:

- no two logical nodes share the same state directory;
- deployment staging data is node-local;
- certificate/key material is node-local;
- runtime crash recovery uses node-local deployment and state records.

### 2. Platform

Deployment compatibility depends on platform facts, not assumptions.

Required fields:

- `os_family`: `linux`, `windows`, `freebsd`, `macos`
- `os_version`
- `architecture`: `x86_64`, `arm32`, `arm64`
- `target_triple` or equivalent ABI tuple
- `endianness`

Recommended fields:

- `libc`: `glibc`, `musl`, `msvc`, `libSystem`, etc.
- `containerized`: boolean
- `virtualized`: boolean

### 3. Software Inventory

A node must report both libraries and the elements they expose.

For each installed library/plugin, report:

- `library_id`: logical identifier, e.g. `s7spectral`, `s7sdr`
- `kind`: `builtin`, `plugin`, `schema-library`, `vendor-plugin`
- `version`
- `api_version`
- `build_target` / `target_triple`
- `origin` (builtin / plugin path / package id)
- `status` (`ready`, `degraded`, `blocked`)
- `status_reason`
- `elements`: list of registered type names

Additionally, the node should expose a richer library descriptor with:

- plugin ABI version
- library build timestamp or build ID when available
- declared dependencies on other libraries/adapters
- whether the library loaded successfully but only partially activated

Important distinction:

- **library installed** does not always mean **all elements deployable**;
- a plugin may load correctly while some hardware-backed elements remain unavailable.

Important rule:

> Deployment matching must be based on logical library and element identifiers, not shared-library filenames.

That avoids OS-specific filename coupling such as `.so` vs `.dll` vs `.dylib`.

### 4. Element Catalog

There are two useful reporting levels:

1. **summary level** for fast node list views
2. **full catalog level** for detailed inspection and deploy-time validation

The full catalog should include per element:

- type name
- version
- category
- ports
- parameters
- optional capability tags such as `hardware`, `network`, `stateful`, `real-time-sensitive`
- source library identifier
- deployability status
- deployability reason
- platform restrictions, if any
- hardware requirements, if any

Recommended semantics:

- `available`: the element is registered in the catalog;
- `deployable`: the element can be instantiated right now on this node;
- `blocked`: the element exists in software but cannot currently be deployed because of hardware, licensing, policy, or dependency issues.

This distinction matters for elements such as `PlutoSource` and `LibreSdrSource`.

### 5. Hardware Capability Inventory

Hardware support must be explicit and normalized.

A node should report:

- installed hardware adapters/drivers
- whether each adapter is usable now
- detected devices
- device properties relevant to deployment
- whether the device is exclusive-use or shareable

Example capability tags:

- `hw:plutosdr`
- `hw:libresdr`
- `hw:rtl-sdr`
- `hw:audio:alsa`
- `hw:audio:wasapi`
- `hw:audio:coreaudio`

This lets deployment reason about both element availability and hardware availability.

It also lets the controller explain failures precisely, for example:

- "element library installed, but device missing"
- "device present, but driver backend unavailable"
- "device present, but reserved by active deployment"

### 6. Runtime State

A node info response should also include current state:

- `idle`
- `preparing`
- `running`
- `error`
- `degraded`

And deployment-related fields:

- current deployment ID
- prepared deployment IDs
- last deployment error summary
- uptime / started-at timestamp

---

## Diagram Deployment Model

## Principle

A diagram is a **logical system description**.

A deployment is a **mapping of that logical system onto specific nodes with specific capabilities**.

Those must remain separate artifacts.

### Recommended Artifacts

#### 1. Schema / Diagram

Contains logical graph structure:

- elements
- parameters
- connections

#### 2. Placement Manifest

Contains deployment intent:

- which node each element or partition should run on
- required capabilities
- stream bindings
- optional affinity or anti-affinity rules
- required library constraints
- required element constraints
- required hardware constraints

#### 3. Deployment Bundle

Contains:

- schema JSON
- placement manifest
- metadata (`deployment_id`, version, author, timestamp)
- optional immutable assets needed by elements
- optional per-platform artifacts when absolutely required

### What Should Not Be Assumed

A deployment bundle should not assume:

- every node runs Linux
- every node uses the same CPU architecture
- every node has the same plugins installed
- every node has the same hardware physically attached

---

## Capability-Based Deployment

### Deployment Preconditions

Before deployment, `s7studio` or `s7ctl` should evaluate:

1. does the target node expose all referenced element types?
2. do referenced element versions satisfy the schema requirements?
3. are required hardware capabilities present?
4. is the node platform compatible with any attached artifacts?
5. are runtime policy constraints satisfied?

### Example Requirement Model

A placement fragment may declare:

```json
{
  "node": "rf-edge-01",
  "requires": {
    "elements": ["PlutoSource", "Fft", "NetSink"],
    "libraries": [
      {"library_id": "s7sdr", "version": ">=1.2.0"},
      {"library_id": "s7spectral", "version": ">=1.0.0"}
    ],
    "hardware": ["hw:plutosdr"],
    "platform": {
      "os_family": ["linux"],
      "architecture": ["arm64", "x86_64"]
    }
  }
}
```

### Why This Is Better Than Shipping Raw Binaries

If `s7studio` shipped a Linux `x86_64` plugin binary to a Windows `arm64` node, deployment would fail by construction.

Therefore:

- the **default** deployment mode should require compatible capabilities already installed on the node;
- a **future** artifact-delivery mode may distribute platform-specific plugin packages, but only with explicit per-target packaging metadata.

This is especially important in a mixed fleet where different nodes may run Linux, Windows, FreeBSD, or macOS, and may host different optional vendor SDKs.

---

## Handling Heterogeneous Platforms

### OS and Architecture Are First-Class Compatibility Fields

A node running:

- Linux ARM32
- Linux ARM64
- Windows x86_64
- FreeBSD x86_64
- macOS arm64

must all be representable without ambiguity.

Recommended normalized fields:

- `os_family`
- `architecture`
- `target_triple`
- `library_package_format`

Example values:

- `x86_64-linux-gnu`
- `aarch64-linux-gnu`
- `armv7-linux-gnueabihf`
- `x86_64-windows-msvc`
- `arm64-macos`

### Packaging Rule

If deployment ever includes optional binaries, those binaries must be attached in a target-specific matrix, for example:

```json
{
  "artifacts": [
    {
      "library_id": "s7sdr",
      "version": "1.2.0",
      "targets": [
        {"target_triple": "x86_64-linux-gnu", "uri": "..."},
        {"target_triple": "aarch64-linux-gnu", "uri": "..."},
        {"target_triple": "x86_64-windows-msvc", "uri": "..."}
      ]
    }
  ]
}
```

That is explicitly a later phase, not required for the first control-plane MVP.

---

## Hardware-Aware Deployment

Hardware-backed elements require two validations.

### Validation 1: Element Availability

Example:

- does this node have `PlutoSource` registered as an element type?

### Validation 2: Device Availability

Example:

- is there an actual PlutoSDR device attached and usable right now?

Both must pass.

### Device Matching

When a schema depends on a specific class of hardware, placement should optionally refine by:

- adapter type
- model
- serial number
- channel count
- sample-rate range
- exclusive/shared access policy

Example requirement:

```json
{
  "hardware": [
    {
      "capability": "hw:plutosdr",
      "min_devices": 1,
      "constraints": {
        "rx_channels": ">=2"
      }
    }
  ]
}
```

---

## Proposed Prepare/Commit Semantics

### `PREPARE_DEPLOYMENT`

Input should include:

- deployment ID
- schema
- placement subset for this node
- stream endpoint/binding assignments
- capability requirements

Node behavior:

1. validate protocol and payload
2. validate required elements are installed
3. validate library/API compatibility
4. validate hardware availability
5. validate local resource limits
6. reserve resources if possible
7. return `READY` or structured incompatibilities

Example failure response reasons:

- missing element type
- plugin version too old
- unsupported OS/architecture
- required hardware absent
- device busy
- insufficient memory/resources

Structured incompatibilities should identify whether the failure belongs to:

- platform mismatch
- missing library
- library version mismatch
- missing element type
- element blocked by local policy
- missing hardware device
- hardware device busy
- stream binding/resource conflict

This makes Studio and `s7ctl` diagnostics actionable.

---

## Dynamic Membership and Decentralized Discovery

### Core Requirement

The system must remain operational without any centralized registry service.

That means:

- there is no single authoritative server whose loss makes discovery impossible;
- each node can be contacted directly if at least one reachable address is known;
- membership views are eventually consistent and may temporarily differ between peers.

### Recommended Discovery Model

Use a decentralized peer model with these ingredients:

1. **static seed peers** in local config;
2. **peer exchange** in control-plane responses;
3. **last-known peer cache** in node-local persistent state;
4. **soft-state liveness** based on heartbeat/hello expiry.

Practical flow:

1. a node starts with configured peers from its own config directory;
2. it connects to any reachable peer;
3. peers exchange their known peer sets;
4. nodes gradually learn about additional nodes;
5. unreachable nodes are marked suspect/offline, not immediately forgotten forever.

### Dynamic Churn Semantics

Nodes may disappear because of:

- power loss
- reboot
- process crash
- Wi-Fi outage / switch failure / routing issue

Controllers and peer nodes must distinguish:

- **unknown**: never seen before
- **online**: recently reachable
- **suspect**: missed expected heartbeat/refresh window
- **offline**: confirmed unreachable after retry policy
- **stale**: remembered from cache but not recently rediscovered

When a node returns:

- the same `node_uuid` indicates the same logical node;
- a different `instance_id` indicates a new runtime process incarnation.

### No Central Registry Rule

There may be helper tooling later for convenience, but not as a required source of truth.

Authoritative sources remain:

- each node's own certificate-bound identity;
- each node's own control-plane self-description;
- direct peer-to-peer discovery and cached peer knowledge.

### Implication for `s7studio`

Studio should maintain a local working set of known nodes gathered from:

- manually entered seed nodes;
- previously seen nodes cached locally;
- peer lists learned from connected nodes.

Studio must tolerate partial visibility:

- not all nodes are always visible;
- different peers may report different subsets;
- topology views must represent uncertainty rather than assuming a perfect global snapshot.

---

## MVP Profile: Manual Seeds + Peer Sharing + Trust Tiers

This section captures the agreed MVP behavior.

### Finalized MVP Defaults (Decision Lock)

The following defaults are locked for MVP:

- seed entry points: both headless config and `s7studio` form;
- canonical seed format: full URL (`wss://host:port`), with hostname/IP shorthand accepted and normalized;
- peer exchange payload: endpoint + last_seen + trust_state + claimed `node_uuid`/`node_name` + optional platform summary;
- trust states: `verified`, `unverified`, `rejected`, `expired`, `identity_mismatch`;
- untrusted node policy: visible for diagnostics only, no deployment actions;
- liveness model: simple `online` / `offline` for MVP;
- last-seen retention: configurable, default `24h`;
- node tab interaction: reuse existing tab for the same node if already open;
- node tab content: deployed diagram plus live side panel (node/library/hardware/deployment state);
- security mode: strict mTLS only for trusted membership.

### 1. Bootstrap from manually configured well-known peers

For MVP, discovery starts from user-provided peer addresses.

Users can enter one or more seed endpoints:

- in headless node config;
- in `s7studio` connect/discovery form.

Accepted seed forms:

- IP address
- hostname
- optional explicit port

Canonical persisted form:

- full URL, for example `wss://node-a.example.net:8443`

If a user enters hostname/IP shorthand, Studio/runtime normalizes it to canonical URL form before storing.

The node/studio then contacts any reachable seed and requests known peers.

### 2. Peer sharing (decentralized gossip-lite)

Each reachable peer should report nodes it currently knows or recently saw.

Minimum peer descriptor fields exchanged:

- `node_uuid` (if known and verified)
- `node_name` (if known)
- endpoint URL/address
- last-seen timestamp
- trust state (`verified`, `unverified`, `rejected`)

Optional MVP field:

- platform summary (`os_family`, `architecture`) when known

No centralized registry is required for this process.

### 3. Dynamic last-seen cache on every node

Each node keeps a local dynamic list of last-seen nodes.

For each entry, store at least:

- endpoint
- last successful contact time
- last failure time
- certificate verification result
- most recently observed identity claims

This cache is soft-state and may expire old entries by policy.

### 4. Membership requires valid certificate proof

A node is considered a trusted member only if it proves ownership of a properly signed X.509 certificate.

Certificate checks for MVP:

- certificate chain validates against configured trust roots;
- certificate validity period is current;
- certificate identity matches presented node identity (`node_uuid` binding rule);
- optional revocation checks can be added later.

Nodes that fail these checks are not accepted as trusted members.

### 5. Untrusted nodes are still visible, but isolated in UI

`s7studio` should display two trust groups:

1. **trusted/verified nodes** (normal operational list);
2. **unverified or certificate-failed nodes** (separate list/panel).

This allows operators to inspect suspicious or misconfigured endpoints without mixing them into normal deployment targets.

For MVP, untrusted nodes are diagnostics-only and cannot be used as deployment targets.

---

## MVP Studio Views and Interaction

### Node-Centric Views

Studio should provide a separate view per node, including:

- node identity and trust status;
- platform and capabilities summary;
- deployment status;
- active diagram/deployment reference.

### Network Diagram View

Studio should also provide a separate network-level diagram/topology view showing:

- discovered trusted nodes;
- optional untrusted nodes with distinct visual style;
- link/discovery relationships;
- online/suspect/offline status markers.

For MVP, use only `online` and `offline` status markers.

### Double-Click Behavior

On double-clicking a node in Studio:

- open a node-dedicated tab, reusing an existing tab for that node when already open;
- show the diagram currently deployed on that node (or empty-state if none);
- keep node-local context in that tab (status, element/library/hardware info, deployment actions).

This supports fast operator drill-down from global network view to per-node details.

### `COMMIT_DEPLOYMENT`

If all nodes return `READY`, coordinator sends `COMMIT_DEPLOYMENT`.

Node behavior:

- finalize deployment
- instantiate runtime objects
- open streams
- transition to running state

### `ABORT_DEPLOYMENT`

If any node fails prepare:

- release reservations
- discard staged deployment state
- return to idle

---

## What `s7studio` Should Show

For each remote node, Studio should eventually display at least:

- node name / UUID
- online/offline state
- OS + architecture
- runtime version
- installed libraries
- available hardware adapters/devices
- deployment state
- compatibility status for the currently open diagram

Recommended deploy UX:

1. open diagram
2. fetch node info from selected nodes
3. show compatibility matrix
4. highlight missing libraries/hardware before deploy
5. allow prepare
6. allow commit only when all targets are ready

---

## Minimal MVP Recommendation

For the first implementation milestone, keep scope narrow.

### Implement First

1. persistent `node_uuid` + `node_name`
2. certificate binding for `node_uuid`
3. node-local state/config/cert/deployment directories
4. `HELLO`
5. `GET_NODE_INFO`
6. `LIST_DEPLOYMENTS`
7. `PREPARE_DEPLOYMENT`
8. `COMMIT_DEPLOYMENT`
9. `ABORT_DEPLOYMENT`
10. static seeds + peer exchange + liveness cache
11. trust-tiered Studio node lists (verified vs unverified)
12. Studio per-node view + separate network diagram view
13. node double-click opens node-specific diagram tab

### `GET_NODE_INFO` MVP Fields

At minimum return:

- node identity
- runtime version
- OS family
- architecture
- target triple
- loaded libraries/plugins
- library-level status and version metadata
- element-level catalog summary and deployability state
- attached hardware capability summary
- current deployment state
- data protocol (`data_protocol`) and clock domains (`clock_domains`), see `S7_Control_Protocol_MVP.md` section 5.2

### Discovery/Membership MVP Inputs

At minimum support:

- configured list of well-known peer hostnames/IPs;
- peer-reported known nodes;
- per-node last-seen cache;
- certificate verification result per discovered endpoint.

### Defer Until Later

- binary plugin distribution
- automatic plugin installation
- topology auto-layout
- distributed live migration

---

## Design Rules Captured From This Discussion

1. **Node discovery and deployment must be capability-driven.**
2. **Element-library compatibility must be explicit.**
3. **Element availability and element deployability are different concepts.**
4. **Hardware-backed elements require both software and device validation.**
5. **Platform heterogeneity is normal, not exceptional.**
6. **Deployment should default to source/schema + manifest, not raw shared binaries.**
7. **`node_uuid` is permanent and certificate-bound.**
8. **Multiple nodes on one host require fully separate persistent directories and credentials.**
9. **Discovery must remain decentralized and tolerate churn.**
10. **Prepare/commit is required for safe multi-node deployment.**

---

## Follow-Up Questions

These topics remain for subsequent discussions:

1. decentralized peer-discovery details (peer exchange message shape, heartbeat, eviction rules)
2. deployment bundle file format
3. topology display and live status overlays in `s7studio`
4. remote element/library version constraints and upgrade policy
5. remote hardware reservation/conflict policy
