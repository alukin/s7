# S7 Control Plane Specification (Draft v0)

**Status:** Active (Draft)  
**Purpose:** Control-plane architecture and command protocol direction  
**Last Updated:** May 13, 2026

---

This document defines the control-plane model for distributed S7 runtime nodes and one centralized GUI controller.

Status: draft, experimental/internal-first.

## 1. Scope and goals

The control plane is responsible for discovery, identity, capabilities, deployment, run lifecycle, and observability.

It is not the high-rate sample/frame transport path.

Goals:

- one authoritative GUI can connect to and orchestrate many runtime nodes,
- each computation node exposes a control-plane server endpoint,
- control is secure with X.509-based peer identity,
- capability and plugin catalogs are queryable and subscribable,
- graph deployment can be validated before start,
- failures prefer reduced-capability continuation when safe.

## 2. Baseline topology

### 2.1 Roles

- `s7-gui`:
  - authoritative session orchestrator,
  - issues deploy/start/stop commands,
  - maintains multi-node view.
- `s7-runtime` (node):
  - owns local execution resources,
  - serves control API,
  - advertises local capabilities and element catalog,
  - executes local portions of deployments.

### 2.2 Connectivity model

- GUI opens control connections directly to each target node.
- Node-to-node coordination may exist later, but GUI-to-node direct control is the baseline.
- Long-running computations may continue while GUI disconnects and reconnects.

## 3. Control/data separation

- Control plane: low/medium-rate commands and events.
- Data plane: high-rate stream/frame payloads.

Rule:

- Never route sustained sample/frame payloads through control messages.

## 4. Security and trust

### 4.1 Trust model

- X.509 certificates are required for control-plane identity.
- Local CA signing is acceptable for prototype and lab usage.
- Certificate identity maps to host/node identity.

### 4.2 Authentication baseline

- Mutual authentication is required for GUI <-> node sessions.
- Anonymous control connections are out of scope.

### 4.3 Authorization baseline

Node should authorize at least these actions:

- read-only capabilities/catalog,
- deploy/validate,
- run lifecycle control,
- plugin rescan/admin operations.

A simple role model is acceptable in first implementation (`viewer`, `operator`, `admin`).

## 5. Protocol shape

The transport implementation can evolve, but the message model should stay stable.

### 5.1 Interaction styles

- request/response:
  - point operations (`runtime.capabilities.get`, `graph.deploy.validate`),
- subscribe/stream:
  - change feeds (`runtime.catalog.subscribe`, `runtime.events.subscribe`),
- fire-and-forget:
  - non-critical notices where response is unnecessary.

### 5.2 Message envelope

Each control message should include at least:

- `protocol_version`,
- `message_id`,
- `correlation_id` (for responses/events tied to request),
- `source_id`,
- `target_id`,
- `timestamp`,
- `type`,
- `payload`.

Optional:

- `session_id`,
- `trace_id`,
- `auth_context`.

## 6. Versioning and compatibility

- Control protocol has explicit version (for example `s7-control/v0`).
- Nodes reject unsupported major protocol versions.
- Feature negotiation is done via capability flags in handshake.
- Experimental fields must be namespaced and optional.

## 7. Core API surface

The following conceptual operations define the minimum control-plane contract.

## 7.1 Session and identity

- `runtime.hello`
  - purpose: initial handshake and identity exchange,
  - returns node id, host id, runtime version, protocol version, features.
- `runtime.ping`
  - liveness check and measured round-trip timing.

## 7.2 Capabilities and catalogs

- `runtime.capabilities.get`
  - returns host capabilities, transports, execution domains, queue policies.
- `runtime.catalog.get`
  - full snapshot of available/unavailable element types.
- `runtime.catalog.subscribe`
  - stream of catalog changes.
- `runtime.catalog.changed` (event)
  - plugin loaded/unloaded, device capability change, incompatibility detected.

## 7.3 Plugin administration

- `runtime.plugin.rescan`
  - request plugin directory rescan.
- `runtime.plugin.load_errors`
  - retrieve detailed diagnostics.

## 7.4 Deployment and validation

- `graph.deploy.validate`
  - validates model + deployment artifacts against node capabilities.
- `graph.deploy.start`
  - activates deployment/run.
- `graph.deploy.stop`
  - stops deployment or run.
- `graph.deploy.status`
  - current state and reason fields.

## 7.5 Runtime observability

- `runtime.events.subscribe`
  - state transitions, warnings, recoveries.
- `runtime.host.resources`
  - CPU/memory/GPU and queue pressure snapshots.
- `runtime.failures.subscribe`
  - failure/degraded/transient events.

## 8. Data models

## 8.1 Runtime identity model

Minimum fields:

- `node_id` (stable node identifier),
- `host_id`,
- `runtime_version`,
- `build_id`,
- `certificate_subject` or mapped principal,
- `listen_endpoints`.

## 8.2 Capability model

Minimum fields:

- `execution_domains` (`scalar_step`, `sample_block`, `frame`),
- `transport_kinds` (`inproc`, `shm`, `remote`, `file`),
- `queue_policies` (`latest`, `realtime_overwrite`, `lossless_bounded`, `mailbox`),
- `plugin_api_version`,
- hardware flags (GPU/SDR/audio/video availability).

## 8.3 Element catalog model

Per element descriptor (summary view):

- `type_id` (lower-case dot-separated),
- `display_name`,
- `plugin_id`,
- `state` (`loaded`, `disabled`, `incompatible`, `failed`, `unsupported`),
- `execution_domains`,
- `transport_support`,
- hardware requirements,
- parameter schema reference/version.

## 8.4 Deployment validation report

Validation response should include:

- `ok` boolean,
- blocking errors,
- non-blocking warnings,
- unresolved dependencies,
- unavailable types/hosts,
- suggested remediations.

## 9. Failure and recovery semantics

Default policy is:

- prefer `degraded_warning` continuation where safe,
- use `transient_recoverable` retry/reconnect for temporary failures.

Examples:

- temporary worker disconnect -> auto-reconnect + degraded state event,
- plugin source unavailable -> affected element marked unavailable, unrelated processing continues,
- critical correctness breach -> escalate to hard stop with explicit reason.

Failure events should carry:

- severity,
- scope (`node`, `branch`, `run`),
- classification (`degraded_warning`, `transient_recoverable`, `fatal`),
- operator-facing message,
- machine-parseable code,
- recovery attempt counters.

## 10. Orchestration flow (centralized GUI)

A typical multi-node run sequence:

1. GUI connects to each node and performs `runtime.hello`.
2. GUI fetches capabilities/catalog snapshots.
3. GUI validates graph + deployment artifacts against selected nodes.
4. GUI issues `graph.deploy.start` to involved nodes.
5. Nodes emit lifecycle and health events.
6. GUI monitors and reacts to degraded/transient events.
7. GUI issues stop/update commands as needed.

## 11. Example message sketches

These are illustrative only and not final wire format.

### 11.1 Handshake response

```json
{
  "type": "runtime.hello.response",
  "protocol_version": "s7-control/v0",
  "message_id": "m-1002",
  "correlation_id": "m-1001",
  "payload": {
    "node_id": "node-lab-a",
    "host_id": "host-a",
    "runtime_version": "0.1.0-dev",
    "execution_domains": ["scalar_step", "sample_block", "frame"],
    "transport_kinds": ["inproc", "shm", "remote", "file"]
  }
}
```

### 11.2 Catalog change event

```json
{
  "type": "runtime.catalog.changed",
  "protocol_version": "s7-control/v0",
  "message_id": "m-2201",
  "payload": {
    "node_id": "node-lab-a",
    "changes": [
      {
        "type_id": "s7.dsp.fft.r2c",
        "old_state": "loaded",
        "new_state": "unsupported",
        "reason": "gpu_unavailable"
      }
    ]
  }
}
```

### 11.3 Deployment validation response

```json
{
  "type": "graph.deploy.validate.response",
  "protocol_version": "s7-control/v0",
  "message_id": "m-3302",
  "correlation_id": "m-3301",
  "payload": {
    "ok": false,
    "errors": [
      {
        "code": "type_unavailable",
        "message": "Element type s7.dsp.fft.r2c unavailable on node-lab-b"
      }
    ],
    "warnings": [
      {
        "code": "degraded_possible",
        "message": "Spectrum preview can continue in reduced mode"
      }
    ]
  }
}
```

## 12. Artifact boundaries

This control-plane contract assumes separate artifacts:

- graph/model artifact,
- deployment/placement artifact,
- GUI workspace artifact.

Control operations should reference these artifacts independently so long-running runtimes can continue without requiring the GUI process to stay alive.

## 13. Implementation order (recommended)

1. `runtime.hello` + capabilities snapshot,
2. catalog snapshot + subscribe,
3. deployment validate/start/stop,
4. failure/degraded event stream,
5. plugin rescan and diagnostics,
6. authorization roles and richer policy.

## 14. Open items

These are intentionally left open for the next iteration:

- exact wire encoding and schema tooling,
- detailed authorization policy model,
- certificate rotation/revocation workflow,
- node-to-node orchestration beyond centralized GUI,
- long-term backward compatibility guarantees for control messages.

