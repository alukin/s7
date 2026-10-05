# S7 (s7l2)

S7 is a platform for constructing, executing, and observing distributed causal systems: a schema of elements and connections is validated, compiled into an executable graph, and stepped by a headless runtime. Studio is an optional GUI client.

The project is in pre-MVP development. Backward compatibility of APIs, file formats, and protocols is not maintained yet.

Documentation map: `docs/INDEX.md`. Known defects and planned fixes: `docs/CODE_REVIEW_REQUIRED_FIXES.md`.

## Repository layout

| Module | Content |
|---|---|
| `s7core/` | Element model, built-in elements, schema (incl. nested schemas with exposed ports), validation, compiler, `NodeRuntime`, element registry and metadata, schema-library loader |
| `s7io/` | File source/sink elements (compact binary, gnuplot text) |
| `s7transport/` | Transport abstraction: in-memory backend and Boost.Beast WebSocket/TLS backend, wire format v1 and lossless stream protocol |
| `s7net/` | `NetSink` / `NetSource` elements bound to the runtime transport endpoint |
| `s7observe/` | Read-only output sampling (`ObservationService`) |
| `s7runtime/` | Headless runtime executable: JSON schema loader, plugin loader, node identity, control-plane MVP |
| `s7noise/` | Example plugin: white and pink noise sources |
| `s7spectral/` | Reference third-party plugin (FFT), built out of tree against the installed SDK by `ctest -R s7spectral_sdk_smoke` |
| `s7studio/` | Qt6 Studio: diagram editor, element library, local embedded runtime with Scope windows, headless node discovery |
| `s7ctl/` | CLI: deploy a diagram to its nodes, read a node's deployed configuration and stream status, recover a stream |
| `s7tests/` | Reserved for whole-system tests; module smoke tests live in `<module>/tests/` |
| `external/` | Vendor code (QSchematic, QCustomPlot) — do not modify |

## Implemented

- Element model with typed ports, templated real/complex elements, converters, filters, oscillator, file I/O, network elements.
- Validation before execution: port indices and types, single driver per input, every input connected, cycles must contain causal memory.
- Compilation to an executable graph with instance UUIDs; elements with direct feedthrough (stateful or not) run in causal order; causal-memory elements (`Delay`, `PreviousValue`) publish their outputs one step later.
- Nested schemas as elements with exposed inputs/outputs; schema-backed element libraries (`.json`, `.s7library`, `.gz`).
- Element registry with metadata; C ABI plugins loaded at startup.
- Runtime JSON schema loading, optional WebSocket server/client with mTLS, persistent node identity, control-plane MVP (`HELLO`, `GET_NODE_INFO`, `GET_PEERS`, `PEER_ANNOUNCE`, deployment prepare/commit/abort/list).
- Studio diagram editing, element library, parameter dialogs, local runs with Scope plots, diagram/schema/library files, headless node discovery.

## Not implemented yet

- Executing a deployed configuration (commit currently only records state), multi-peer transport, the draft wire format.
- Control authorization bound to authenticated certificates; certificate-bound node identity.
- Snapshots/restore, dynamic probe subscriptions, remote observation streams.
- `NetRuntime`, distributed scheduling, coherence domains.
- Generic typed signals (all non-complex types are stored as `double`).

## Build

Install dependencies (Ubuntu): `tools/install_deps_ubuntu.sh` (`--no-studio` for headless only).

```bash
cmake -S . -B build                       # add -DS7_BUILD_STUDIO=OFF to skip Qt
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/s7runtime/s7runtime --help
./build/s7runtime/s7runtime --schema s7runtime/examples/schema_oscillator_gnuplot.json
./build/s7runtime/s7runtime --schema s7runtime/examples/schema_minimal.json --serve-seconds 10
./build/s7runtime/s7runtime --plugin-dir build/s7noise --library s7runtime/examples/schema_library_demo.json --schema <schema.json>
./build/s7studio/s7studio
```

Runtime schema examples are in `s7runtime/examples/`. A `simulation` block runs a fixed number of steps at startup (`periods * sample_rate_hz / frequency_hz`). While serving (`--serve-forever` / `--serve-seconds`), the runtime steps the graph continuously at `execution.step_rate_hz` (or `--step-rate <hz>`); with no rate set it serves the control plane only. SIGINT/SIGTERM stop stepping cleanly and print the step count and achieved rate. For two local nodes discovered by Studio see `docs/LOCAL_HEADLESS_NODES_DEMO.md`.

Runtime state (node identity, peer cache) is stored in the directory given by `--state-dir` (default `~/.s7runtime/default-node`, for single-node development); each node needs its own, and the runtime locks it while running. With a TLS certificate, the node UUID comes from its SAN URI `spiffe://s7/node/<node_uuid>`.
