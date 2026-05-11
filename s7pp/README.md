# S7 (Simulation 7)

S7 is a library for modeling causal discrete-time dynamical systems in a generalized Z-domain style.

## Documentation Index

- `S7_CONCEPT.md` — Core modeling and execution semantics
- `S7_ELEMENTS.md` — Built-in element reference (equations, pins, params)
- `docs/S7_TESTS.md` — DSP/control theory example test suite
- `docs/S7_RUNTIME_ARCHITECTURE.md` — Separate runtime/GUI, transport, and plugin architecture notes
- `docs/S7_CONTROL_PLANE.md` — Draft control-plane contract for centralized GUI and distributed runtime nodes
- `docs/S7_CONTROL_PROTOCOL_V0_DRAFT.md` — Working draft for node control command/event protocol (pre-socket)
- `docs/S7_BAKED_ENGINE.md` — Draft high-performance baked execution engine specification
- `docs/S7_PLUGIN_API.md` — Draft shared-library plugin ABI and element descriptor contract
- `docs/S7_BUILTIN_PLUGIN.md` — Built-in s7stdlib as pseudo-plugin with static linking and plugin ABI export
- `docs/S7_FILE_SOURCES.md` — File-based sources and sinks: FileSink, FileSource, S7SS format specification
- `docs/S7_FILE_IO_OOP.md` — New OOP file-I/O architecture (`RealFile*`/`ComplexFile*`, scalar codecs, sidecar metadata)
- `docs/S7_MEM_IO.md` — Shared-memory I/O first slice (`RealMemSink`/`RealMemSource`, SPSC ring)
- `s7io/README.md` — Current `s7fileio` OOP layout (real/complex classes, scalar encodings, JSON sidecar headers)
- `s7main/README.md` — Computation node CLI usage and responsibilities
- `s7ui/README.md` — Qt UI skeleton for schematic editing, visual sinks, and control shell layout

## Planned Documentation

As the project grows, this index can be extended with:

- architecture and scheduler internals
- netlist format guide
- cookbook/examples
- mathematical validation notes
- performance and profiling notes

## Quick Note

If you add new docs, please link them here so this file stays the single entry point.

