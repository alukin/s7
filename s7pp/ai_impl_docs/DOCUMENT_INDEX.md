# AI Implementation Docs Index

This folder now contains only the compact active/reference set for the project.
Historical snapshots and milestone writeups were moved to `obsolete_docs/`.

## Core active references

- `README.md` - top-level repository entry point
- `ai_impl_docs/S7_CONCEPT.md` - modeling concepts and terminology
- `ai_impl_docs/S7_ELEMENTS.md` - built-in element behavior reference
- `ai_impl_docs/S7UI_DIAGRAM_SPEC_V1.md` - Diagram vs Schema contract and visual-layer rules
- `ai_impl_docs/S7_FILE_IO_OOP.md` - active file I/O design (`s7io/s7fileio`)

## UI / authoring references

- `s7ui/README.md` - current UI architecture and file model
- `s7ui/OSCILLOSCOPE_WIDGET.md` - oscilloscope widget API
- `ai_impl_docs/S7_OSCILLOSCOPE_PROBE_DESIGN.md` - probe/oscilloscope design intent
- `ai_impl_docs/OSCILLOSCOPE_ARCHITECTURE_DIAGRAMS.md` - oscilloscope architecture diagrams
- `ai_impl_docs/SHAPE_AWARE_NODES.md` - current shape-aware node reference
- `ai_impl_docs/IMPLEMENTATION_SHAPE_AWARE_NODES.md` - implementation details for the current shape-aware model

## Runtime / integration references

- `S7_RUNTIME_ARCHITECTURE.md` - runtime and distributed architecture direction
- `S7_PLUGIN_API.md` - plugin ABI and descriptor contract
- `S7_TESTS.md` - test suite overview
- `S7_CONTROL_PLANE.md` - control-plane architecture draft
- `S7_CONTROL_PROTOCOL_V0_DRAFT.md` - control protocol draft
- `ai_impl_docs/S7_BAKED_ENGINE.md` - baked-engine draft
- `ai_impl_docs/S7_BUILTIN_PLUGIN.md` - built-in plugin export notes
- `ai_impl_docs/S7_MEM_IO.md` - shared-memory I/O notes
- `ai_impl_docs/S7_NET_IO.md` - network I/O notes
- `s7io/README.md` - current I/O module behavior
- `s7main/README.md` - runtime entry-point notes

## JSON schema contracts

- `ai_impl_docs/json_schemas/diagram-document.schema.json`
- `ai_impl_docs/json_schemas/compiled-schema.schema.json`
- `ai_impl_docs/json_schemas/visual-spec.schema.json`

## Archive

- `obsolete_docs/README.md` - archived milestone notes, older summaries, and obsolete implementation writeups

## Path convention

- Use repository-relative paths (for example: `ai_impl_docs/S7_CONCEPT.md`).
- Avoid obsolete `/docs/...` references; that directory is not used in this repository layout.

## Suggested review order

1. `README.md`
2. `ai_impl_docs/DOCUMENT_INDEX.md`
3. `ai_impl_docs/S7_CONCEPT.md`
4. `ai_impl_docs/S7_ELEMENTS.md`
5. `ai_impl_docs/S7UI_DIAGRAM_SPEC_V1.md`
6. `s7ui/README.md`
7. `S7_RUNTIME_ARCHITECTURE.md`

Last updated: June 2, 2026.

