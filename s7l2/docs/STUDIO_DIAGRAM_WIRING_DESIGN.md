# Studio Diagram Wiring: Design

Status: implemented 2026-10-06 (phases 1–4). `STUDIO_DIAGRAM_EDITOR.md` describes the editor as built; section 9 lists where the implementation departs from this proposal.

## 1. Problems this solves

- Saved routes are absolute polylines whose end points are replaced by connector positions on load; any change in port geometry leaves diagonal segments (`resources/demo_startup.s7diagram`, `diagram1.s7diagram`).
- Port indices are guessed from connector geometry; connections are guessed from wire end positions (1 px tolerance) and disagree with QSchematic's attachment records.
- A net with two outputs is silently dropped from the schema; output→output, input→input and incompatible types can be drawn.
- Moving a node only drags the wire end points; nothing re-routes.
- Adding an element or changing port counts rebuilds the whole scene from JSON (loses selection and undo, re-attaches wires by geometry).
- The format claims `"units": "mm"` but stores 96-dpi scene units.

## 2. Decisions

| Topic | Decision |
|---|---|
| Wire semantics | QSchematic's schematic model: touching wire ends make one net, junctions are drawn as dots, crossing wires without a dot are not connected. A net has exactly one driver (an output port) and any number of inputs. |
| QSchematic | Not modified. Everything is a subclass or an adapter in `s7studio`. |
| Drawing | Two gestures: drag from a port (new scene mode), and QSchematic's click-by-click Wire mode (Ctrl+W). Both apply the same connection rules. |
| Ports | Inputs on the left edge, outputs on the right, no flip or rotation. |
| Rules | Refused at draw time: output→output (two drivers in a net), a second driver into a net, incompatible types. |
| Routing | Automatic orthogonal routing; bends placed by hand are kept until removed by hand. |
| Format | `.s7diagram` v2; v1 is not read. The three existing files are converted once. |

## 3. Classes (`s7studio/src/diagram/`)

| Class | Base | Responsibility |
|---|---|---|
| `ElementNode` | `QSchematic::Items::Node` | Element id, type, title, params, nested schema, placement, Scope channel count as typed members (replace the `kNode*Role` data roles, which `deepCopy` does not copy). Owns its ports; `setPortCounts()` adds/removes `PortConnector`s in place. Paints the node body and title. |
| `TextFrameNode` | `QSchematic::Items::Node` | Diagram-only text frame (today's `TextFrame` special case). |
| `PortConnector` | `QSchematic::Items::Connector` | Direction (input/output), port index, port name (shown with the connector label), declared type from element metadata. Port identity no longer comes from geometry. |
| `SignalWire` | `QSchematic::Items::Wire` | Installed with `Scene::setWireFactory`, so interactively drawn wires are this class too. Marks each point as *anchor* (port end, junction, or user bend) or *routed*. Overrides `insert_point`/`move_point_to` to record user edits; draws the type label (replaces `wire_type_labels` child items). |
| `DiagramScene` | `QSchematic::Scene` | Adds `ConnectMode` (`Scene::UserMode`) for the drag gesture; filters clicks in `WireMode` (section 5); runs the net check on `netlistChanged`; triggers the router when nodes move. |
| `NetModel` | — | Builds nets from `QSchematic::NetlistGenerator` plus `PortConnector` directions: one driver, its inputs, and errors (no driver, two drivers). Produces the schema `connections`. Replaces `RuntimeSchemaExtractor`'s geometric matching. |
| `WireRouter` | — | Orthogonal router (section 6). |
| `DiagramCommands` | `QUndoCommand` | Undoable add/remove element, change params, change port counts, re-route; Studio gets Undo/Redo actions on the scene's `QUndoStack`. |

`SceneSerializer` becomes a thin reader/writer of format v2 that builds `ElementNode`s and `SignalWire`s; `MainWindow` stops calling `loadDocument` for edits and pushes commands instead.

## 4. File format v2

```json
{
  "document_kind": "s7.diagram",
  "diagram_version": 2,
  "id": "feedback_loop_demo",
  "dt": 0.001,
  "sheet": { "orientation": "landscape", "width_mm": 297.0, "height_mm": 210.0 },
  "schema": { "name": "...", "elements": [ ... ], "connections": [ ... ] },
  "diagram": {
    "grid": { "step": 20, "mm_per_step": 5.0 },
    "nodes": [
      { "id": "add1", "position": [10, 4], "size": [9, 5], "title": "Adder" }
    ],
    "wires": [
      { "id": "w1", "from": { "port": ["add1", "out", 0] }, "to": { "port": ["g1", "in", 0] }, "bends": [] },
      { "id": "w2", "from": { "wire": "w1", "at": [21, 6] }, "to": { "port": ["d1", "in", 0] }, "bends": [[21, 14], [30, 14]] }
    ],
    "text_frames": [ { "id": "note1", "position": [2, 20], "size": [13, 6], "params": { "text": "..." } } ]
  }
}
```

- Coordinates are integer grid steps; `grid.step` is the scene size of a step, `mm_per_step` maps the sheet to the grid (no more scene-unit/mm confusion).
- A wire end is a port (`[element, "in"|"out", index]`) or a junction on another wire (`wire` + `at`). This is QSchematic's own structure (one wire per connector, branches start on wires), stored explicitly instead of guessed.
- `bends` holds only user bends; routed points are not saved and are recomputed on load, so changes in port geometry can no longer produce diagonals.
- `schema.connections` stays the source of semantics. On load the connections derived from `wires` must equal `schema.connections`; a mismatch is a load error naming the connection (no silent repair).
- Element params stay in `schema`; `diagram.nodes` holds layout only. Node size is saved.
- A plain runtime schema still opens with the layered auto-layout; its wires are produced by the router instead of the hand-written track allocation in `LayeredLayout`.

## 5. Interaction

**Drag from a port (`ConnectMode`, entered automatically on press over a port in normal mode).**
Press on a port; while dragging, compatible targets are highlighted, incompatible ones dimmed, and the routed wire is previewed. Release on a highlighted port connects; release on an existing wire creates a junction on it (allowed only if the merged net stays valid); release on empty canvas cancels. Pressing on a wire with Alt starts a branch from that point.

**Wire mode (Ctrl+W), click by click.**
QSchematic's gesture is kept for hand-drawn routes; its clicks become user bends. `WireMode` internals are private, so `DiagramScene::mousePressEvent` checks each click before passing it on: the wire being drawn is known because `setWireFactory` creates it, and a click that would attach it to an incompatible port or merge it into a net with another driver is not forwarded (status bar says why). Escape removes a wire that is not attached at both ends.

**Editing wires.** QSchematic already provides dragging a bend, dragging a straight segment, and the context menu *Add point* / *Remove point*. `SignalWire` records these as user bends; *Remove point* on a user bend returns that part of the wire to the router. Added: *Re-route* (drop all user bends of the wire) and *Delete wire*.

**Fan-out from a pin.** Starting a second wire on an output that already has one makes it a junction on the first wire's end (QSchematic attaches one wire per connector). If the first wire is deleted, `DiagramScene` re-attaches the remaining wire to the port on `netlistChanged`.

**Checks.** The net check runs on every `netlistChanged`; any invalid net that still appears (e.g. a node dropped so that wire ends touch) is drawn red with the reason in the tooltip, and Run/Deploy are disabled until it is fixed. Types are checked with the same inference as `wire_type_labels` (`local_runtime_v2.hpp`), run on the scene with the candidate connection added.

## 6. Router

- Grid-based search (A* on grid steps) between consecutive anchors of a wire: port, user bends, port or junction. Each leg is routed separately, so user bends are kept exactly.
- Costs: length + bend penalty + crossing penalty; node rectangles plus one grid step of clearance are blocked; a wire leaves an output to the right and enters an input from the left for at least one step.
- Wire legs of other nets are passable but costly to run along (a shared segment would merge nets): touching another net is forbidden, crossing is allowed.
- Branch junction points that are not user-placed move to the point of the trunk nearest to the branch target after the trunk is routed.
- Triggered when: a connection is made, a node's move finishes (wires of that node only; live preview during the drag is a straight-angle stretch as today), port counts change, a diagram without routes is loaded. Each re-route is one undo command.

## 7. Implementation phases

1. `ElementNode`, `PortConnector`, `SignalWire`, `DiagramScene`, `NetModel`; format v2 read/write; convert the three diagram files. Edits through undo commands, Undo/Redo actions. No router yet: wires are straight-angle stretched as QSchematic does today.
2. Connection rules in both gestures; drag-from-port gesture; net check display.
3. `WireRouter`; replace `LayeredLayout`'s wire tracks; re-route on node move.
4. Docs: rewrite `STUDIO_DIAGRAM_EDITOR.md`, drop this note's "proposal" status.

## 8. Tests (offscreen, `s7studio/tests`)

- Save → load round trip keeps nodes, wires, junctions and user bends exactly; derived connections equal `schema.connections`.
- Changing a node's port geometry and reloading never produces a non-orthogonal segment.
- Output→output, second driver and incompatible types are refused in both gestures.
- Fan-out from a pin, then deleting the first wire, keeps the second connected.
- Routed wires never cross a node rectangle; a node move re-routes only its wires and keeps user bends.
- `--layout-check` and `--wire-types` keep working on the new scene classes.

## 9. As built

- **Click-by-click drawing is Studio's own `DiagramScene::DrawMode`**, not QSchematic's `WireMode`: `WireMode` keeps its wire private, attaches the wire to any wire a click lands on, and pushes its own undo command on the first click. Ctrl+W and the Wire Draw action switch to `DrawMode`; a click on a port without dragging also starts it. Its clicks are the wire's user bends, as proposed.
- **`NetModel` reads the connections from Studio's wire trees** (`WireEnd`s of each `SignalWire`), not from `QSchematic::NetlistGenerator`. QSchematic's wire manager is kept in step only for connector attachments (wire ends follow an element while it is dragged) and junction dots.
- **One undo command type**, `DiagramChange` (nodes and wires added or removed, wires re-hung, bends, element edits), instead of a class per edit. Element moves use QSchematic's `ItemMove`; the scene re-routes the wires of moved elements when the undo stack changes.
- **Format:** element size is not saved (it follows from the ports); text frames keep their size in their params; a layout without `wires` is wired automatically (as is a plain schema).
- **Repair:** besides the wires an edit touches, any wire whose route no longer fits (an end off its port, a junction off its trunk, a segment through an element, a user bend off the route) is re-routed.
- **Type refusals** apply when the diagram's types resolve without the new connection and do not with it; a diagram whose types do not resolve yet (or uses element types unknown to the registry) refuses nothing on type grounds.
- **Deleting** a trunk promotes its first branch to trunk; deleting an element removes the nets it drives.

