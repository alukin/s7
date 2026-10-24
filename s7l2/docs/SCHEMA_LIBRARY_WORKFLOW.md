# Schema Library Workflow

## Purpose

This document describes how to build, save, load, and run schema-based element libraries in S7.

Schema libraries let you package a `Schema` as a reusable element type and register it in the same `ElementRegistry` used by built-in and plugin elements.

---

## Library Document Format

A schema library document is a JSON object with:

- `document_kind`: must be `"s7.element_library"`
- `name`: library name
- `elements`: array of schema-backed element definitions

Each element entry contains:

- `type_name`: unique registry type name
- `display_name`: UI label
- `description`: element description
- `category`: picker/category grouping
- `version`: semantic-style version string
- `schema`: runtime schema JSON used to instantiate the element

### Minimal Example

```json
{
  "document_kind": "s7.element_library",
  "name": "demo_schema_library",
  "elements": [
    {
      "type_name": "DoubleGain",
      "display_name": "Double Gain",
      "description": "Schema-based gain x2",
      "category": "demo",
      "version": "1.0.0",
      "schema": {
        "name": "double_gain",
        "elements": [
          { "id": "gain", "type": "Gain", "params": { "factor": 2.0 } }
        ],
        "connections": [],
        "exposed_inputs": [
          { "name": "in", "type": "F64", "element": "gain", "port": 0 }
        ],
        "exposed_outputs": [
          { "name": "out", "type": "F64", "element": "gain", "port": 0 }
        ]
      }
    }
  ]
}
```

Reference example in repository:

- `s7runtime/examples/schema_library_demo.json`

---

## Bundled resource and simulation model

Reusable controllers and other standard components live in the separate
`s7core/resources/schema_elements.s7library` resource. They use the existing
schema-is-element mechanism and are automatically registered with core.
Simulation follows the [analog-computer model](SIMULATION_MODEL.md); independent
RK4 and other solvers are permitted as test references.

See [CONTROL_ELEMENTS.md](CONTROL_ELEMENTS.md) for the bundled components,
configuration and numerical contracts.

## Studio Workflow

### 1) Export a Schema as a Library Element

In `s7studio`:

1. Build or open the schema/diagram you want to package.
2. Use `File → Element Libraries → Save Diagram as Library Element…`.
3. Choose output file (`.s7library` or `.json`).
4. Fill metadata fields (`type_name`, display name, category, version).

Result:

- The library file is written/updated.
- The new element is registered immediately in the current session.
- The Element Library picker refreshes automatically.

### 2) Load an Existing Library

In `s7studio`:

1. Use `File → Element Libraries → Load Element Library…`.
2. Select a `s7.element_library` document.
3. The element types from that file are registered.

You can also open library files via `File -> Open...`.

### 3) Use a Schema-Based Element in a Diagram

- Add the element from the picker as usual.
- The node stores its embedded `schema` payload in the diagram/runtime JSON.
- Saving/reloading diagram documents preserves this payload.
- Bundled and user-loaded schema elements appear in the separate **Schema elements** category.
- Double-click an instance to view/edit its embedded graph in a new tab; double-click nested schema elements to navigate deeper.
- Edits affect that instance and propagate into the parent diagram, preserving the exposed interface and internal layout. The registry/library template is unchanged.
- Save and Run use the root diagram; Save Diagram as Library Element from a schema tab exports that schema.

---

## Runtime Workflow (Headless)

`s7runtime` supports loading one or more libraries before schema execution:

```bash
./build/s7runtime/s7runtime --library path/to/lib1.s7library --library path/to/lib2.json --schema path/to/runtime_schema.json
```

Notes:

- Library files may be plain JSON (`.json`, `.s7library`) or gzip-compressed (`.gz`).
- Library element types must be unique in the process registry.
- Runtime reports loaded library count (`runtime.library_count=...`).

---

## Validation and Constraints

- `type_name` must be unique across built-ins, plugins, and previously loaded libraries.
- Duplicate type names in a single library document are rejected.
- Each library element must include `schema` as an object.
- Port exposure is derived from schema `exposed_inputs` / `exposed_outputs`.
- Any runtime element entry with a nested `schema` payload is treated as a composite schema-backed element.

---

## Integration Points

Core loader API:

- `s7core/include/s7/schema_library.hpp`
- `s7core/src/schema_library.cpp`

Runtime registration path:

- `s7runtime/src/main.cpp` (`--library`)
- `s7runtime/src/runtime_service.cpp`

Studio integration path:

- `s7studio/src/main_window.cpp`
- `s7studio/src/scene_serializer.cpp`
- `s7studio/src/diagram/net_model.cpp`

---

## Troubleshooting

### "Element type already registered"

Cause:

- The same `type_name` is already loaded from built-ins, plugins, or another library.

Fix:

- Rename `type_name` or start a clean process and load only one definition.

### "Library element ... is missing a schema definition"

Cause:

- Element entry omitted `schema`.

Fix:

- Add a valid schema object under the element.

### Element appears with wrong I/O in picker

Cause:

- `exposed_inputs` / `exposed_outputs` missing or incomplete.

Fix:

- Define exposed ports in the nested schema.


