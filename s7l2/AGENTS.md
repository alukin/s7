# AGENTS.md

## Mission-critical orientation
- S7 is a **causal graph runtime**: schema -> validate -> compile -> execute -> observe (`README.md`, `docs/S7_Runtime_Architecture.md`).
- Prioritize **causality over clock time** and keep network boundaries explicit (`docs/S7_Architecture_Overview.md`, `docs/S7_Architectural_Decisions.md`).
- Keep runtime logic headless-first; GUI (`s7studio`) is optional and should not leak into core/runtime paths.

## Component map (where to change what)
- `s7core/`: schema model, validation, compiler, runtime graph execution (`include/s7/*.hpp`, `src/{validation,compiler,runtime}.cpp`).
- `s7runtime/`: CLI/service wiring, JSON schema loading, execution loop (`s7runtime/src/main.cpp`).
- `s7observe/`: read-only probing/sampling API around runtime (`s7observe/include/s7/observe.hpp`).
- `s7noise/`: plugin-loaded noise elements (e.g., white/pink noise generators).
- `s7spectral/`: experimental spectral-analysis plugin; excluded from the build until block/frame signal types exist (do not wire it into CMake as-is).
- `s7io/`: file source/sink elements (compact binary, gnuplot text); future hardware I/O.
- `s7net/`: network boundary elements and websocket/server-client scaffolding.
- `s7ctl/`: CLI control/tooling module (currently minimal smoke-test scaffold).
- `s7studio/`: Qt UI client (guarded by `S7_BUILD_STUDIO`).
- `s7transport/`: transport backends (in-memory, Beast WebSocket/TLS) and prototype packet codec.
- `s7tests/`: reserved for whole-system tests; module smoke tests live in `<module>/tests/`.
- Docs map: `docs/INDEX.md`; known defects and planned fixes: `docs/CODE_REVIEW_REQUIRED_FIXES.md`.

## Build/test workflow (verified from repo docs/config)
- Configure + build:
```bash
cmake -S . -B build
cmake --build build
```
- Headless configure when Qt6 is unavailable:
```bash
cmake -S . -B build -DS7_BUILD_STUDIO=OFF
```
- Run tests:
```bash
ctest --test-dir build --output-on-failure
```
- Run specific smoke tests:
```bash
ctest --test-dir build -R s7core_smoke
ctest --test-dir build -R s7runtime_control_smoke
ctest --test-dir build -R s7io_smoke
```
- Run runtime service (steps the graph at the schema's `execution.step_rate_hz` while serving; use a distinct `--state-dir` per node):
```bash
./build/s7runtime/s7runtime --schema s7runtime/examples/schema_minimal.json --state-dir /tmp/s7-node-a --serve-forever
```
- Optional net mode:
```bash
./build/s7runtime/s7runtime --start-net
```
- Show CLI options:
```bash
./build/s7runtime/s7runtime --help
```

## Project-specific coding patterns
- Build schemas via element factories, e.g. `schema.add_element("id", [] { return std::make_unique<MyElement>(); });`, and prefer runtime `ElementRegistry` registration for built-ins/plugins (`s7runtime/src/main.cpp`, `s7core/tests/core_smoke.cpp`).
- New elements follow the port-centric pattern: own output storage in the element and register ports in the constructor via `register_input/register_output` (`s7core/include/s7/elements.hpp`).
- Ports are introspected via `Port{index,name,type,value_ptr}`; preserve `SignalType` correctness and typed access (`s7core/include/s7/element.hpp`).
- Port-type mismatches are hard validation errors in current runtime wiring (no implicit conversion path enabled by default) (`s7core/src/validation.cpp`, `s7core/tests/core_smoke.cpp`).
- Respect execution ordering: `NodeRuntime::step()` publishes the outputs staged by causal-memory elements in the previous step, runs `causal_order` (every element with direct feedthrough, stateful or not), then steps `memory_order` (`Feedthrough::None` elements such as `Delay`) and stages their outputs for the next step (`s7core/src/runtime.cpp`).
- Validation expects cycles to include at least one causal-memory element; do not bypass this in compiler/runtime paths.
- Observation is non-invasive: use `ObservationService` reads, never mutate runtime state from observe code.
- Observation sampling pattern: `sample_output(element_id, port)` and `snapshot({ProbePoint{...}})` (one coherent step); observe only on the thread that steps the runtime, otherwise `std::logic_error` (`s7observe/include/s7/observe.hpp`, `s7observe/tests/observe_smoke.cpp`).
- Plugin elements are loaded at runtime via `PluginLoader`; keep element registration idempotent and scoped to plugin registration hooks (`s7runtime/include/s7/plugin_loader.hpp`, `s7runtime/src/plugin_loader.cpp`).

## Integration and dependency notes
- C++20 project; top-level dependencies include Boost (JSON/Asio/Beast) and optional Qt6 (`CMakeLists.txt`).
- Optional plugin dependencies include FFTW3 for `s7spectral` and standard RNG-based noise generation in `s7noise`.
- JSON schemas under `s7runtime/examples/` are canonical runtime inputs.
- Runtime default schema path is compiled via `S7RUNTIME_DEFAULT_SCHEMA_PATH` in `s7runtime/CMakeLists.txt`.
- Runtime/plugin integration point: plugin `.so` discovery uses `--plugin-dir` directories, `~/.s7/plugins`, `/opt/s7/plugins`, `./plugins`, and `$S7_PLUGIN_PATH`; plugin targets install under `lib/s7/plugins`.
- External UI libs live in `external/` (e.g., `QCustomPlot`, `QSchematic`); avoid coupling core logic to them.
- Treat code under `external/` as vendor code: default to **no edits**.
- If behavior/style changes are needed, implement wrappers/subclasses/adapters in project modules (typically `s7studio/`) instead of patching `external/`.
- Modify `external/` only when the user explicitly requests a vendor patch; document rationale and keep such patches minimal and isolated.

## Guardrails for AI edits
- Keep changes module-local unless cross-module contracts require expansion.
- If touching compiler/validator/runtime, add or update a smoke test in the module's `tests/` directory (e.g. `s7core/tests/core_smoke.cpp`).
- The project is pre-MVP with no deployments: breaking changes to APIs, formats, and protocols are allowed; do not add compatibility shims.
- Prefer explicit errors around schema/type mismatches over silent coercion.
- Tests must not write under `$HOME`: give `RuntimeService` a temporary state directory.
- Control mutations are authorized only from the transport's `ControlPeer` (verified certificate, control-node key purpose in Extended Key Usage, pinned fingerprint); never add authorization based on JSON fields. Implemented control behavior is summarized in `docs/S7_Control_Protocol_MVP.md` section 10.1.
- Preserve public namespace and API surface conventions (`namespace s7`, public headers under `s7core/include/s7/`).
- Before any edit in `external/`, stop and confirm explicit user approval; otherwise, do not change vendored sources.

