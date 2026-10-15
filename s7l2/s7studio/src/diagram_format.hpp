#pragma once

namespace s7studio::diagram {

// Diagram file format, version 2 (docs/STUDIO_DIAGRAM_EDITOR.md, "Diagram
// file format"). A diagram document is a runtime schema plus its layout:
//
// {
//   "document_kind": "s7.diagram",
//   "diagram_version": 2,
//   "id": "feedback_loop_demo",
//   "dt": 0.001,
//   "sheet": { "orientation": "landscape", "width_mm": 297.0, "height_mm": 210.0 },
//   "schema": {
//     "name": "feedback_loop_demo",
//     "elements": [
//       { "id": "const1", "type": "Constant", "params": { "value": 2.0 } },
//       { "id": "add1", "type": "Adder", "params": {} },
//       { "id": "delay1", "type": "Delay", "params": { "initial_value": 1.0 } }
//     ],
//     "connections": [
//       { "from_element": "const1", "from_port": 0, "to_element": "add1", "to_port": 0 },
//       { "from_element": "delay1", "from_port": 0, "to_element": "add1", "to_port": 1 },
//       { "from_element": "add1", "from_port": 0, "to_element": "delay1", "to_port": 0 }
//     ],
//     "requires": [ { "plugin": "s7noise", "version": "1.0.0", "elements": ["WhiteNoiseGenerator"] } ]  // only if plugin elements are used
//   },
//   "diagram": {
//     "grid": { "step": 20, "mm_per_step": 5.2917 },
//     "nodes": [
//       { "id": "const1", "position": [2, 4] },
//       { "id": "add1", "position": [16, 4], "title": "Sum" },
//       { "id": "delay1", "position": [30, 4] }
//     ],
//     "wires": [
//       { "id": "w1", "from": { "port": ["const1", "out", 0] }, "to": { "port": ["add1", "in", 0] }, "bends": [], "route": [[11, 6], [15, 6], [15, 12], [25, 12]] },
//       { "id": "w2", "from": { "port": ["delay1", "out", 0] }, "to": { "port": ["add1", "in", 1] },
//         "bends": [[41, 12], [14, 12]], "route": [[30, 6], [41, 6], [41, 12], [14, 12], [14, 20]] },
//       { "id": "w3", "from": { "port": ["add1", "out", 0] }, "to": { "port": ["delay1", "in", 0] }, "bends": [] }
//     ],
//     "text_frames": []
//   }
// }
//
// - `schema` is the runtime schema (semantics, element params); `diagram` is
//   layout only. Coordinates are whole grid steps.
// - A wire runs from an output port, or from a junction `at` a point of
//   another wire of the same net ("from": { "wire": "w1", "at": [x, y] }),
//   to an input port. `bends` are the bends placed by hand; optional `route`
//   points cache the current path to avoid rerouting on load;
//   missing or invalid cached routes are recomputed.
// - The connections the wires make must equal `schema.connections`. Without
//   `wires` (or without `diagram`) Studio wires and lays the schema out itself.

} // namespace s7studio::diagram
