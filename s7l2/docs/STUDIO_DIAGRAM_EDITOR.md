# Studio Diagram Editor

Internals of the `s7studio` diagram editor and its file format. Studio is a runtime client (ADR-014); the editor produces runtime schema JSON and never defines execution semantics. The design decisions behind the wiring are recorded in `STUDIO_DIAGRAM_WIRING_DESIGN.md`.

## Components

| File | Role |
|---|---|
| `s7studio/src/diagram/items.*` | Scene items, QSchematic subclasses: `ElementNode` (element id, type, params, nested schema, placement, title as typed members; one `PortConnector` per port), `PortConnector` (direction, index, name), `TextFrameNode`, `SignalWire` (its two `WireEnd`s, the bends placed by hand, and its current routed path) |
| `s7studio/src/diagram/element_ports.*` | The ports an element shows: nested schema `exposed_inputs`/`exposed_outputs`, registry metadata, `Adder` `weights`, `Multiply` `factors`, observer `Scope` channels |
| `s7studio/src/diagram/diagram_scene.*` | `DiagramScene` (a `QSchematic::Scene`): connection rules, gestures, undoable edits (`DiagramChange`), routing of changed wires, junction placement |
| `s7studio/src/diagram/net_model.*` | Connections read from the wire trees, wires that connect nothing, the runtime schema of the scene |
| `s7studio/src/diagram/wire_router.*` | Orthogonal A* router on the grid |
| `s7studio/src/scene_serializer.*` | `DiagramDocument` ↔ scene (format version 2); layered auto-layout for plain schemas |
| `s7studio/src/diagram_editor.*` | Widget hosting the scene and view, page frame (mm-based sheet), rulers, zoom; keys go to the scene first; emits node double-click signals |
| `s7studio/src/element_requirements.*` | The plugins a schema's element types come from (`requires`); finds and loads them on diagram load and reports what is missing |
| `s7studio/src/wire_type_labels.*` | Resolves the scene's types and labels the first wire of each net; marks wires that connect nothing. `s7studio --wire-types <file>` prints them, `S7_WIRE_TYPES_PNG=<png>` renders the result |
| `s7studio/src/diagram_document.*`, `diagram_format.hpp` | Diagram file model and format description |
| `s7studio/src/element_picker_panel.*` | Element Library tool window built from `ElementCatalogService`; double-click emits `elementRequested` |
| `s7studio/src/main_window.cpp` | Wiring: add element, parameter dialog, run, save/load, Undo/Redo, schema libraries |
| `s7studio/src/tool_windows.*` | JetBrains-style tool windows: docks with stripe buttons, one window per area, Alt+number toggles, Hide All Tool Windows |
| `s7studio/src/tool_panels.*` | Tool window contents: Problems, Log, Run, Deploy |
| `s7studio/src/ui_icons.*` | Tabler icons (`resources/icons/tabler`, MIT) drawn in the palette's text color or a given color |
| `s7studio/src/local_schema_runner.*`, `local_runtime_v2.hpp`, `scope_sample_mailbox.hpp` | Embedded runtime: builds the functional schema through the registry, runs it on a worker thread, and hands observer samples to the UI through a bounded mailbox (oldest dropped and counted when the UI falls behind) |

The diagram sources build as the `s7studio_diagram` library, shared by `s7studio` and `s7studio_diagram_test`.

## Main window

The layout follows the JetBrains IDEs (CLion, IntelliJ IDEA): the diagram fills the middle, everything else is a tool window with a button on a stripe at the window edge. The left stripe holds the bottom windows' buttons (at its bottom end), the right stripe the right windows'. An area shows one tool window at a time; its button or shortcut shows it, pressed again it hides it. Side windows run the full height; the bottom window sits between them.

| Tool window | Area | Shortcut | Content |
|---|---|---|---|
| Element Library | right | Alt+1 | Elements by category; double-click adds one (open at first start) |
| Headless Nodes | right | Alt+2 | Discovered runtime nodes, Studio TLS credentials |
| Run | bottom | Alt+4 | Run status, steps, messages of the run; run actions (run, pause, resume, stop, reset, Run Configuration) |
| Problems | bottom | Alt+6 | What stops the diagram from running or deploying: missing plugins and element types, wires that connect nothing, type errors; load warnings. Double-click shows the element or wire |
| Log | bottom | Alt+7 | Everything Studio logs; Clear |
| Deploy | bottom | Alt+8 | Outcome of the last deployment per node; Deploy and Open Deployed Diagram |

- View > Hide All Tool Windows (Ctrl+Shift+F12) gives the diagram the whole window; again brings the same windows back. View > Appearance hides the tool window bars and each toolbar.
- The status bar shows the problem count (a click opens Problems). Run and Deploy refuse a diagram with errors and open Problems; a failed run opens Run, a failed deployment opens Deploy. Message boxes are left for actions that need an answer.
- View > Fit Diagram to Window (Ctrl+8) zooms so the whole diagram fills the window; Fit Page (Ctrl+9) shows the whole sheet.
- Run settings (run time, discretization frequency and the resulting dt, clearing Scope traces) are in Run > Run Configuration.
- The window layout (geometry, open tool windows, toolbars) is restored at start (`QSettings`, organization `s7`, application `s7studio`).
- `s7studio <file>` opens a diagram, schema or element library at start; `S7_STUDIO_SCREENSHOT=<png>` saves a picture of the main window and quits.

## Local simulation with the bundled runtime

This is the [next release's focus](DEVELOPMENT_PLAN.md). The behavior below is implemented; the development plan's remaining quality gates are not yet all met.

Open a diagram (for example `s7studio examples/2int_osc.s7diagram`), use **Run > Run Configuration** to set duration and discretization frequency, then run locally and double-click the Scope observer. Studio embeds `s7core` through `LocalSchemaRunner`; no separate `s7runtime` process, network deployment or certificate configuration is needed for this workflow.

- **Model time:** frequency `f` selects `dt = 1/f` seconds. Studio updates rate-related element parameters and seeds the run's schema rate; validation still rejects incompatible rates/types. The worker steps as fast as it can, with pause/stop controls. Changing `f` changes numerical discretization and the number of steps for a fixed model duration, not a promised wall-clock execution rate. Samples carry the runtime step and model timestamp; causal-memory outputs are published on the next step, so reference comparisons must account for that publication convention.
- **Observation:** values are read on the runtime worker through `ObservationService`, then delivered to the UI through a bounded mailbox. Scope observers and their links do not participate in functional compilation or execution. Opening, hiding or reopening a Scope does not restart or change the model.
- **Scope history:** recording starts with the first sample delivered for a Scope, even before its window is shown. Closing hides the view and retains its recording; opening it again does not clear or replace the data. Clearing samples or clearing before a new run discards that history; closing the diagram/application releases it. These temporary recordings are not persistent saved sessions.
- **Preview versus recording:** each channel has a bounded preview (20,000 retained points plus the latest endpoint when needed), uniformly thinned over the run as it grows. At the buffer threshold, a temporary disk file retains delivered samples for detailed history/CSV export. Fit full run shows the recorded time extent; zoom/pan can read the disk-backed region; Auto Y rescales values without changing the time axis. Preview thinning cannot guarantee detail for arbitrary high-frequency signals; characterize the supported signal/rate/window combinations before relying on an overview plot alone.
- **CSV fidelity:** timestamps and values use 17 significant digits and a locale-independent decimal point, preserving finite double values on round trip. Scope/channel fields are CSV-escaped. The [nonlinear-pendulum benchmark](../examples/pendulum/README.md) verifies every captured/exported sample against headless execution over 100 seconds at 1/10 kHz, with two channels and no dropped samples on the tested Linux build.
- **Completeness and failures:** the mailbox is bounded at 200,000 samples across probes. It drops the oldest when the UI falls behind and reports the loss; temporary disk buffering cannot restore those samples. Scope reports temporary-file/write failures too. **Export Full CSV** exports the recorded samples; its name does not establish completeness if capture loss or a storage warning occurred. Disk use grows with delivered sample count even though preview memory is bounded.
- **Numerical method limits:** a smooth plot alone is not evidence of accurate dynamics. The two-`EulerIntegrator` oscillator has known forward-Euler amplitude growth; a finer `dt` reduces finite-duration error but does not make that method exactly energy-conserving. `SymplecticIntegrator` is available for the documented position/velocity formulation; it is not a general accuracy guarantee for arbitrary nonlinear systems. See [known integrator limits](OPEN_PROBLEMS.md) and the [numerical validation plan](VISUAL_NUMERICAL_TESTS_PLAN.md).

## Model

- **Nets are trees.** A net has one driver, an output port. Its first wire (the trunk) runs from that output to an input; every further wire starts at a junction on a wire of the same net and ends at an input. A `WireEnd` is either a port (`node`, `direction`, `index`) or a junction (`wire`, `at`). Junctions are drawn as dots; wires that only cross are not connected. Wires refer to ports by element and index, so ports rebuilt after a parameter change keep their wires.
- **Connections come from the trees:** each wire ending at an input connects the driver of its tree to that input (`NetModel`). Nothing is guessed from geometry.
- **Routes are cached.** A wire's route runs from its upstream end through its user bends to its downstream end; `WireRouter` routes each leg between them. Saves include both user bends and the current routed points. On load, Studio restores cached paths and reroutes only paths that are missing or structurally invalid (an end off its port, a junction off its trunk, a diagonal segment, or a missing user bend). Existing routes through overlapping elements are preserved because the router cannot resolve those collisions on load. Edits reroute the wires they touch, and moving elements reroutes their wires (including undo/redo of moves).
- **Junction placement:** a junction keeps its point while that point is on its trunk (not at the trunk's ends); otherwise it moves to the trunk point nearest to where the branch goes next.
- **Elements and observers:** inputs on the left edge, outputs on the right, no rotation. Width 9 grid steps, height `max(5, 2 × ports + 1)` steps; ports two steps apart where they fit, centred. All positions, ports, bends and junctions are on the 20-unit grid. The **Observers** library category contains `Scope`, a diagram-only observation client with variable signal inputs; connecting to its final `+` port adds another input. Observation wires are green and dashed, while functional signal wires remain black. Scope nodes and their wires persist in the diagram layout but are excluded from the runtime schema. Existing probe markers are not converted while a diagram loads. `TextFrame`s are diagram-only and not part of the runtime schema.
- **Undo:** every edit is a `DiagramChange` (nodes and wires added or removed, wires re-hung, bends, element edits) pushed as one command on the scene's `QUndoStack`; element moves use QSchematic's `ItemMove`. Edit > Undo/Redo. Text frame text and style edits and run-configuration params (`sample_rate_hz`, `dt`) are applied directly.

## Element types and plugins

Studio loads every plugin in the plugin search paths at start (`PLUGIN_INTERFACE.md`, "Discovery and loading"). On save, the schema's root `requires` lists the plugins its element types come from (`PLUGIN_INTERFACE.md`, "Plugins a schema requires"). On loading a diagram or schema (Open, the demo, Open Deployed Diagram), Studio searches the plugin paths again when a required plugin or an element type is missing, then reports what it still cannot find: the Problems tool window opens with the missing plugins and element types, the affected elements are outlined red with the reason as tooltip, and Run and Deploy refuse the diagram until they are available. The diagram stays editable.

## Rules

Refused when drawing, with the reason in the status bar:

| Connection | Result |
|---|---|
| output → free input | connected; a further wire of the same output branches off its first wire |
| input → wire, wire → input | connected, the wire end is a junction |
| output → output | refused: two outputs |
| output → wire of a net | refused: a net has one output |
| input → input, wire → wire | refused |
| anything → an input already fed | refused |
| incompatible types | refused when the diagram's types resolve without the connection and do not with it (same inference as the runtime, `s7::TypeResolver`) |

Deleting a wire deletes the connection to its input; wires hanging on it move up to its upstream end (a branch of a deleted trunk becomes the new trunk). Deleting an element deletes the wires into it and the whole nets it drives. Shrinking an element's ports (fewer `weights`, `channel_count`) deletes the wires on the removed ports. A wire that still connects nothing (not reachable through the rules; e.g. a hand-edited file) is drawn red with the reason as tooltip, is listed under Problems, is not saved, and Run and Deploy refuse the diagram while it exists.

## Interaction

- **Drag from a port** to a port or a wire: valid targets are marked with a green ring, refused ones are dimmed, and the routed wire is previewed. Alt+drag starts from a wire (a junction). Releasing anywhere else connects nothing.
- **Click by click (Wire Draw, `Ctrl+W`, or a click on a port without dragging):** click a port or a wire to start, click empty canvas to place bends, click a port or a wire to finish. Escape cancels the wire, then leaves the mode; Backspace/Delete removes the last bend; right-click cancels.
- **Editing a wire:** select it; drag a bend handle (filled: placed by hand, hollow: routed) or drag a straight segment. Context menu: Add Bend Here, Remove Bend, Re-route (remove hand-placed bends), Delete Wire.
- **Elements and observers:** move by dragging (wires follow and are re-routed on release); Delete removes the selection. Double-click: element properties dialog (ports, parameter specs, editable parameter JSON, Node placement); `Scope` opens a multi-channel plot window; `TextFrame` edits text.
- **Element Library tool window:** double-click adds an element; schema libraries can be loaded and saved from the File menu.
- **Placement:** the element properties dialog has a Node field listing the nodes found in the Headless Nodes tool window; placed elements show `@ <node>` under them.
- Run > Deploy to Nodes...: deploys the diagram to the nodes its elements are placed on, over mTLS with the CA, certificate and key set in the Headless Nodes tool window (the Studio certificate pinned by the nodes). It runs off the UI thread; the Deploy tool window shows each node's outcome; nothing is committed unless every node is ready.
- Run > Open Deployed Diagram...: loads the diagram a node runs (`GET_NETWORK_CONFIGURATION`) into the editor.

## Diagram file format (`.s7diagram`, version 2)

```json
{
  "document_kind": "s7.diagram",
  "diagram_version": 2,
  "id": "feedback_loop_demo",
  "dt": 0.001,
  "sheet": { "orientation": "landscape", "width_mm": 297.0, "height_mm": 210.0 },
  "schema": { "name": "...", "elements": [ ... ], "connections": [ ... ] },
  "diagram": {
    "grid": { "step": 20, "mm_per_step": 5.2917 },
    "nodes": [ { "id": "add1", "position": [17, 8], "title": "Sum" } ],
    "wires": [
      { "id": "w1", "from": { "port": ["c1", "out", 0] }, "to": { "port": ["add1", "in", 0] }, "bends": [], "route": [[11, 6], [15, 6], [15, 12], [25, 12]] },
      { "id": "w2", "from": { "wire": "w1", "at": [12, 10] }, "to": { "port": ["d1", "in", 0] }, "bends": [[12, 16], [30, 16]], "route": [[12, 10], [12, 16], [30, 16], [32, 16]] }
    ],
    "text_frames": [ { "id": "text_frame_1", "position": [2, 20], "params": { "text": "...", "width": 260.0, "height": 120.0 } } ]
  }
}
```

- `schema` is the runtime schema and the source of semantics; element params live there. `diagram` is layout only.
- `schema.requires` lists the plugins the element types come from (see "Element types and plugins").
- Coordinates are whole grid steps (`grid.step` scene units each; scene units are 96 per inch, so a step is 5.29 mm on the sheet). Text frame sizes are in scene units in their params.
- A wire's `from` is an output port or a junction on another wire; `to` is an input port. `bends` are the bends placed by hand. Optional `route` points are the current routed path in grid coordinates and act as a cache; older diagrams without them remain valid and Studio routes their wires on load. Structurally invalid cached routes are rerouted.
- On load the connections the wires make must equal `schema.connections`, otherwise loading fails naming the connection. A layout without `wires` is wired automatically (one trunk per output, further connections as branches); a document without `diagram` (a plain runtime schema) is also laid out by data flow (columns from inputs to outputs, rows by the mean row of the inputs).
- A layout of another `diagram_version` is refused unless it is empty. `s7studio --normalize <in> <out>` loads a diagram or schema and writes it as Studio saves it.
- Deployment: each schema element may carry `node` (the node_uuid it runs on); the root keeps `runtime_nodes` (`node_uuid`, `name`, `endpoint`) and `streams` (stream descriptors, so redeployments continue the streams). See `S7_Network_Wire_Protocol_v1.md`, section 12.1, items 1 and 6.

## Tests and tools

| Command | Checks |
|---|---|
| `s7studio_diagram` (`s7studio_diagram_test`) | Rules, fan-out branches, deleting a trunk and an element, undo, port count changes, moves, bends, drag/click/bend-drag gestures, save/reload of connections, bends and routes, refusal of a layout that does not match its schema, finding a required plugin and reporting missing plugins and element types |
| `s7studio --layout-check <schema>` | Auto-layout: every connection drawn and attached, orthogonal, no wire through an element, nets never touch (`S7_LAYOUT_PNG` renders it) |
| `s7studio --edit-check <diagram>` | Adding an element through the main window keeps connections and routes; undo; save/reload |
| `s7studio --wire-types <file>` | Type labels (`s7studio_wire_types`) |
| `s7studio_scope_view` | Scope preview resolution and bounded size at 1/10/100 kHz; latest endpoint and Auto Y preserving a zoomed time range |
| `s7studio --scope-run-check` (`s7studio_scope_run`) | Actual `examples/2int_osc.s7diagram` through the embedded runner at 1/10/100 kHz, ten-second extent/curve checks, late opening and close/reopen |
| `s7studio --deployment-diagram <in> <out>` | Placement and stream assignment as Deploy does it (`s7studio_deployment_diagram`) |

## Known gaps

- Drag-and-drop from the Element Library is not implemented.
- Routing is per wire in a fixed order (trunks first); a wire routed earlier is not moved to make room for a later one.
- Live preview while dragging an element stretches the attached wire ends; the wires are re-routed on release.

## QSchematic notes

QSchematic lives in `external/QSchematic` and is vendor code: do not edit it; Studio subclasses it. What Studio uses: `Scene` (items, selection, element moves through `ItemMove`, undo stack, the wire manager for connector attachments and junction dots), `View` (zoom, pan, grid), `Node`/`Connector`/`Wire` as base classes. What Studio replaces: QSchematic's `WireMode` (its internals are private and it attaches a wire to any wire it touches) by `DiagramScene::DrawMode`; wire point dragging by the scene's bend editing; the netlist by `NetModel`. Item types are `QSchematicItemUserType + n` (`diagram/items.hpp`). Studio keeps JSON as its file format instead of GPDS.
