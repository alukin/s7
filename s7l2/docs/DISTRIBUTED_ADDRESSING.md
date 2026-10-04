# Distributed Addressing

Identifiers used to address nodes, running schemas, elements, and data streams in a distributed S7 system.

## Principles

- A **node** is one runtime instance (one `s7runtime` process configuration), not a physical host. Several nodes may run on one host with distinct endpoints, identities, and state directories.
- Elements on different nodes are never connected directly or through transparent proxies. Cross-node data flow uses explicit `NetSink` / `NetSource` elements (ADR-015, `S7_Net_Element_Contract.md`).
- Human-readable element ids are local to a schema; UUIDs make instances unique across nodes and runs.

## Identifier hierarchy

| Identifier | Scope and lifetime | Source |
|---|---|---|
| `node_uuid` | Logical node; permanent across restarts | Certificate SAN URI `spiffe://s7/node/<node_uuid>` (see `IDENTITY_AND_CREDENTIAL_PROVISIONING.md`); persisted in the node state directory |
| `instance_id` | One runtime process incarnation; new on every start | Random UUIDv4 |
| `runtime_instance_uuid` | Runtime that executes a compiled graph | Currently equal to `node_uuid` (`RuntimeService::runtime_instance_uuid()`) |
| `schema_instance_uuid` | One compiled instance of a schema | Random UUIDv4 per compilation |
| element instance UUID | One element inside one schema instance | UUIDv5 with `schema_instance_uuid` as namespace and the element id as name (`s7core/src/compiler.cpp`) |
| element id | Human-readable, unique within its schema | Schema JSON `id` |
| `stream_id` | One logical cross-node data stream | Assigned in the committed configuration (128-bit in the wire draft; the prototype uses a topic name) |

`NodeRuntime` resolves both directions: `element_instance_uuid_of(id)` and `element_id_of_instance_uuid(uuid)`; output accessors exist for both forms.

A fully qualified element address is `node_uuid / element instance UUID`; the human-readable form is `node name / element id`.

## Node state directory

Each node keeps persistent state (identity, peer cache, deployment records) in its own directory:

- `$S7_NODE_STATE_DIR` if set;
- otherwise `~/.s7runtime/default-node`.

Multiple nodes on one host must use different `S7_NODE_STATE_DIR` values. The target layout per node (`REMOTE_NODE_INFO_AND_DEPLOYMENT.md`) is `config/`, `certs/`, `deployments/`, `state/`, `logs/`.

## Cross-node streams

A schema assigned to one node publishes through `NetSink` and the receiving node's schema consumes through `NetSource`. In the current prototype both sides are matched by the `topic` parameter, which becomes the packet stream name. The wire-protocol draft replaces topic names with a configured 128-bit `stream_id` bound to an authenticated producer node (`S7_Network_Wire_Protocol_v1.md`).

Placement of elements on nodes is a deployment concern (placement manifest in `REMOTE_NODE_INFO_AND_DEPLOYMENT.md`); a schema is the unit of deployment (ADR-004).
