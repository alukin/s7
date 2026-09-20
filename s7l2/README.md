# S7 starter project

This repository now contains a runnable C++ starter aligned with the documents in `docs/`, organized as multiple subprojects in the same style as the previous `s7pp` repository structure.

## Documentation quick links

- `docs/INDEX.md` - technical reference entry point
- `docs/SCHEMA_LIBRARY_WORKFLOW.md` - schema-based library format and Studio/runtime workflow

## Project layout

The codebase is now split into focused subprojects:

- `s7core/` — core causal model, validation, compilation, and headless runtime
- `s7io/` — placeholder I/O-facing module for future external interfaces
- `s7net/` — placeholder network/deployment-facing module for explicit network boundaries
- `s7observe/` — observation-side APIs built on top of `s7core`
- `s7runtime/` — headless runtime service executable
- `s7ctl/` — command-line control/tooling starter
- `s7studio/` — Studio client starter, replacing the old `s7ui` name
- `s7tests/` — smoke tests

This mirrors the earlier modular layout approach without copying implementation code from the previous project.

## What is implemented

The current code focuses on the smallest useful subset of the architecture described in the S7 documents:

- `Element` as the smallest executable unit
- `Schema` as a structure-only graph definition
- `Connection` as the explicit representation of causality
- validation before execution
- compilation into an `ExecutableGraph`
- a headless runtime service entry point in `s7runtime`
- JSON schema loading/parsing from file (Boost.JSON)
- websocket server/client skeleton service paths (Boost.Beast/Asio)
- placeholder subprojects for I/O, networking, CLI tooling, and Studio
- a minimal observation layer in `s7observe`

## How it maps to the docs

### Formal model

The implementation mirrors the central ideas from `docs/S7_Formal_Model.md` and `docs/S7_Architecture_Overview.md`:

- computation is local
- state is local
- connections define causality
- schemas contain structure, not runtime state
- cycles are allowed only when they contain causal memory
- execution is headless and independent from any GUI

### Runtime architecture

The project includes minimal versions of the runtime pipeline described in `docs/S7_Runtime_Architecture.md`:

1. load schema definition JSON from file
2. parse/validate JSON structure
3. expose runtime service metadata
4. provide websocket server/client startup hooks for protocol work

### Observation architecture

The new `s7observe` subproject starts a separate observation-facing layer, consistent with `docs/S7_Observation_Architecture.md`:

- observation depends on runtime data but is separate from execution
- observation is read-only
- observation consumes outputs from the running system
- observation does not modify the functional graph

### Additional scaffolding

The newly added sibling subprojects are intentionally small, but they establish the long-term repository shape:

- `s7io` starts a dedicated place for external data ingress/egress abstractions
- `s7net` starts a dedicated place for node/topology and explicit network boundary concepts
- `s7ctl` starts a command-line operational surface
- `s7studio` starts the future Studio client as a runtime client, not part of runtime execution

### Runtime service focus

`s7runtime` no longer uses programmatic schema construction examples.

Instead it now:

- loads JSON schema definitions from a file;
- parses the schema with Boost.JSON;
- provides a Boost-based websocket server/client skeleton (`ws://`) for later protocol definition;
- supports a safe default mode where networking is not started unless requested.

### Architectural decisions already reflected

- `step()`-style progression rather than global time
- explicit causal graph
- validation before execution
- headless-first operation
- cycles require causal memory

## Runtime JSON example

Example schema files:

- `s7runtime/examples/schema_minimal.json`
- `s7runtime/examples/schema_oscillator_gnuplot.json`

Current shape includes:

- schema name
- elements array
- connections array
- nested schemas in `Schema` elements
- websocket server config
- websocket client config

## Current limitations of this MVP

This is a project bootstrap, not the full S7 platform yet.

Not implemented yet:

- schema input/output ports
- generic typed signals
- dynamic probe attachment and subscriptions
- real deployment graph semantics
- `NetRuntime`
- explicit `NetSource` / `NetSink`
- metadata-driven element registration
- snapshots and restore
- real GUI implementation for `s7studio`
- websocket message protocol definition (planned)

## Suggested next milestones

1. add typed ports and signal metadata
2. support nested schemas and schema ports
3. introduce executable-graph instance identifiers
4. add observation probes and subscriptions
5. define deployment and explicit network boundary elements
6. extend from `NodeRuntime` toward `NetRuntime`

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/s7runtime/s7runtime
./build/s7ctl/s7ctl
./build/s7studio/s7studio
```
