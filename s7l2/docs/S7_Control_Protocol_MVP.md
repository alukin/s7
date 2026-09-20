# S7 Control Protocol MVP

## Status

Normative MVP protocol reference (v1).

This document defines the control-plane message contract for decentralized discovery and deployment orchestration over `/control`.

---

## Scope

Included in MVP:

- node hello and identity bootstrap
- node capability retrieval
- decentralized peer discovery
- deployment lifecycle (`prepare`/`commit`/`abort`)
- standardized error semantics

Out of scope for MVP:

- centralized registry service
- plugin binary distribution/install
- advanced liveness grades beyond `online`/`offline`

---

## Transport and Security

- Transport path: `/control` WebSocket
- Encoding: JSON
- Security: strict mTLS for trusted membership
- Node identity: certificate-bound `node_uuid` (SAN URI `spiffe://s7/node/<node_uuid>`)

A node is trusted only when certificate validation succeeds and identity binding matches.

---

## Common Envelope

All request/response messages include:

```json
{
  "type": "MESSAGE_TYPE",
  "request_id": "uuid-v4-or-equivalent",
  "protocol_version": 1
}
```

Rules:

- `request_id` is required for request/response correlation.
- `protocol_version` is required and must be `1` in MVP.
- Unknown fields must be ignored for forward compatibility.

---

## Enums

### Trust State

- `verified`
- `unverified`
- `rejected`
- `expired`
- `identity_mismatch`

### Reachability State (MVP)

- `online`
- `offline`

### Deployment State (Node-Local)

- `idle`
- `prepared`
- `running`
- `error`

### Prepare Status

- `READY`
- `NOT_READY`

---

## Message Set

## 1. Hello and Node Info

### `HELLO`

Purpose:

- reachability check
- protocol negotiation
- minimal identity snapshot

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

### `GET_NODE_INFO`

Purpose:

- full capability descriptor for deployment preflight

Request:

```json
{
  "type": "GET_NODE_INFO",
  "request_id": "dc78bd66-2dca-4d8e-9a41-dda10fbf1ef5",
  "protocol_version": 1
}
```

Response (`NODE_INFO`) includes at minimum:

- `node` identity (`node_uuid`, `node_name`, `instance_id`, runtime info)
- `platform` (`os_family`, `architecture`, `target_triple`)
- `network` (`listen_url`, control/data paths)
- `libraries` with status metadata
- `elements` summary and deployability metadata
- `hardware` adapter/device summary
- `state` deployment/runtime status

---

## 2. Discovery and Membership

### `GET_PEERS`

Purpose:

- retrieve known peers from a node-local last-seen cache

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

Response (`PEERS`) includes entries with:

- `endpoint`
- `node_uuid` / `node_name` when known
- optional `platform` summary
- `trust_state`
- `reachability`
- `last_seen_at`
- `last_failure_at`
- optional `cert_fingerprint_sha256`

### `PEER_ANNOUNCE`

Purpose:

- proactive self-announcement to a known peer

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

---

## 3. Deployment Lifecycle

### `LIST_DEPLOYMENTS` -> `DEPLOYMENTS`

Purpose:

- list active/prepared/history deployment records on node

### `PREPARE_DEPLOYMENT` -> `PREPARE_DEPLOYMENT_RESULT`

Purpose:

- validate assigned placement subset
- reserve resources if deployable
- return structured incompatibilities when not deployable

Result:

- `prepare_status: READY` with `prepare_token`, reservation window
- `prepare_status: NOT_READY` with `incompatibilities[]`

### `COMMIT_DEPLOYMENT` -> `COMMIT_DEPLOYMENT_RESULT`

Purpose:

- activate previously prepared deployment

Requires:

- matching valid `prepare_token`

### `ABORT_DEPLOYMENT` -> `ABORT_DEPLOYMENT_RESULT`

Purpose:

- release reservations and discard staged prepare state

Requires:

- matching valid `prepare_token`

---

## Incompatibility Taxonomy (`PREPARE_DEPLOYMENT_RESULT`)

Allowed `code` values:

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

- `platform`, `library`, `element`, `hardware`, `stream`, `resource`, `policy`

Each incompatibility should include:

- `code`
- `scope`
- `target`
- `message`
- optional `details`

---

## Error Message

### `ERROR`

```json
{
  "type": "ERROR",
  "request_id": "same-as-request",
  "protocol_version": 1,
  "code": "INVALID_REQUEST",
  "message": "human-readable summary",
  "details": {}
}
```

Allowed error codes (MVP):

- `INVALID_REQUEST`
- `UNSUPPORTED_PROTOCOL_VERSION`
- `UNAUTHORIZED`
- `CERTIFICATE_REQUIRED`
- `CERTIFICATE_INVALID`
- `IDENTITY_MISMATCH`
- `FORBIDDEN`
- `RATE_LIMITED`
- `INTERNAL_ERROR`

---

## Idempotency and Token Rules

- `PREPARE_DEPLOYMENT` may return the same valid token when replayed for same `deployment_id` + `revision`.
- `COMMIT_DEPLOYMENT` and `ABORT_DEPLOYMENT` must validate token ownership and expiry.
- Unknown/expired token returns `ERROR` with `code: INVALID_REQUEST`.

---

## Canonicalization Rules (Seeds)

- Seed peers may be entered as full URL or hostname/IP shorthand.
- Persist canonical form as full URL: `wss://host:port`.
- Default port may be injected by local policy during normalization.

---

## Studio UX Requirements (Protocol-Driven)

- Separate trusted and untrusted node lists.
- Untrusted nodes are diagnostics-only (not deployment targets).
- Provide per-node view and network topology view.
- Double-click node reuses existing node tab when already open.
- Node tab displays deployed diagram plus live node/library/hardware panel.

---

## Normative Source and Schemas

Normative examples and JSON Schemas are maintained in:

- `docs/REMOTE_NODE_INFO_AND_DEPLOYMENT.md`

This file is the compact implementer reference. If conflicts appear, follow `docs/REMOTE_NODE_INFO_AND_DEPLOYMENT.md`.

