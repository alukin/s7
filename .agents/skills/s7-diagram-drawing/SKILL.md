---
name: s7-diagram-drawing
description: Create a new runnable S7 Studio .s7diagram with correct causal wiring and readable block layout.
---

# Draw a new S7 diagram

Use for **new** saved diagrams. For changes to an existing diagram use `s7-diagram-editing`; for render/GUI checks use `s7-visual-validation`.

1. Read repository `AGENTS.md`, `doc/S7_Formal_Model.md`, `s7l2/docs/STUDIO_DIAGRAM_EDITOR.md`, and `s7l2/s7studio/src/diagram_format.hpp`. Inspect the element catalog, a similar checked-in diagram, and its tests. Do not guess element types, ports, parameters, or implemented features.
2. Write the intended model first: element IDs and types, indexed input/output ports, parameters, initial states, sample rate, units, directed connections, and an expected reference behavior. State which elements provide causal memory. Observers and text frames are not functional graph elements.
3. Construct the version-2 document (`document_kind: s7.diagram`, `diagram_version: 2`). Keep executable semantics in `schema` and visual geometry in `diagram`. Prefer existing project generators/Studio serializer and the built-in automatic layout over hand-calculating wire routes.
4. Arrange functional flow left to right, reserve space for feedback and fan-out, keep readable text/symbols and visible observer branches. Keep positions/sizes on the documented grid. Avoid overlaps and accidental wire crossings.
5. If supplying explicit wires, obey the actual wire-tree model: one output driver per net; trunk to an input; additional sinks branch from a junction on the same net. Crossings alone do not connect. Keep `schema.connections` exactly consistent with the wire-derived connections. Prefer omitting cached `route` and unnecessary bends so Studio can compute them.
6. Normalize using `s7l2/build/s7studio/s7studio --normalize <input> <output>` (adjust build path to the actual workspace) and review the result. Where relevant run `--layout-check`, `s7studio_diagram_test`, targeted model tests, and a headless execution/independent numerical reference. Use `s7-visual-validation` to verify an actual rendering.
7. Save an original editable `.s7diagram` and a short explanation of the mathematical model and how to run it. Do not substitute a PNG/SVG illustration for the runnable diagram.

Report changed paths, model assumptions, exact executed commands, numeric checks, visual evidence, and any unresolved collision or missing-plugin issue. Never claim a diagram is GUI-qualified without executing GUI checks.
