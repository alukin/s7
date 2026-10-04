# S7 Control Protocol — MVP Draft

**Status:** Proposed protocol draft; not yet a conformance standard.
**Version:** 0.2
**Transport:** WebSocket carrying UTF-8 JSON messages.

This document defines how the S7 control Studio and runtime nodes identify one another, discover peers, exchange network configuration, and prepare/commit a configuration update. It is intended to be implementable in C++, Rust, Python, and JVM runtimes without depending on a shared implementation library.

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** express protocol requirements and recommendations in this draft.

## 1. MVP Scope

The MVP supports:

- runtime node identity and capability discovery;
- peer discovery without requiring a central registry;
- retrieval of the committed network configuration from a runtime node;
- configuration deployment from one authorized control Studio;
- per-node prepare, commit, and abort operations;
- structured errors and versioned configuration identifiers.

The MVP has one control Studio authorized to change a computational network at a time. It does not define collaboration or master election between multiple Studio nodes, operator voting, distributed consensus, plugin distribution, or conflict merging. Runtime nodes do not vote on Studio authority.

Each runtime node MAY execute only its assigned graph fragment. The configuration bundle SHOULD describe the full network and its placement so a node can report or serve the committed network configuration. Runtime nodes do not need Git access: Studio selects a revision from its local Git repository and sends the required configuration bundle through the control protocol.

## 2. Transport and Authentication

Control messages use a WebSocket connection at the `/control` path. The connection MAY use `ws://` for local development and testing. A trusted network deployment MUST use `wss://` with mutual TLS (mTLS).

For trusted membership, the receiver MUST:

1. validate the peer certificate chain against configured trust roots;
2. validate certificate validity and applicable usage constraints;
3. extract the authenticated node identity from the certificate SAN URI;
4. verify that the identity matches the identity claimed in protocol messages;
5. authorize the requested operation for the certificate role.

Runtime-node identity uses the SAN URI form:

```text
spiffe://s7/node/<node_uuid>
```

The control Studio certificate MUST carry a CA-issued X.509 Certificate Policies entry for the S7 control-node role. Each network's trust configuration MUST identify the exact policy OID it accepts and the single authorized Studio certificate identity (certificate fingerprint or an equivalent pinned key identity). A second Studio certificate is not authorized until an operator updates that configuration. Certificate rotation requires an explicit trust-configuration update. A valid runtime-node certificate alone does not authorize configuration changes.

Plain WebSocket is not trusted membership and MUST NOT be used to authorize configuration changes.

## 3. Message Framing and Common Envelope

Each WebSocket text message contains exactly one JSON object encoded as UTF-8. A request and its response use the same connection. A client MUST NOT rely on JSON object member order.

Every request MUST contain:

```json
{
  "protocol": "S7CP",
  "protocol_version": 1,
  "type": "HELLO",
  "request_id": "<unique-request-id>"
}
```

Every response MUST include `protocol`, `protocol_version`, `type`, and the request's `request_id`. `request_id` is an opaque identifier used only to correlate a response with a request; UUID format is RECOMMENDED. It is not an authorization credential.

Receivers MUST reject unsupported protocol versions with `UNSUPPORTED_PROTOCOL_VERSION`. Receivers SHOULD ignore unknown object members. Unknown message types MUST return `ERROR` with `UNSUPPORTED_MESSAGE_TYPE`.

The initial request/response profile is synchronous: a connection has at most one outstanding request. A later revision may define pipelining or notifications.

## 4. Identity and Version Fields

- `node_uuid` identifies a logical runtime node and MUST remain stable across process restarts.
- `instance_id` identifies one runtime process incarnation and MUST change when that process restarts.
- A configuration revision identifies immutable configuration content; it MUST NOT be a moving branch name.
- `project_revision` MAY be the Git commit identifier held by Studio. Runtime nodes treat it as opaque metadata and do not fetch from Git.
- `configuration_digest` is the lowercase hexadecimal SHA-256 digest of the UTF-8 bytes produced by RFC 8785 JSON Canonicalization Scheme (JCS) over the complete configuration bundle, excluding the `configuration_digest` member itself. The canonicalized bundle MUST contain no duplicate JSON object member names. `project_revision` remains separate opaque Git metadata.

JSON identity fields are claims. The authenticated certificate identity is authoritative; a mismatch MUST fail the request.

## 5. MVP Message Set

### 5.1 `HELLO` → `HELLO_OK`

Checks reachability and protocol compatibility. The response includes `node_uuid`, `instance_id`, `node_name`, `runtime_version`, and current configuration revision when available.

### 5.2 `GET_NODE_INFO` → `NODE_INFO`

Returns the node descriptor: identity, platform, network endpoints, runtime state, supported protocol versions, available libraries/elements, and hardware capabilities. The capability structure is specified in [REMOTE_NODE_INFO_AND_DEPLOYMENT.md](REMOTE_NODE_INFO_AND_DEPLOYMENT.md); that document remains the detailed source for descriptor fields.

### 5.3 `GET_PEERS` → `PEERS`

Returns the node's known peer cache. Each entry SHOULD contain an endpoint, verified `node_uuid` when known, trust state, reachability, last-seen information, and certificate fingerprint when available. Peer records learned from announcements are claims until verified by connecting and validating the peer certificate.

### 5.4 `PEER_ANNOUNCE` → `PEER_ANNOUNCE_OK`

Shares the sender's endpoint and identity claims with a peer. The recipient MAY cache this information. An announcement MUST NOT by itself establish trust or authorize a peer.

### 5.5 `GET_NETWORK_CONFIGURATION` → `NETWORK_CONFIGURATION`

Returns the node's latest committed network configuration bundle, or a requested retained revision if available. The response MUST identify the immutable revision and digest. It MUST distinguish the full network configuration from the fragment assigned to this node.

If the node does not retain the requested revision, it returns `REVISION_NOT_FOUND`. This operation supports inspection and recovery from any runtime node that has the committed configuration; it does not make that node an authority for editing.

### 5.6 `PREPARE_DEPLOYMENT` → `PREPARE_DEPLOYMENT_RESULT`

The authorized Studio sends the proposed full network configuration metadata and the node-specific assigned fragment to each participating runtime node. The request MUST include:

- `deployment_id` and `revision`;
- `base_revision` (the currently active revision expected by the Studio);
- `project_revision` and `configuration_digest`;
- a configuration bundle included in the request;
- the recipient's assigned graph fragment and its required capabilities.

The node validates the bundle, fragment, local capabilities, and `base_revision`. The request MUST contain the configuration content; a runtime node MUST NOT need Git access to resolve a Git commit identifier. At minimum, the bundle distinguishes the full network configuration from this node's assigned fragment:

```json
{
  "configuration": {
    "project_revision": "<opaque-immutable-revision>",
    "configuration_digest": "<digest>",
    "full_network": {},
    "assigned_fragment": {}
  }
}
```

The two objects above are placeholders for the selected schema/configuration representation; this protocol does not standardize the schema format. A direct bundle MAY be sent in one request up to the receiver's advertised `max_configuration_bytes`. Larger bundles MUST use the chunked transfer messages below. The absolute MVP limit is 256 MiB per configuration bundle and 1 MiB per chunk. A receiver MUST reject a bundle exceeding either its advertised limit or the absolute limit before allocating its full declared size.

Chunk transfer uses `BEGIN_CONFIGURATION_TRANSFER`, `CONFIGURATION_CHUNK`, and `END_CONFIGURATION_TRANSFER` requests. Each chunk carries a transfer ID, zero-based chunk index, byte offset, and base64url-encoded bytes. Chunks MUST be contiguous and ordered; receivers reject overlaps, missing indices, inconsistent offsets, excess size, and digest mismatch. The receiver MUST stage the complete bundle and verify its canonical digest before it can become a prepared deployment. Repeated chunks with identical transfer ID/index/content are idempotent; conflicting repeats fail the transfer.

The node MUST stage the prepared material without activating it. A ready response contains a short-lived, node-local `prepare_token`; a negative response contains structured incompatibilities.

Repeating the same prepare request for the same deployment and revision SHOULD return the same live prepare token. A different request using a stale `base_revision` MUST return `REVISION_CONFLICT`.

### 5.7 `COMMIT_DEPLOYMENT` → `COMMIT_DEPLOYMENT_RESULT`

Commits a previously prepared deployment using its token. The operation MUST be idempotent. A node MUST record the committed revision and digest before reporting success. It activates its assigned fragment according to local runtime policy and reports the resulting state.

The MVP uses two-phase prepare/commit, but does not promise that separate nodes switch at the same physical instant. A connection failure during commit can leave a deployment partially committed. The Studio MUST query `LIST_DEPLOYMENTS`, retry idempotent commits on nodes still prepared, and report the per-node committed revision and digest. If the operator elects to revert a partially applied change, Studio MUST deploy the previous project revision as a new transaction; nodes MUST NOT autonomously vote or roll back. Revision matching on network data streams is specified by the data-stream protocol.

### 5.8 `ABORT_DEPLOYMENT` → `ABORT_DEPLOYMENT_RESULT`

Discards a prepared deployment and its reservations. Abort is idempotent. A committed deployment cannot be undone with abort; reverting configuration is a new deployment based on an earlier project revision.

### 5.9 `LIST_DEPLOYMENTS` → `DEPLOYMENTS`

Returns active, prepared, and retained deployment records, including revision, digest, state, and expiry information where applicable.

## 6. Configuration Transaction Rules

The normal deployment flow is:

1. Studio reads the current revision from participating nodes.
2. Studio prepares the same proposed configuration revision on every target node.
3. If any target is not ready, Studio aborts prepared participants and reports the incompatibilities.
4. If all targets are ready, Studio commits the deployment on every target.
5. Studio queries participants after commit and reports any incomplete or mixed state.

Nodes MUST reject prepare requests whose `base_revision` does not match their active revision, unless the request explicitly identifies a recovery operation. This provides optimistic conflict detection without runtime voting.

The protocol MUST retain enough transaction identifiers and state to make prepare, commit, abort, and recovery requests idempotent across reconnects and node restarts. Retention duration and crash-recovery journal format are implementation-specific in the MVP. `LIST_DEPLOYMENTS` MUST report enough state for Studio to distinguish committed, prepared, aborted, and unknown deployment IDs and to resume status reconciliation.

## 7. Error Responses

Errors use this shape:

```json
{
  "protocol": "S7CP",
  "protocol_version": 1,
  "type": "ERROR",
  "request_id": "<request-id>",
  "code": "INVALID_REQUEST",
  "message": "Human-readable summary",
  "details": {}
}
```

Initial error codes:

- `INVALID_REQUEST`
- `UNSUPPORTED_PROTOCOL_VERSION`
- `UNSUPPORTED_MESSAGE_TYPE`
- `UNAUTHORIZED`
- `CERTIFICATE_REQUIRED`
- `CERTIFICATE_INVALID`
- `IDENTITY_MISMATCH`
- `REVISION_NOT_FOUND`
- `REVISION_CONFLICT`
- `PREPARE_TOKEN_INVALID`
- `PREPARE_EXPIRED`
- `CAPABILITY_MISSING`
- `RESOURCE_LIMIT`
- `INTERNAL_ERROR`

Errors MUST NOT disclose private key material, credentials, or sensitive local paths.

## 8. Security and Operational Requirements

- Control mutation requests MUST be authorized using authenticated certificate identity and role, never JSON claims alone.
- Prepare tokens MUST be unpredictable, scoped to the node, deployment, and revision, and expire.
- Requests that mutate state MUST be idempotent or carry an idempotency key.
- Configuration bundles MUST be validated before activation.
- A failed or disconnected Studio does not stop a runtime node's last committed execution.
- Local Git history on Studio is not a runtime-node dependency. Runtime configuration retrieval and deployment use S7 control messages.

## 9. Open Items Before Interoperability Freeze

1. Exact JSON schemas for each message and chunk-transfer operation.
2. Configuration schema/diagram bundle representation and the mapping from Git project revision to exact executable configuration.
3. Retention and garbage-collection rules for configuration and deployment history.
4. Whether request IDs use UUIDv4 specifically and how long duplicate responses are cached.

## 10. Relationship to Implementation

The C++ runtime is the reference implementation. Existing code contains JSON control handlers for several messages and mTLS configuration, but this document also specifies behavior that is not yet implemented. Conformance must be established with cross-language protocol fixtures and tests before this draft is treated as a stable wire contract.

### 10.1 Implemented behavior (C++ runtime, 2026-10-04)

**Authorization.** `PREPARE_DEPLOYMENT`, `COMMIT_DEPLOYMENT` and `ABORT_DEPLOYMENT` are authorized from the TLS session only. The transport passes each control request together with the authenticated peer: whether the connection is TLS and, if the client certificate passed chain verification, its SHA-256 fingerprint, SAN URIs and Certificate Policies OIDs. A mutation is accepted only if all hold, in this order: TLS (`UNAUTHORIZED`), a verified client certificate (`CERTIFICATE_REQUIRED`), the node has an authorized Studio configured (`UNAUTHORIZED`), the certificate carries the configured control-node policy OID (`UNAUTHORIZED`), and its fingerprint equals the pinned Studio fingerprint (`UNAUTHORIZED`). JSON fields are never consulted. Each decision is logged as `control.authorized` / `control.denied` with the peer certificate fingerprint; denials also return it in `details.peer_certificate_sha256`. Read-only messages and `PEER_ANNOUNCE` need no authorization.

The authorized Studio is node configuration in the runtime schema file, not deployable:

```json
"control": {
  "studio_policy_oid": "2.25.4266950079767543379770189370854073200",
  "studio_certificate_sha256": "<64 hex digits>"
}
```

The OID above is a development placeholder (UUID-derived, under `2.25`) used by the test certificates until S7 policy OIDs are assigned under an enterprise arc.

**Prepare request.**

```json
{
  "type": "PREPARE_DEPLOYMENT", "request_id": "...", "protocol_version": 1,
  "deployment": {
    "deployment_id": "dep-1",
    "revision": 1,
    "base_revision": 0,
    "configuration": {
      "project_revision": "<opaque>",
      "configuration_digest": "sha256:<hex>",
      "full_network": {},
      "assigned_fragment": { "name": "...", "elements": [], "connections": [] }
    },
    "requires": { "libraries": [], "elements": [], "hardware": [] }
  }
}
```

- `revision` is an integer in [1, 2^53]; `base_revision` is required, in [0, 2^53], and 0 means "no active deployment". A mismatch with the active revision returns `REVISION_CONFLICT`.
- `configuration_digest` is `"sha256:"` plus the lower-case hex SHA-256 of the RFC 8785 (JCS) canonical JSON of the `configuration` object with its `configuration_digest` member removed. Integers outside ±2^53 cannot be canonicalized and are rejected. Requests are parsed with correctly rounded numbers so that digests agree across implementations. A mismatch returns `INVALID_REQUEST` with the expected digest in `details`.
- `assigned_fragment` uses the runtime schema-file format (`name`, `elements`, `connections`, optional `exposed_inputs`/`exposed_outputs`). Node settings (`websocket`, `simulation`, `execution`, `control`) are not allowed in it.
- `requires` is optional; malformed entries return `INVALID_REQUEST` instead of being skipped. Missing libraries, elements or hardware return `prepare_status: "NOT_READY"` with `incompatibilities`.
- The node then compiles the fragment without running it. A fragment that does not build or validate returns `NOT_READY` with an `INVALID_CONFIGURATION` incompatibility. Elements are constructed at this point, so an element with side effects in its constructor (for example a file sink opening its file) acts at prepare time.
- A `READY` response carries a 256-bit `prepare_token` from OpenSSL's CSPRNG, valid for 5 minutes. Repeating the same prepare returns the same token; the same deployment and revision with a different digest is `INVALID_REQUEST`.

**Commit.** `COMMIT_DEPLOYMENT {deployment_id, revision, prepare_token}` checks the token in constant time (`PREPARE_TOKEN_INVALID`), its expiry (`PREPARE_EXPIRED`) and that `base_revision` still matches the active revision (`REVISION_CONFLICT`, for example when another preparation on the same base was committed first). It then swaps the compiled graph into the running node at a step boundary; the new graph starts at step 0. If activation fails the response is `INTERNAL_ERROR` and the previous graph keeps running. On success the response reports `state: "active"`, the committed `configuration_digest`, and `execution` (`stepping` or `not_stepping`, whether the node is currently stepping its graph). Repeating a successful commit with the same token returns the same result.

**Abort.** Discards a prepared deployment (token required). Repeating an abort, or aborting a preparation that already expired, failed or was superseded, succeeds. Aborting the active deployment is `INVALID_REQUEST`.

**Status.** `HELLO.status`, `NODE_INFO.state.mode` and `DEPLOYMENTS.state` are `idle` or `active`; `NODE_INFO.state.execution` and `DEPLOYMENTS.execution` report `stepping` / `not_stepping`. `LIST_DEPLOYMENTS` returns `active_revision`, `active_deployment` (revision, project revision, digest, commit time), unexpired `prepared_deployments` (never their tokens) and a bounded `history` of outcomes (`committed`, `aborted`, `expired`, `failed`, `superseded`).

**Not yet implemented:** chunked configuration transfer and size limits, `GET_NETWORK_CONFIGURATION`, persistence of deployment state across node restarts (a restarted node runs its startup schema with active revision 0), and request-ID replay caching.
