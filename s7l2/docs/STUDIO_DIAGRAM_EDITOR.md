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

**Help → S7 Studio Help** (F1) opens the embedded offline guide, with article
search, a bundled-examples catalogue, family notes and Back/Forward/Home
navigation. Help windows are modeless and reused when reopened.
**Help → About S7 Studio** lists author Oleksiy Lukin, contributor teren-ukr,
acknowledgements, version/revision/compiler/Qt/system details and licence texts.
**About Qt** is also available. Help works without a source checkout or network;
explicit HTTPS links open the system browser.

The layout follows the JetBrains IDEs (CLion, IntelliJ IDEA): the diagram fills the middle, everything else is a tool window with a button on a stripe at the window edge. The left stripe holds the bottom windows' buttons (at its bottom end), the right stripe the right windows'. An area shows one tool window at a time; its button or shortcut shows it, pressed again it hides it. Side windows run the full height; the bottom window sits between them.

| Tool window | Area | Shortcut | Content |
|---|---|---|---|
| Element Library | right | Alt+1 | Elements by category; double-click adds one (open at first start) |
| Headless Nodes | right | Alt+2 | Discovered runtime nodes, Studio TLS credentials |
| Run | bottom | Alt+4 | Run status, steps, messages of the run; run actions (run, pause, resume, stop, reset, Run Configuration) |
| Problems | bottom | Alt+6 | What stops the diagram from running or deploying: missing plugins and element types, wires that connect nothing, type errors; load warnings. Double-click shows the element or wire |
| Log | bottom | Alt+7 | Everything Studio logs; Clear |
| Deploy | bottom | Alt+8 | Outcome of the last deployment per node; Deploy and Open Deployed Diagram |

- Tools opens/toggles the existing tool windows with the same Alt+number shortcuts.
- View > Hide All Tool Windows (Ctrl+Shift+F12) gives the diagram the whole window; again brings the same windows back. View > Appearance hides the tool window bars and each toolbar.
- The status bar shows the problem count (a click opens Problems). Run and Deploy refuse a diagram with errors and open Problems; a failed run opens Run, a failed deployment opens Deploy. Message boxes are left for actions that need an answer.
- View > Fit Diagram to Window (Ctrl+8) zooms so the whole diagram fills the window; Fit Page (Ctrl+9) shows the whole sheet.
- Run settings (run time, discretization frequency and the resulting dt, clearing Scope traces) are in Run > Run Configuration.
- Run duration accepts fractional seconds (up to nine decimal places). The
  dialog warns when a finite duration produces fewer than 100 whole steps;
  this warning does not block the run. Invalid negative durations other than
  `-1` (continuous) are rejected. Fractional durations persist in `studio_run`.
- The window layout (geometry, open tool windows, toolbars) is restored at start (`QSettings`, organization `s7`, application `s7studio`).
- `s7studio <file>` opens a diagram, schema or element library at start; `S7_STUDIO_SCREENSHOT=<png>` saves a picture of the main window and quits.

## Local simulation with the bundled runtime

This is the [next release's focus](DEVELOPMENT_PLAN.md). The behavior below is implemented; the development plan's remaining quality gates are not yet all met.

**File > Recent Diagrams** lists the ten most recently opened or saved diagrams,
newest first, with their directories shown to distinguish matching filenames.
Opening an entry moves it to the top; **Clear recent diagrams** clears the
stored list. Missing files remain visible but disabled. Runtime schemas,
element libraries and the bundled demo are not added automatically.

At startup Studio reopens the most recent saved diagram. If that file is
missing, unreadable or invalid, it logs the failure and tries earlier entries;
if none can be opened, it loads the demo. Passing a file on the command line
opens that file directly and bypasses history restoration. History stores file
paths in Studio settings; it does not recover unsaved edits. On opening a
diagram, Studio fits the full sheet after the editor becomes visible and its
viewport is laid out. **View > Fit Diagram to Window** (Ctrl+8) fits the diagram
contents; **View > Fit Page** (Ctrl+9) fits the sheet. Subsequent window resizing
preserves the selected zoom.

Open a diagram (for example `s7studio examples/2int_osc.s7diagram`), use **Run > Run Configuration** to set duration and discretization frequency, then run locally and double-click the Scope observer. Studio embeds `s7core` through `LocalSchemaRunner`; no separate `s7runtime` process, network deployment or certificate configuration is needed for this workflow.

- **Model time:** frequency `f` selects `dt = 1/f` seconds. Studio updates rate-related element parameters and seeds the run's schema rate; validation still rejects incompatible rates/types. The worker steps as fast as it can, with pause/stop controls. Changing `f` changes numerical discretization and the number of steps for a fixed model duration, not a promised wall-clock execution rate. Samples carry the runtime step and model timestamp; causal-memory outputs are published on the next step, so reference comparisons must account for that publication convention.
- **Observation:** values are read on the runtime worker through `ObservationService`, then delivered to the UI through a bounded mailbox. Scope observers and their links do not participate in functional compilation or execution. Opening, hiding or reopening a Scope does not restart or change the model.
- **Scope history:** recording starts with the first sample delivered for a Scope, even before its window is shown. Closing hides the view and retains its recording; opening it again does not clear or replace the data. **Clear current** or starting a new run discards the current history; closing the diagram/application releases temporary buffers. **Save Run…** persists the current samples to `.s7scope`; **Open Run…** overlays saved results for comparison. Imported comparisons survive new runs and can be removed separately. See [saving and comparing results](SCOPE_RESULTS.md) for snapshot semantics, completeness metadata and the format.
- **Viewers:** Scope, Phase Portrait and 3D Phase Trajectory windows are drawn with Qt Graphs and Qt Quick 3D; their registry, lifecycle and tests are described in [STUDIO_OBSERVATION_VIEWERS.md](STUDIO_OBSERVATION_VIEWERS.md). A diagram may hold any number of observers, including several of one type, each with its own viewer.
- **Phase Portrait:** the diagram observer plots its X/Y input states paired by runtime step. It records delivered pairs before its window is opened; closing hides the viewer and reopening preserves the trace. Clear/reset removes old pairs. The bounded trajectory retains at most two million pairs and draws at most 200,000 (a uniform subsample beyond that; export keeps every pair). See the [two-integrator example and verification](../examples/2int_osc_phase_portrait.md).
- **3D Phase Trajectory:** draws same-step X/Y/Z triples as a continuous 3D curve (Qt Quick 3D): drag rotates, the wheel zooms, and the XY/XZ/YZ buttons show the orthogonal projections. Diagram observers collect while hidden and preserve history on reopening, with a two-million-triple limit. See the [three-integrator helix example](../examples/3int_helix_phase_trajectory.md).
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
- **Elements and observers:** move by dragging (wires follow and are re-routed on release); Delete removes the selection. Double-click or select and press F4/Enter: element properties with metadata-driven fields, inline validation and Advanced JSON; ports and deployment are expandable; `Scope` opens a multi-channel plot window; `TextFrame` edits text.
- **Element Library tool window:** double-click adds an element; schema libraries can be loaded and saved from the File menu.
- **Placement:** the element properties dialog has a Node field listing the nodes found in the Headless Nodes tool window; placed elements show `@ <node>` under them.
- Nodes → Deploy to Nodes…: deploys the diagram to the nodes its elements are placed on, over mTLS with the CA, certificate and key set in the Headless Nodes tool window (the Studio certificate pinned by the nodes). It runs off the UI thread; the Deploy tool window shows each node's outcome; nothing is committed unless every node is ready.
- Nodes → Open Deployed Diagram…: loads the diagram a node runs (`GET_NETWORK_CONFIGURATION`) into the editor.

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

## Save reliability

Diagram saves and schema-library exports use atomic QSaveFile replacement.
Studio reports success only after a complete write and successful commit;
when a replacement fails, the original destination remains intact and the
new path is not adopted. Direct truncating fallback is disabled.
The `s7studio_atomic_file` regression injects an actual Linux write failure
using a child-only file-size limit and verifies destination preservation.

Modified diagrams are protected by Save / Discard / Cancel on application or
diagram close and document replacement (including recent and deployed diagrams).
Cancel and failed Save preserve the current document. Dirty state compares the
persisted document, so undoing back to the saved contents clears it; nested
schema edits participate through their parent instance. Local run duration and
clear-trace settings persist in `studio_run`, outside the Functional Graph.
Replacing a document stops and joins its embedded worker and clears its mailbox.

## Local run lifecycle and editing during a run

The embedded runner owns a const `RunConfiguration` snapshot: a monotonically
increasing run ID, schema JSON, dt, requested step count and probe bindings.
The Run panel always identifies that snapshot's rate/count/ID and says that edits
apply to the next run. Parameter, discretization, duration and nested-schema edits
never mutate the executing graph. A subsequent Run compiles the edited document.

| State | Meaning and controls |
|---|---|
| Idle | No prepared run; Run is enabled. |
| Preparing / Prepared | Validate/compile the copied schema, then prepare execution. Preparation errors enter Error. |
| Running | Step the frozen graph; Pause and Stop are enabled. |
| Pausing | Pause requested; finish the current step and publish its samples. Resume is disabled in the UI until acknowledgment. |
| Paused | Worker acknowledges a complete step boundary and waits on a condition variable. Resume or Stop wakes it. Previously queued samples may still be delivered. |
| Stopping | Stop requested; no new run can begin until worker finalization. Stop itself does not join/block the UI. |
| Stopped / Completed | Manual stop / requested finite steps finished. Run is enabled; retained configuration identifies the results. |
| Error | Preparation or worker failure; the Run panel contains the error and Run permits recovery. |

Reset joins the retiring worker, clears its runtime/mailbox/configuration, and
returns to Idle. Each run/reset invalidates queued owner notifications; sample
batches and samples carry their originating run ID. The window also guards its
queued callbacks by generation and shutdown state. State getters are authoritative:
a notification overtaken by a newer transition need not be presented as current.
Runner commands and snapshot access belong to the QObject's owner thread.

Observer bindings/types are frozen with the run. Existing windows continue to
show the original snapshot if the diagram changes. Opening a newly added or
rewired/retyped observer asks the user to rerun through a log/status message;
it cannot silently subscribe to an old graph. Destroying and recreating a viewer
uses its existing run binding, while a newly started run clears all phase alignment
and live history. With **Keep Previous Scope Traces** checked, the preceding
Scope trace becomes a separately labelled `Run <ID>` comparison (including its
capture-loss/storage warnings), rather than appending restarted timestamps to one
series. Existing saved comparisons remain available; phase views restart.

Document replacement stops/joins the old worker before loading the new graph.
Application close and destruction invalidate callbacks and stop/join/release the
runtime before destroying viewers, schema tabs or the plugin loader. A closed
window cannot be repopulated by queued samples/completion events.

Stop is cooperative: an executing element step or synchronous compilation cannot
be safely interrupted midway. Reset/close/destruction wait for that work to return;
no universal shutdown bound is claimed for a plugin that blocks indefinitely.
Pause acknowledgment removes polling delays but does not promise a wall-clock
execution rate. Recording completeness remains a separate limitation described
in SCOPE_RESULTS.md.

`s7studio_run_lifecycle` tests gated step boundaries, run snapshots/IDs,
retired notifications, rapid transitions, cancellation, worker exceptions and
recovery. `s7studio_window_lifecycle` tests the real window's running/paused
replacement and shutdown, active edits, observer lifetime/type changes, run
comparison isolation, phase alignment reset and error recovery. Their synchronization
uses explicit step gates and state conditions, not arbitrary sleep intervals.


## Editing and keyboard access

The active diagram supports Ctrl+C/Ctrl+X/Ctrl+V and Ctrl+A, also available
in Edit. Copy includes selected elements/annotation frames and connections
whose driver and destination are both selected, including observer links.
Wires alone cannot be copied. Pasted elements/frames have fresh IDs; named
probes get distinct names. Parameters, nested schemas, titles, user bends and
relative placement are retained. Paste is one undo step. It avoids the occupied
content bounds; large groups may be placed outside the sheet, where Fit Diagram
(Ctrl+8) and scrolling make them accessible for repositioning. Drawing/paste
never changes runtime causality or creates links to unselected elements.

Probe add/rename/remove share undo history. Deleting an element or a removed
output removes its probes in the same command; undo restores them with wiring.
Selection is not document data. Ctrl+0 resets zoom; Ctrl+9 fits the sheet;
Ctrl+8 fits the diagram. Ctrl+W toggles wire drawing and Escape cancels it.

Properties show editable display Name and read-only Type, Library and Internal
ID. Display-name edits preserve IDs and all connections. Library provenance
comes from owning-module metadata, the plugin loader or the schema-library
registration, never a category/type-name guess. F4/Enter opens Properties for
schema elements and observers too; double-click retains nested-editor/viewer
navigation.

Diagram > Diagram Properties edits `schema.name` and the optional top-level
`description` string. Missing descriptions mean empty text; present non-string
values are rejected. Multiline/Unicode notes survive edits, Save As and reopen,
and remain outside runtime schema semantics. The root tab uses the name with a
file-basename/Untitled fallback; nested tabs keep their existing navigation.

Properties validate known parameters using registry metadata before applying.
Numeric fields retain JSON text precision (including uint64 and complex arrays);
booleans/enums use choices and optional parameters without defaults have a Set
checkbox. Advanced JSON is retained for arrays, imported data and expert work.
Whole-graph/type-dependent validation still occurs when compiling/running.
Cancel leaves the document unchanged. Deployment placement is optional for local
simulation. F5 opens the local run dialog; Shift+F5 stops; Ctrl+Shift+R opens
Run Configuration. Duration and step rate describe model time, not wall-clock
pacing. Simulation actions keep their engineering transport icons and text labels.
