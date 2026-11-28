---
name: s7-diagram-drawing
description: Create a new runnable S7 Studio .s7diagram with correct causal wiring and readable block layout.
---

# Draw a new S7 diagram

Use for **new** saved diagrams. For changes to an existing diagram use `s7-diagram-editing`; for render/GUI checks use `s7-visual-validation`.

1. Read repository `AGENTS.md`, `doc/S7_Formal_Model.md`, `s7l2/docs/STUDIO_DIAGRAM_EDITOR.md`, and `s7l2/s7studio/src/diagram_format.hpp`. Inspect the element catalog, a similar checked-in diagram, and its tests. Do not guess element types, ports, parameters, or implemented features.
2. Identify the intended element types, indexed ports, parameters, initial state, rates, connections and expected behavior. For feedback, identify the explicit causal memory. For reusable plants or controllers, check `s7l2/s7core/resources/schema_elements.s7library` before introducing new implementations.
3. Use the current document format (`document_kind: s7.diagram`; it is not versioned before 0.2.0, and `s7l2/tools/update_diagrams.py --write` updates saved diagrams when it changes). Put execution data in `schema` and layout in `diagram`. Start from the schema and use Studio normalization to generate the layout when practical.
4. Arrange functional flow left to right, reserve space for feedback and fan-out, keep readable text/symbols and visible observer branches. Keep positions/sizes on the documented grid. Avoid overlaps and accidental wire crossings.
5. For explicit wire trees, use one trunk per output and junctions on the same net for fan-out. A crossing leaves wires independent. The wire trees must match `schema.connections`. Omit cached `route` unless preserving a known route is useful. Store deliberate bends in `bends`.
6. Use `s7studio --normalize <input> <separate-output>` with the local Studio build. Use `--layout-check <schema>` when testing schema auto-layout. Inspect the actual saved diagram using `s7-visual-validation` when rendering is in scope. Execute runnable examples against their expected behavior and use an independent reference for numerical claims.
7. Deliver the editable `.s7diagram` and the run instructions needed to reproduce its behavior.

Report changed paths, model assumptions, exact executed commands, numeric checks, visual evidence, and any unresolved collision or missing-plugin issue. Never claim a diagram is GUI-qualified without executing GUI checks.
