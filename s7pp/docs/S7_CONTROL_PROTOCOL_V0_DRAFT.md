# S7 Control Protocol v0 Draft

**Date:** 2026-05-10  
**Status:** Draft v0.2 (review decisions applied)

## Purpose

Define a minimal, implementation-ready control protocol for the computation node (`s7main`) before adding the control socket server transport.

Scope in this draft:
- request/response command channel
- event stream channel
- schema lifecycle (`load`, `validate`, `run`, `pause`, `resume`, `stop`, `reset`, `unload`)
- node-local schema file operations (`list`, `get`, `save`)
- runtime discovery and status

Out of scope for v0:
- authentication/TLS details (already planned via X.509 in architecture docs)
- full multi-tenant session management
- remote data-plane stream transport definitions

---

## Decisions Locked for v0

1. **Single active run policy:** one active run per node.
2. **Schema source:** support raw JSON payload and node filesystem references.
3. **Schema handle lifetime:** handles survive GUI/client disconnect.
4. **Post-run behavior:** schema remains loaded by default.
5. **Replay:** no replay buffer in v0; deterministic replay via `schema.reset` + `schema.run`.
6. **Stop semantics:** stop after current `tick()` completes (graceful boundary).
7. **Type catalog richness:** return full available elements suitable for GUI editing.
8. **Errors:** return full node error details.
9. **Authorization:** node-level access only; no fine-grained method roles in v0.
10. **Framing:** JSON-RPC-like envelopes approved for baseline.
11. **Schema filesystem root policy:** fixed configured root only (no absolute paths or allowlist).
12. **Schema reset semantics:** reset element state _and_ re-run parameter calculation hooks explicitly.
13. **Pause granularity:** pause at next tick boundary of the schema (graceful boundary, not immediate best-effort).

---

## 1) Envelope

All control messages use one envelope shape:

```json
{
  "jsonrpc": "2.0",
  "id": "c9a6b3d5-9f95-4c8f-8e8a-6c6a7f5f0f5b",
  "method": "schema.run",
  "params": {
    "schema_handle": "schema-001",
    "mode": { "ticks": 1000 }
  }
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": "c9a6b3d5-9f95-4c8f-8e8a-6c6a7f5f0f5b",
  "result": {
    "run_id": "run-123",
    "accepted": true
  }
}
```

Error:

```json
{
  "jsonrpc": "2.0",
  "id": "c9a6b3d5-9f95-4c8f-8e8a-6c6a7f5f0f5b",
  "error": {
    "code": -32010,
    "message": "Schema validation failed",
    "data": {
      "details": ["Algebraic loop detected"]
    }
  }
}
```

---

## 2) Core Methods (v0)

### `runtime.hello`

Client -> node capability handshake.

Request:

```json
{
  "method": "runtime.hello",
  "params": {
    "client_name": "s7-gui",
    "client_version": "0.1.0",
    "protocol_min": 0,
    "protocol_max": 0
  }
}
```

Result:

```json
{
  "protocol_version": 0,
  "node_id": "node-local-001",
  "node_name": "s7main-local",
  "runtime_version": "0.1.0",
  "capabilities": {
    "methods": [
      "runtime.hello",
      "runtime.types.list",
      "runtime.status",
      "schema.fs.list",
      "schema.fs.get",
      "schema.fs.save",
      "schema.load",
      "schema.validate",
      "schema.run",
      "schema.pause",
      "schema.resume",
      "schema.stop",
      "schema.reset",
      "schema.unload",
      "events.subscribe",
      "events.unsubscribe"
    ],
    "supports_event_stream": true,
    "supports_parallel_runs": false,
    "schema_handles_persist_on_disconnect": true,
    "authorization_model": "node-level"
  }
}
```

### `runtime.types.list`

Return current registered element types.

Result:

```json
{
  "types": [
    {
      "name": "Integrator",
      "display_name": "Integrator",
      "library": "s7stdlib",
      "domain": "control",
      "category": "Core"
    },
    {
      "name": "RealFileSink",
      "display_name": "Real File Sink",
      "library": "s7fileio",
      "domain": "io",
      "category": "I/O/File"
    },
    {
      "name": "ComplexMemSource",
      "display_name": "Complex Mem Source",
      "library": "s7memio",
      "domain": "io",
      "category": "I/O/SharedMemory"
    }
  ]
}
```

### `schema.fs.list`

List schema files available on node filesystem under the configured schema root (no absolute paths or allowlist; only relative paths within the fixed configured root).

Result:

```json
{
  "root": "/var/lib/s7/schemas",
  "schemas": [
    { "name": "oscillator.json", "size": 1321, "mtime_unix_ms": 1778409000000 }
  ]
}
```

### `schema.fs.get`

Request (path is relative to configured root):

```json
{
  "method": "schema.fs.get",
  "params": {
    "path": "oscillator.json"
  }
}
```

Result:

```json
{
  "path": "oscillator.json",
  "schema_json": "{ ... raw JSON string ... }"
}
```

### `schema.fs.save`

Request (path is relative to configured root):

```json
{
  "method": "schema.fs.save",
  "params": {
    "path": "oscillator.json",
    "schema_json": "{ ... raw JSON string ... }",
    "overwrite": true
  }
}
```

Result:

```json
{
  "path": "oscillator.json",
  "saved": true
}
```

### `runtime.status`

Result:

```json
{
  "uptime_ms": 124230,
  "active_schema_handles": ["schema-001"],
  "active_runs": ["run-123"],
  "health": {
    "state": "ok",
    "degraded": false,
    "warnings": []
  }
}
```

### `schema.load`

Loads schema text and prepares handle.

`schema.load` accepts either inline JSON or a node filesystem path.

Request:

```json
{
  "method": "schema.load",
  "params": {
    "source": {
      "kind": "inline",
      "schema_json": "{ ... raw JSON string ... }"
    },
    "schema_name": "osc-test"
  }
}
```

Alternative request (`kind=filesystem`):

```json
{
  "method": "schema.load",
  "params": {
    "source": {
      "kind": "filesystem",
      "path": "oscillator.json"
    }
  }
}
```

Result:

```json
{
  "schema_handle": "schema-001",
  "loaded": true,
  "summary": {
    "id": "classical_oscillator_2integrators",
    "elements": 4,
    "connections": 4,
    "dt": 0.001
  }
}
```

### `schema.validate`

Result:

```json
{
  "schema_handle": "schema-001",
  "valid": true,
  "diagnostics": []
}
```

### `schema.run`

Exactly one mode in `mode`: `ticks` or `seconds`.

If another run is active, return `-32012` (single active run policy).

Request:

```json
{
  "method": "schema.run",
  "params": {
    "schema_handle": "schema-001",
    "mode": { "ticks": 5000 }
  }
}
```

Result:

```json
{
  "run_id": "run-123",
  "accepted": true,
  "mode": { "ticks": 5000 }
}
```

### `schema.stop`

Request by `run_id` or by `schema_handle`.

Semantics: graceful stop at tick boundary (complete current `tick()` first).

Result:

```json
{
  "stopped": true,
  "run_id": "run-123"
}
```

### `schema.pause`

Pause active run at the next tick boundary.

Semantics: graceful pause that allows the current `tick()` to complete before entering paused state (not immediate best-effort pause).

Result:

```json
{
  "run_id": "run-123",
  "paused": true
}
```

### `schema.resume`

Resume a paused run.

Result:

```json
{
  "run_id": "run-123",
  "resumed": true
}
```

### `schema.reset`

Reset schema state to initial parameters/initial states without unloading handle.

Semantics: resets all element states to their initial values _and_ explicitly re-runs parameter calculation hooks (e.g., any initialization computations or setup callbacks defined in the schema).

Result:

```json
{
  "schema_handle": "schema-001",
  "reset": true
}
```

### `schema.unload`

Result:

```json
{
  "schema_handle": "schema-001",
  "unloaded": true
}
```

---

## 3) Event Stream

Subscription methods:
- `events.subscribe`
- `events.unsubscribe`

Subscription request:

```json
{
  "method": "events.subscribe",
  "params": {
    "topics": ["runtime", "schema", "run", "warning", "error"]
  }
}
```

Node-pushed event envelope:

```json
{
  "event": "run.state.changed",
  "ts_unix_ms": 1778409000123,
  "payload": {
    "run_id": "run-123",
    "state": "completed"
  }
}
```

Initial event list:
- `runtime.warning`
- `runtime.error`
- `schema.loaded`
- `schema.unloaded`
- `schema.reset`
- `run.started`
- `run.paused`
- `run.resumed`
- `run.completed`
- `run.stopped`
- `run.failed`

Delivery semantics for v0:
- at-most-once, live stream only
- no replay window in protocol transport
- deterministic replay expected via `schema.reset` + `schema.run`

---

## 4) Error Codes (initial)

- `-32000` generic runtime error
- `-32001` unknown method
- `-32002` invalid params
- `-32010` schema parse/validation error
- `-32011` schema handle not found
- `-32012` run already active for schema
- `-32013` run id not found
- `-32014` run is not paused
- `-32015` run is already paused
- `-32016` schema file operation failed
- `-32020` node degraded (operation partially accepted)

---

## 5) Runtime State Model (simplified)

Schema states:
- `loaded`
- `validated`
- `running`
- `paused`
- `stopped`
- `unloaded`

Run states:
- `starting`
- `running`
- `paused`
- `completed`
- `stopped`
- `failed`

---

## 6) Implementation Notes for Next Step

1. Implement in-process command dispatcher first (no socket yet).
2. Back it with a `NodeRuntimeService` object owning:
   - registry snapshot
   - schema handle table
   - node schema file repository adapter
   - run table
3. Enforce single-active-run guard in dispatcher.
4. Add transport adapter later (RSocket server) that maps wire messages to dispatcher calls.

---

## Status: v0 Decisions Complete

All narrow open points for v0 have been resolved and locked in the "Decisions Locked for v0" section above (items 11–13).

v0 is now ready for implementation with a complete, constraint-specified protocol definition.

