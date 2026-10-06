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
| `stream_id` | One logical cross-node data stream | A UUID set as the `stream_id` parameter of the `NetSink` / `NetSource` pair; carried as 128 bits in RESUME, bound to a 32-bit connection-local alias in data packets |

`NodeRuntime` resolves both directions: `element_instance_uuid_of(id)` and `element_id_of_instance_uuid(uuid)`; output accessors exist for both forms.

A fully qualified element address is `node_uuid / element instance UUID`; the human-readable form is `node name / element id`.

## Node state directory

Each node keeps persistent state (`node_uuid.txt`, `peers.json`, the active deployment in `deployment.json`, producer sequence reservations in `streams/`) in its own directory, given with `s7runtime --state-dir <dir>`. Without the option the runtime uses `~/.s7runtime/default-node`, which is meant for single-node development only.

The runtime holds an exclusive lock (`runtime.lock`) on the directory while it runs, so two nodes cannot share one identity: a second runtime pointed at the same directory fails at startup.

With a TLS certificate, `node_uuid` is taken from the SAN URI `spiffe://s7/node/<node_uuid>`; startup fails if the certificate has no such URI, names more than one node, or disagrees with the `node_uuid.txt` already stored in the state directory. Without a certificate, a UUID is generated on first start and persisted. The target layout per node (`REMOTE_NODE_INFO_AND_DEPLOYMENT.md`) is `config/`, `certs/`, `deployments/`, `state/`, `logs/`.

## Cross-node streams

A schema assigned to one node publishes through `NetSink` and the receiving node's schema consumes through `NetSource`. Both sides are matched by their `stream_id` parameter (`S7_Net_Element_Contract.md`, `S7_Network_Wire_Protocol_v1.md`). A stream flows directly over the connection between the producing and the consuming node; relaying through other nodes is not in the MVP.

Placement of elements on nodes is part of the diagram: each element has a `node` attribute, and the diagram is the unit of deployment (wire draft section 12.1, item 6). Each node runs its part; Studio creates the streams for connections between nodes, so a diagram needs no explicit `NetSink` / `NetSource` for them.
