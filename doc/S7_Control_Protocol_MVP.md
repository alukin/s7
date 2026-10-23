# S7 Control Protocol — MVP Draft

**Status:** MVP contract, implemented by the C++ runtime (2026-10-06). The conformance script `s7l2/s7runtime/tests/fixtures/control_v1.json` (section 9) is its message schema.
**Version:** 0.3
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

The control Studio certificate MUST carry the S7 control-node key purpose in its X.509 v3 Extended Key Usage extension, next to `clientAuth`. (Decided 2026-10-05: the role is a permitted key use, so Extended Key Usage expresses it; Certificate Policies describes issuance policy and the subject only names the certificate.) Each network's trust configuration MUST identify the exact key-purpose OID it accepts and the single authorized Studio certificate identity (certificate fingerprint or an equivalent pinned key identity). A second Studio certificate is not authorized until an operator updates that configuration. Certificate rotation requires an explicit trust-configuration update. A valid runtime-node certificate alone does not authorize configuration changes.

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

The data plane and the node's clock are described by these top-level members (wire draft section 12.1, items 1 and 4):

```json
"data_protocol": { "packet_versions": [1], "extensions": ["0x0002", "0x8001"] },
"clock_sync": { "synchronized": true, "max_error_ns": 87468000, "estimated_error_ns": 10284000,
                "source": "kernel", "max_error_bound_ns": 250000000, "usable": true },
"clock_domains": [
  { "id": "model", "kind": "model", "continuous_across_restart": true },
  { "id": "utc", "kind": "utc", "continuous_across_restart": true, "synchronized": true }
]
```

`control_limits` (`max_configuration_bytes`, `max_chunk_bytes`, `max_transfer_bytes`) bound the configurations the node accepts (section 10.1, Configuration transfer). `packet_versions` are the data packet versions the node sends and accepts; `extensions` the header extensions it understands (`0x0002`: `SEND_TIME`, `0x8001`: `TIMESTAMPS`). `model` is the deployment's model time.

**Clock.** S7 nodes are expected to keep their UTC clocks synchronized by NTP. `clock_sync` is read from the kernel (`ntp_adjtime`) at each request: `synchronized` unless the kernel marks the clock unsynchronized, with its worst-case (`max_error_ns`) and estimated error. The clock is `usable` when it is synchronized and its worst-case error is within the node's bound (node setting `"clock": { "max_error_ms": 250 }`; `"assume_synchronized": true` skips the kernel check, for tests and hosts synchronized by other means, and reports `source: "assumed"`). A usable clock is the shared domain `utc` (nanoseconds since the Unix epoch, comparable between nodes within their error bounds); otherwise the node lists its own domain `node:<node_uuid>:utc`, which is not comparable with other nodes. The deployment client refuses a node whose clock is not usable before preparing anything. A running node re-reads the state every second; when the clock becomes unusable it prints `runtime.clock.usable=false` and records `CLOCK_UNSYNCHRONIZED` in the recent faults of every stream (the streams keep running).

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
    "diagram": {}
  }
}
```

`diagram` is the whole deployed diagram (`S7_Network_Wire_Protocol_v1.md`, section 12.1, item 6). Each node finds its part of it through the elements' `node` placement; that part is its assigned fragment. A direct bundle MAY be sent in one request up to the receiver's advertised `max_configuration_bytes`. Larger bundles MUST use the chunked transfer messages below. The absolute MVP limit is 256 MiB per configuration bundle and 1 MiB per chunk. A receiver MUST reject a bundle exceeding either its advertised limit or the absolute limit before allocating its full declared size.

Chunk transfer uses `BEGIN_CONFIGURATION_TRANSFER`, `CONFIGURATION_CHUNK`, and `END_CONFIGURATION_TRANSFER` requests. Each chunk carries a transfer ID, zero-based chunk index, byte offset, and base64url-encoded bytes. Chunks MUST be contiguous and ordered; receivers reject overlaps, missing indices, inconsistent offsets, excess size, and digest mismatch. The receiver MUST stage the complete bundle and verify its canonical digest before it can become a prepared deployment. Repeated chunks with identical transfer ID/index/content are idempotent; conflicting repeats fail the transfer.

The node MUST stage the prepared material without activating it. A ready response contains a short-lived, node-local `prepare_token`; a negative response contains structured incompatibilities.

Repeating the same prepare request for the same deployment and revision SHOULD return the same live prepare token. A different request using a stale `base_revision` MUST return `REVISION_CONFLICT`.

### 5.7 `COMMIT_DEPLOYMENT` → `COMMIT_DEPLOYMENT_RESULT`

Commits a previously prepared deployment using its token. The operation MUST be idempotent. A node MUST record the committed revision and digest before reporting success. It activates its assigned fragment according to local runtime policy and reports the resulting state.

The MVP uses two-phase prepare/commit. With synchronized clocks the nodes switch at the same instant: the commits carry one `activate_at_ns`, and every node runs its previous graph until then and the new one from then (section 10.1, Commit), within the clocks' error. A connection failure during commit can leave a deployment partially committed. The Studio MUST query `LIST_DEPLOYMENTS`, retry idempotent commits on nodes still prepared, and report the per-node committed revision and digest. If the operator elects to revert a partially applied change, Studio MUST deploy the previous project revision as a new transaction; nodes MUST NOT autonomously vote or roll back. Revision matching on network data streams is specified by the data-stream protocol.

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
- `CONFIGURATION_TOO_LARGE`
- `TRANSFER_NOT_FOUND`
- `TRANSFER_INVALID`
- `INTERNAL_ERROR`

Errors MUST NOT disclose private key material, credentials, or sensitive local paths.

## 8. Security and Operational Requirements

- Control mutation requests MUST be authorized using authenticated certificate identity and role, never JSON claims alone.
- Prepare tokens MUST be unpredictable, scoped to the node, deployment, and revision, and expire.
- Requests that mutate state MUST be idempotent or carry an idempotency key.
- Configuration bundles MUST be validated before activation.
- A failed or disconnected Studio does not stop a runtime node's last committed execution.
- Local Git history on Studio is not a runtime-node dependency. Runtime configuration retrieval and deployment use S7 control messages.

## 9. Formerly Open Items (resolved 2026-10-06)

1. **Message schemas.** `s7l2/s7runtime/tests/fixtures/control_v1.json` (written by `s7l2/tools/gen_control_fixtures.py`) is the normative schema of every message: a scripted conversation with one node that gives each request and, for its response, the type, every required member with its JSON type, and exact values where they are determined, including the errors and idempotent repeats. Responses may carry further members. The configuration digests in it are computed from RFC 8785 by the generator, independently of the C++ code, so the script also checks an implementation's canonicalization. Section 10.1 describes each message's behavior.
2. **Bundle representation.** A configuration bundle is `{project_revision, configuration_digest, diagram}`. `diagram` is the S7 diagram document (`document_kind: "s7.diagram"`): `schema` (elements with their `node` placement, connections), `runtime_nodes`, `streams` (stream descriptors, wire protocol section 12.1 item 1), `dt` (the model-time step, optional) and Studio's `diagram` layout. `project_revision` is opaque: a Git revision when the diagram is in Git, otherwise `diagram:<first 16 hex digits of the content digest>`; nodes never resolve it. The executable configuration of a node is its part of the diagram (`extract_node_schema`), which depends on the bundle content only.
3. **Retention.** A node keeps its active configuration only (persisted, `GET_NETWORK_CONFIGURATION` serves no other revision), prepared deployments until they are committed, aborted or expire (5 minutes), configuration transfers until they expire (5 minutes, at most 4), and the latest 64 deployment outcomes in memory. After a restart the history starts with the restored deployment's `restored` (or `restore_failed`) entry.
4. **Request IDs and duplicates.** `request_id` is required: a string of 1 to 128 characters, chosen by the client (a UUIDv4 is recommended, not required) and echoed in the response; a request without one is `INVALID_REQUEST` with an empty `request_id`. Responses are not cached by request ID, because every mutation is idempotent by its own keys: `PREPARE_DEPLOYMENT` by deployment, revision and digest (the same token again), `COMMIT_DEPLOYMENT` by its token (the same result), `ABORT_DEPLOYMENT` always, configuration transfers by transfer ID, chunk index and content, and `RECOVER_STREAM` (a stream without a fault is left alone). Prepare tokens do not survive a restart: a commit repeated after one is `PREPARE_TOKEN_INVALID`, and the client reconciles with `LIST_DEPLOYMENTS`, whose active revision and digest show whether its commit took effect.

## 10. Relationship to Implementation

The C++ runtime is the reference implementation; `s7runtime_control_fixtures_smoke` runs the conformance script of section 9 against its control plane with an accepting deployment host, without compiling graphs or performing TLS handshakes. Other implementations are checked with the same script. The Python node `s7py` passes it over mTLS except the data-stream steps, since it has no data plane yet (`s7py/tools/check_control_fixtures.py --skip-streams`).

A live C++ node defaults to requiring a client certificate during the TLS handshake. The script also uses an anonymous peer, so a live conformance-test node must explicitly set `websocket.server.tls.require_client_cert` to `false`; certificates supplied by clients are still verified, and mutation authorization still requires the pinned control certificate. Anonymous TLS mutations return `CERTIFICATE_REQUIRED`, while mutations without TLS return `UNAUTHORIZED`. The Python runner accounts for this difference. See [the fixture setup](../s7l2/s7runtime/tests/fixtures/README.md) for the configuration and remaining node prerequisites.

### 10.1 Implemented behavior (C++ runtime, 2026-10-06)

**Authorization.** `PREPARE_DEPLOYMENT`, `COMMIT_DEPLOYMENT`, `ABORT_DEPLOYMENT`, `RECOVER_STREAM` and the configuration transfer messages are authorized from the TLS session only. The transport passes each control request together with the authenticated peer: whether the connection is TLS and, if the client certificate passed chain verification, its SHA-256 fingerprint, SAN URIs and Extended Key Usage OIDs. A mutation is accepted only if all hold, in this order: TLS (`UNAUTHORIZED`), a verified client certificate (`CERTIFICATE_REQUIRED`), the node has an authorized Studio configured (`UNAUTHORIZED`), the certificate's Extended Key Usage contains the configured control-node key purpose (`UNAUTHORIZED`), and its fingerprint equals the pinned Studio fingerprint (`UNAUTHORIZED`). JSON fields are never consulted. Each decision is logged as `control.authorized` / `control.denied` with the peer certificate fingerprint; denials also return it in `details.peer_certificate_sha256`. Read-only messages and `PEER_ANNOUNCE` need no authorization.

The authorized Studio is node configuration in the runtime schema file, not deployable:

```json
"control": {
  "studio_key_purpose_oid": "2.25.4266950079767543379770189370854073200",
  "studio_certificate_sha256": "<64 hex digits>"
}
```

The OID above is a development placeholder (UUID-derived, under `2.25`) used by the test certificates until S7 OIDs are assigned under an enterprise arc.

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
      "diagram": { "runtime_nodes": [], "schema": { "name": "...", "elements": [], "connections": [] }, "streams": [] }
    },
    "requires": { "libraries": [], "elements": [], "hardware": [] }
  }
}
```

- `revision` is an integer in [1, 2^53]; `base_revision` is required, in [0, 2^53], and 0 means "no active deployment". A mismatch with the active revision returns `REVISION_CONFLICT`.
- `configuration_digest` is `"sha256:"` plus the lower-case hex SHA-256 of the RFC 8785 (JCS) canonical JSON of the `configuration` object with its `configuration_digest` member removed. Integers outside ±2^53 cannot be canonicalized and are rejected. Requests are parsed with correctly rounded numbers so that digests agree across implementations. A mismatch returns `INVALID_REQUEST` with the expected digest in `details`.
- `diagram` is a Studio diagram document; its `schema` uses the runtime schema-file format with a `node` (node_uuid) on every element. The node runs the elements placed on its own `node_uuid` plus a `NetSink` / `NetSource` per stream end; node settings in the schema (`websocket`, `simulation`, `execution`, `control`) are ignored. A diagram with nothing placed on the node, or with a connection between nodes that has no stream, returns `NOT_READY` (`INVALID_CONFIGURATION`). On commit the node connects to the producers of the streams it consumes.
- `requires` is optional; malformed entries return `INVALID_REQUEST` instead of being skipped. Missing libraries, elements or hardware return `prepare_status: "NOT_READY"` with `incompatibilities`.
- The node then compiles the fragment without running it. A fragment that does not build or validate returns `NOT_READY` with an `INVALID_CONFIGURATION` incompatibility. Elements are constructed at this point, so an element with side effects in its constructor (for example a file sink opening its file) acts at prepare time.
- A `READY` response carries a 256-bit `prepare_token` from OpenSSL's CSPRNG, valid for 5 minutes. Repeating the same prepare returns the same token; the same deployment and revision with a different digest is `INVALID_REQUEST`.

**Commit.** `COMMIT_DEPLOYMENT {deployment_id, revision, prepare_token, activate_at_ns?}` checks the token in constant time (`PREPARE_TOKEN_INVALID`), its expiry (`PREPARE_EXPIRED`) and that `base_revision` still matches the active revision (`REVISION_CONFLICT`, for example when another preparation on the same base was committed first). It then swaps the compiled graph into the running node at a step boundary; the new graph starts at step 0. With `activate_at_ns` (UTC nanoseconds, at most 60 s ahead, otherwise `INVALID_REQUEST`) the commit takes effect at once (the revision is committed and persisted) but the previous graph keeps running until that moment, when the serving loop swaps in the new one; its real-time grid starts there. A commit that arrives after `activate_at_ns` activates at once and catches up on the grid, so its steps still fall on the other nodes' grid. If activation fails the response is `INTERNAL_ERROR` and the previous graph keeps running. On success the response reports `state` (`active`, or `scheduled` until `activate_at_ns`), `activate_at_ns` (or `null`), the committed `configuration_digest`, and `execution` (`stepping` or `not_stepping`, whether the node is currently stepping its graph). `LIST_DEPLOYMENTS.active_deployment` reports `activate_at_ns` and whether it is `activated` yet. A restarted node restores its deployment at the next slot of the original grid (`activate_at_ns + k · dt`), without catching up the steps it missed. Repeating a successful commit with the same token returns the same result.

**Abort.** Discards a prepared deployment (token required). Repeating an abort, or aborting a preparation that already expired, failed or was superseded, succeeds. Aborting the active deployment is `INVALID_REQUEST`.

**Status.** `HELLO.status`, `NODE_INFO.state.mode` and `DEPLOYMENTS.state` are `idle` or `active`; `NODE_INFO.state.execution` and `DEPLOYMENTS.execution` report `stepping` / `not_stepping`. `LIST_DEPLOYMENTS` returns `active_revision`, `active_deployment` (revision, project revision, digest, commit time, and `restored` when it was restored at startup), unexpired `prepared_deployments` (never their tokens) and a bounded `history` of outcomes (`committed`, `aborted`, `expired`, `failed`, `superseded`, `restored`, `restore_failed` with a `message`).

**Restart.** A commit writes the active deployment (its id, revision, commit time and the configuration as received) to `deployment.json` in the node's state directory, atomically. At startup the node restores it in place of its startup schema: it verifies the configuration digest, compiles its part and activates it at the same revision, so the next deployment uses it as `base_revision`. A record that does not verify or cannot be run is logged (`deployment.restore_failed`), reported in the history as `restore_failed`, and the node starts idle with its startup schema (active revision 0). Prepared but uncommitted deployments and prepare tokens are not kept: repeating a commit after a restart is `PREPARE_TOKEN_INVALID`, and the client reconciles with `LIST_DEPLOYMENTS`. A restarted producer continues its streams beyond its sequence reservation, so their consumers fault with `SEQUENCE_GAP` until recovered (`RECOVER_STREAM`).

**Stream status.** `GET_STREAM_STATUS` (read-only) returns `STREAM_STATUS` with one entry per data stream of the node:

```json
{
  "type": "STREAM_STATUS", "request_id": "...", "protocol_version": 1,
  "streams": [
    {
      "stream_id": "3f2b8c1d-6e4a-4b7f-9a2c-1d5e8f7a6b3c", "role": "producer", "data_type": "F64",
      "state": "active", "fault": "NONE", "next_sequence": 120,
      "packets": 120, "elements": 120, "replayed": 10, "lost_resumes": 0,
      "duplicates_discarded": 0, "malformed": 0, "unauthorized": 0,
      "send_log_depth": 4, "buffered_elements": 0, "blocked_ms_total": 0,
      "recent_faults": [ { "code": "UNDERRUN", "detail": "", "at": "2026-10-06T09:12:03.418Z" } ],
      "latency": { "samples": 120, "min_ns": 410000, "mean_ns": 5300000, "p50_ns": 4096000,
                   "p99_ns": 16384000, "max_ns": 15100000, "last_ns": 5020000, "late": 0 }
    }
  ]
}
```

`state` is `active`, `blocked` (a producer waiting on a full send log), `faulted`, or `closed` (no element of the running graph uses the stream; its state is kept for a redeployment); `fault` is `NONE` or a fault code of `S7_Net_Element_Contract.md`. `next_sequence` is the next sequence a producer assigns, or the next a consumer expects. `unauthorized` counts refused peers (a producer: `RESUME`s from nodes that are not consumers of the stream; a consumer: packets from a connection that is not to its producer node). `recent_faults` lists the latest 16 faults and refusals, oldest first, with what was refused in `detail` (for example `node <uuid>`); it is kept across `RECOVER_STREAM`, so a recovered fault stays reported. `latency` is the end-to-end latency of the packets a consumer stream received (wire protocol section 7.2); `late` counts those over the stream's `max_latency_ms`.

**Stream recovery.** `RECOVER_STREAM {stream_id}` is authorized like a deployment mutation. It clears the stream's fault (a stream without one is left as it is, so a repeated request changes nothing) and returns `RECOVER_STREAM_RESULT {stream_id, recovered}` (`recovered: false` if the node has no such stream; `INVALID_REQUEST` for a malformed id). A recovered consumer accepts the next packet it receives as its new start; the data lost in the fault stays lost and stays counted. A runtime whose stepping stopped because a step failed (for example on a stream fault) resumes stepping after a successful recovery.

**Network configuration.** `GET_NETWORK_CONFIGURATION {revision?}` (read-only) returns `NETWORK_CONFIGURATION` with `node_uuid`, `deployment_id`, `revision`, `project_revision`, `configuration_digest`, `committed_at`, the deployed `diagram` and this node's part of it as `node_schema`. Only the active revision is retained: no committed configuration, or another `revision`, is `REVISION_NOT_FOUND`.

**Model time and pacing.** A deployed graph steps in the diagram's model time: with a `dt` (seconds) in the diagram each step advances model time by `dt` (in whole nanoseconds) and observations carry it; without one the graph's steps are untimed.

With a `dt` the graph also runs in real time on a UTC grid: step `n` is due at `origin + n · dt`, where `origin` is the moment the graph was activated. Model time thus advances with wall time, and nodes with synchronized clocks keep the same pace instead of drifting apart. A node that falls behind runs the missed steps at once (every step runs, so streams keep one element per step) and reports its lag (`runtime.execution.behind lag_ms=...` on stderr when it exceeds a step and 50 ms). A step that fails stops stepping until `RECOVER_STREAM` or a new deployment; the grid then continues at its next slot, skipping the slots missed while stopped (`runtime.execution.grid_rebased`). A graph without `dt`, and the node's startup schema, step at the node's `execution.step_rate_hz` as before. `NODE_INFO.state.timing` and `DEPLOYMENTS.timing` report the pacing: `{pacing: "grid", period_ns, origin_utc_ns, steps, lag_ns, max_lag_ns, rebases}`, or `{pacing: "rate" | "none", step_rate_hz, steps}`.

**Deployment client.** `s7::deployment::deploy_diagram` (`s7l2/s7net/include/s7/deployment_client.hpp`), used by Studio (Run > Deploy to Nodes) and `s7ctl deploy`, assigns the streams, checks each node's identity with `HELLO`, reads each node's clock and `data_protocol` with `GET_NODE_INFO` (refusing a node whose clock is not usable) and refuses a deployment in which a stream's producer and a consumer share no packet version, or a stream with per-packet timestamps has an end without the `TIMESTAMPS` extension (nothing is prepared then), uses one revision (the highest active revision + 1) with each node's own `base_revision`, prepares every node and commits only if all are `READY`; otherwise it aborts the prepared ones. The commits carry one `activate_at_ns`: 250 ms plus four times the nodes' summed `GET_NODE_INFO` round trips after the commits begin (at most 10 s; `DeployOptions::activation_delay` overrides it, zero activates each node as its commit arrives). A node committed after that moment is reported as catching up. `s7ctl deploy` prints the activation time. A commit that fails after others succeeded is reported per node.

**Configuration transfer.** `NODE_INFO.control_limits` advertises `max_configuration_bytes` (default 1 MiB), `max_chunk_bytes` (at most 1 MiB) and `max_transfer_bytes` (default 64 MiB, at most 256 MiB). A `PREPARE_DEPLOYMENT` whose serialized `configuration` exceeds `max_configuration_bytes` is `CONFIGURATION_TOO_LARGE`; such a configuration is sent first by chunked transfer, and the prepare names it with `deployment.configuration_transfer_id` instead of `configuration`. The transfer messages are authorized like a deployment mutation:

- `BEGIN_CONFIGURATION_TRANSFER {transfer_id, total_bytes, configuration_digest}` → `CONFIGURATION_TRANSFER_STARTED {transfer_id, max_chunk_bytes}`. `total_bytes` is the size of the serialized configuration (including its `configuration_digest` member). Over `max_transfer_bytes` it is `CONFIGURATION_TOO_LARGE`, refused before anything is allocated; at most 4 transfers exist at a time (`RESOURCE_LIMIT`). Repeating a `BEGIN` with the same parameters is accepted; with others it is `TRANSFER_INVALID`.
- `CONFIGURATION_CHUNK {transfer_id, index, offset, data}` → `CONFIGURATION_CHUNK_ACCEPTED {transfer_id, index, received_bytes}`. `data` is unpadded base64url of 1 to `max_chunk_bytes` bytes. The next chunk must have the next index and start where the previous one ended, without exceeding `total_bytes`; otherwise `TRANSFER_INVALID` (the transfer stays). A chunk repeated with the same content is accepted; with other content it fails the transfer.
- `END_CONFIGURATION_TRANSFER {transfer_id}` → `CONFIGURATION_TRANSFER_COMPLETE {transfer_id, configuration_digest, total_bytes}` once all bytes arrived (`TRANSFER_INVALID` before) and the bytes are a configuration whose canonical digest is the announced one (otherwise the transfer fails, `TRANSFER_INVALID`).

A transfer, complete or not, expires with the prepare TTL (5 minutes); an unknown or failed transfer is `TRANSFER_NOT_FOUND`. Control connections accept WebSocket messages of up to 4 MiB (`transport::kMaxControlMessageBytes`); data connections one data packet. The deployment client reads each node's limits from `NODE_INFO` and uses the transfer when needed (`transfer-<digest prefix>-r<revision>` as `transfer_id`).

Request IDs and retention follow section 9 (items 3 and 4).
