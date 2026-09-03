# AGENTS.md

## Mission-critical orientation
- S7 is a **causal graph runtime**: schema -> validate -> compile -> execute -> observe (`README.md`, `docs/S7_Runtime_Architecture.md`).
- Prioritize **causality over clock time** and keep network boundaries explicit (`docs/S7_Architecture_Overview.md`, `docs/S7_Architectural_Decisions.md`).
- Keep runtime logic headless-first; GUI (`s7studio`) is optional and should not leak into core/runtime paths.

## Component map (where to change what)
- `s7core/`: schema model, validation, compiler, runtime graph execution (`include/s7/*.hpp`, `src/{validation,compiler,runtime}.cpp`).
- `s7runtime/`: CLI/service wiring, JSON schema loading, execution loop (`s7runtime/src/main.cpp`).
- `s7observe/`: read-only probing/sampling API around runtime (`s7observe/include/s7/observe.hpp`).
- `s7io/`: hardware-facing abstractions and converters.
- `s7net/`: network boundary elements and websocket/server-client scaffolding.
- `s7studio/`: Qt UI client (guarded by `S7_BUILD_STUDIO`).
- `s7tests/`: smoke/integration checks (validation + I/O focused).

## Build/test workflow (verified from repo docs/config)
- Configure + build:
```bash
cmake -S . -B build
cmake --build build
```
- Run tests:
```bash
ctest --test-dir build --output-on-failure
```
- Run runtime service:
```bash
./build/s7runtime/s7runtime --schema s7runtime/examples/schema_minimal.json
```
- Optional net mode:
```bash
./build/s7runtime/s7runtime --start-net
```

## Project-specific coding patterns
- Build schemas via element factories, e.g. `schema.add_element("id", [] { return std::make_unique<MyElement>(); });` (`s7tests/src/validation_smoke.cpp`).
- Ports are introspected via `Port{index,name,type,value_ptr}`; preserve `SignalType` correctness and typed access (`s7core/include/s7/element.hpp`).
- Respect execution ordering: `NodeRuntime::step()` runs `causal_order` then `memory_order` (`s7core/src/runtime.cpp`).
- Validation expects cycles to include at least one causal-memory element; do not bypass this in compiler/runtime paths.
- Observation is non-invasive: use `ObservationService` reads, never mutate runtime state from observe code.

## Integration and dependency notes
- C++20 project; top-level dependencies include Boost (JSON/Asio/Beast) and optional Qt6 (`CMakeLists.txt`).
- JSON schemas under `s7runtime/examples/` are canonical runtime inputs.
- External UI libs live in `external/` (e.g., `QCustomPlot`, `QSchematic`); avoid coupling core logic to them.

## Guardrails for AI edits
- Keep changes module-local unless cross-module contracts require expansion.
- If touching compiler/validator/runtime, add or update a smoke test in `s7tests/src/`.
- Prefer explicit errors around schema/type mismatches over silent coercion.
- Preserve public namespace and API surface conventions (`namespace s7`, public headers under `s7core/include/s7/`).

