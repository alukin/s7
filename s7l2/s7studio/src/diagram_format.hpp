#pragma once

namespace s7studio::diagram {

// Diagram file format specification
//
// Diagram documents extend runtime schemas with visual layout metadata.
// The root structure contains:
//   - document_kind: "s7.diagram" (identifies as diagram file)
//   - diagram_magic: "S7STUDIO_DIAGRAM_V1" (version marker)
//   - id, dt, schema_version, diagram_version: metadata
//   - sheet: page layout (A4/A3, orientation, dimensions)
//   - schema: embedded runtime schema (elements + connections)
//   - diagram: visual layout (nodes with positions, wires with routing points)
//
// Example diagram JSON:
// {
//   "document_kind": "s7.diagram",
//   "diagram_magic": "S7STUDIO_DIAGRAM_V1",
//   "id": "feedback_loop_demo",
//   "dt": 0.001,
//   "schema_version": 1,
//   "diagram_version": 1,
//   "units": "mm",
//   "sheet": {
//     "orientation": "landscape",
//     "width_mm": 297.0,
//     "height_mm": 210.0
//   },
//   "schema": {
//     "name": "feedback_loop_demo",
//     "elements": [
//       { "id": "const1", "type": "Constant", "params": { "value": 2.0 } },
//       { "id": "add1", "type": "Adder" },
//       { "id": "delay1", "type": "Delay", "params": { "initial_value": 1.0 } }
//     ],
//     "connections": [
//       { "from_element": "const1", "from_port": 0, "to_element": "add1", "to_port": 0 },
//       { "from_element": "delay1", "from_port": 0, "to_element": "add1", "to_port": 1 },
//       { "from_element": "add1", "from_port": 0, "to_element": "delay1", "to_port": 0 }
//     ]
//   },
//   "diagram": {
//     "version": 1,
//     "nodes": [
//       {
//         "id": "const1",
//         "type": "Constant",
//         "name": "Constant 2.0",
//         "position": { "x": 50.0, "y": 100.0 }
//       },
//       {
//         "id": "add1",
//         "type": "Adder",
//         "name": "Adder",
//         "position": { "x": 150.0, "y": 100.0 }
//       },
//       {
//         "id": "delay1",
//         "type": "Delay",
//         "name": "Delay 1s",
//         "position": { "x": 250.0, "y": 100.0 }
//       }
//     ],
//     "wires": [
//       {
//         "id": "wire_0",
//         "points": [
//           { "x": 100.0, "y": 100.0 },
//           { "x": 150.0, "y": 100.0 }
//         ]
//       },
//       {
//         "id": "wire_1",
//         "points": [
//           { "x": 200.0, "y": 100.0 },
//           { "x": 250.0, "y": 100.0 }
//         ]
//       }
//     ]
//   }
// }
//
// Format Notes:
// - Schema is the s7l2 runtime format: elements array + connections array
// - Diagram.nodes: visual representation (positions, names, custom properties)
// - Diagram.wires: routing info (list of absolute scene-coordinate points for wire paths)
// - Positions are in absolute scene coordinates (mm)
// - Wire points are stored in absolute scene coordinates so moved wires reload with stable endpoint attachments
// - Type names match s7::Element types: Constant, Adder, Multiply, Delay, Integrator, etc.
// - Parameters are element-type-specific (stored in schema, not diagram)

} // namespace s7studio::diagram
