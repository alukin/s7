# Studio Diagram Editor

Internals of the `s7studio` diagram editor and its file format. Studio is a runtime client (ADR-014); the editor produces runtime schema JSON and never defines execution semantics.

## Components

| File | Role |
|---|---|
| `s7studio/src/diagram_editor.*` | Widget hosting the QSchematic `Scene`/`View`, page frame (mm-based sheet), rulers, zoom; emits node double-click signals |
| `s7studio/src/scene_serializer.*` | `DiagramDocument` ↔ QSchematic scene; creates nodes, connectors, and wires |
| `s7studio/src/runtime_schema_extractor.*` | Scene → runtime schema JSON (`elements`, `connections`) |
| `s7studio/src/diagram_document.*`, `diagram_format.hpp` | Diagram file model and format description |
| `s7studio/src/element_picker_panel.*` | Element Library dock built from `ElementCatalogService`; double-click emits `elementRequested` |
| `s7studio/src/main_window.cpp` | Wiring: add element, parameter dialog, run, save/load, schema libraries |
| `s7studio/src/local_schema_runner.*`, `local_runtime_v2.hpp` | Embedded runtime: builds the schema through the registry, runs it on a worker thread, emits Scope samples |


## Model and conventions

- Each scene node stores its schema identity in `QGraphicsItem` data roles (`diagram_node_roles.hpp`): id, type, title, params (JSON string), nested schema (JSON string), scope channel count, and text-frame styling.
- Input connectors are on the left edge, outputs on the right. The extractor derives port indices from connector geometry (left/right of node center, then sorted by y). Port counts come from built-in rules, element metadata, nested-schema `exposed_inputs`/`exposed_outputs`, and connections in the loaded schema.
- `Scope` is a Studio-only observer element (implemented in `local_runtime_v2.hpp`) with `channel_count` inputs. `TextFrame` nodes are diagram-only and are omitted from the runtime schema.
- Running a diagram serializes the current scene to schema JSON, fills `sample_rate_hz` / `dt` parameters from the run configuration, and passes it to `LocalSchemaRunner`.

## Diagram file format (`.s7diagram`)

```json
{
  "document_kind": "s7.diagram",
  "diagram_magic": "S7STUDIO_DIAGRAM_V1",
  "diagram_version": 1,
  "id": "feedback_loop_demo",
  "dt": 0.001,
  "units": "mm",
  "sheet": { "orientation": "landscape", "width_mm": 297.0, "height_mm": 210.0 },
  "schema": { "name": "...", "elements": [ ... ], "connections": [ ... ] },
  "diagram": {
    "version": 1,
    "nodes": [ { "id": "const1", "type": "Constant", "name": "Constant 2.0", "position": { "x": 50.0, "y": 100.0 } } ],
    "wires": [ { "id": "wire_0", "points": [ { "x": 100.0, "y": 100.0 }, { "x": 150.0, "y": 100.0 } ] } ]
  }
}
```

- `schema` is the runtime schema; parameters live there, not in `diagram`.
- `diagram.nodes` stores positions and titles (and `channel_count` for scopes, `params` for text frames).
- `diagram.wires` stores routing points in absolute scene coordinates. On load, wire *i* is matched with connection *i* of the schema; endpoints are re-attached to connectors.
- A plain runtime schema file can also be opened; Studio lays nodes out on a grid.

## Interaction

- Normal mode: select/move; Wire Draw mode (`Ctrl+W`): click a connector, intermediate points, then a target connector.
- Double-click a node: element properties dialog (ports, parameter specs, editable parameter JSON); `Scope` opens its plot window; `TextFrame` edits text.
- Element Library dock: double-click adds a node; schema libraries can be loaded and saved from the File menu.

## Known gaps

- No undo/redo: the scene owns a `QUndoStack`, but edits are not pushed as commands and several operations rebuild the whole scene.
- Some operations rebuild the scene from the stored schema and can drop interactively drawn wires (see `CODE_REVIEW_REQUIRED_FIXES.md`).
- Drag-and-drop from the Element Library is not implemented.

## QSchematic quick reference

QSchematic lives in `external/QSchematic` and is vendor code: do not edit it; wrap or subclass in `s7studio` instead.

```cpp
scene->setMode(QSchematic::Scene::NormalMode);   // or WireMode
connect(scene, &QSchematic::Scene::modeChanged, ...);
scene->setWireFactory([] { return std::make_shared<CustomWire>(); });

auto cmd = std::make_unique<QSchematic::Commands::ItemAdd>(scene, item);   // undo-aware add
scene->undoStack()->push(cmd.release());
scene->undoStack()->createUndoAction(parent, "Undo");

auto nodes = scene->nodes();
auto wires = scene->items<QSchematic::Items::Wire>();
scene->wire_manager()->attach_wire_to_connector(wire, pointIndex, connector);

QSchematic::Netlist<NodeT, ConnectorT> netlist;
QSchematic::NetlistGenerator::generate(netlist, *scene);
```

The QSchematic demo (`external/QSchematic/demo`) shows library widgets, undo/redo, and GPDS serialization. Studio deliberately keeps JSON as its file format instead of GPDS.
