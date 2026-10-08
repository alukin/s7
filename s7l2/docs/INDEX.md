# S7 Documentation Index (C++ implementation)

S7 is in pre-MVP development. Backward compatibility of APIs, file formats, and protocols is not a goal yet; documents describe the current design direction and say explicitly where the code differs.

The language-neutral documentation (vision, formal model, architecture, terminology, decisions, and the control and wire protocols) is in [`../../doc`](../../doc/README.md). This directory documents the C++ implementation and Studio.

## Start here

1. `../README.md` — what this implementation contains, how to build and run it.
2. `PROJECT_STATUS_AND_PLAN_2026-09-20.md` — current priorities.
3. `CODE_REVIEW_REQUIRED_FIXES.md` — known defects and required fixes, prioritized.

## Implementation architecture

| Document | Content |
|---|---|
| `S7_Transport_Architecture.md` | `s7transport` / `s7runtime` / `s7net` boundaries, security and data-plane baseline |

## Developer guides

| Document | Content |
|---|---|
| `ELEMENT_DEV_GUIDE.md` | Writing elements: element contract, metadata, registration, parameters |
| `PLUGIN_INTERFACE.md` | C ABI for element plugins, discovery paths, loading process |
| `SCHEMA_LIBRARY_WORKFLOW.md` | Schema-backed element libraries in Studio and the runtime |
| `STUDIO_DIAGRAM_EDITOR.md` | Studio diagram editor internals, diagram file format, QSchematic notes |
| `STUDIO_DIAGRAM_WIRING_DESIGN.md` | Design record of the diagram wiring: QSchematic subclasses, format v2, connection rules, router (implemented) |
| `LOCAL_HEADLESS_NODES_DEMO.md` | Run two local headless runtimes and discover them in Studio |
| `WINDOWS_BUILD.md` | Cross-building Windows binaries on Linux (Clang, MSYS2 sysroot), testing under Wine, packaging |

Module READMEs: `../s7transport/README.md`, `../s7noise/README.md`, `../s7spectral/README.md`.

Conformance data for other implementations: `../s7transport/tests/fixtures/README.md` (wire format and stream descriptors) and `../s7runtime/tests/fixtures/README.md` (control protocol).

## Source entry points

- `s7core/include/s7/element.hpp` — `Element` base class and port model
- `s7core/include/s7/elements.hpp` — built-in elements
- `s7core/include/s7/element_registry.hpp`, `element_metadata.hpp` — registry and metadata
- `s7core/include/s7/schema.hpp`, `compiler.hpp`, `runtime.hpp` — schema, compiler, `NodeRuntime`
- `s7core/include/s7/schema_library.hpp` — schema library loader
- `s7runtime/src/runtime_service.cpp` — JSON schema loading, node identity, control-plane wiring
- `s7runtime/src/control_plane_mvp.cpp` — control-plane message handlers
- `s7transport/src/transport.cpp` — in-memory and WebSocket transport backends
