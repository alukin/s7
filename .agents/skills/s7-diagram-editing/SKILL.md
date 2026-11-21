---
name: s7-diagram-editing
description: Safely edit an existing S7 .s7diagram including elements, wiring, layout, annotations, nested schemas and undo behavior.
---

# Edit an existing S7 diagram

Use for **modifying** a saved diagram, not creating a new one or merely reviewing a screenshot.

1. Read `AGENTS.md`, `s7l2/docs/STUDIO_DIAGRAM_EDITOR.md`, and the original `.s7diagram`. Inspect the actual element and connection declarations, nested schemas, observers, plugin requirements, and current geometry. Check `git status`; preserve unrelated edits.
2. For a layout-only edit, preserve the exact original file or Git revision as a baseline. Identify the runtime schema, probes and run settings the edit must preserve.
3. Keep the edit local and preserve unaffected IDs and routes. Use integral grid positions. Keep explicit wire trees consistent with `schema.connections`, including junctions for fan-out.
4. For changes to ports, parameters or nested interfaces, update the corresponding wires and metadata. Check explicit causal memory, initial state, rates and required plugins where the edit affects them.
5. Preserve user-placed `bends`. Studio can recompute cached `route` coordinates. Normalize to a separate file for verification so unrelated serialized changes stay outside the requested edit.
6. Verify the affected behavior. Open or normalize the saved file for load checks. For editor implementation changes, use relevant CTest or Studio cases for undo, attachment, or save/reopen. `s7studio --edit-check <diagram>` exercises a specific add-element, undo and reload workflow. Use `s7-visual-validation` for appearance checks.
7. For a layout-only change, run `validate_diagram.py --layout-only` with the original via `--baseline` or `--base-ref`. The projection excludes top-level `diagram` and `sheet` and recognizes optional wire trees. Observer nodes, settings and wire branches can live under `diagram`. Compare those separately, along with other presentation fields the task should preserve. Check the saved file in Studio when required. For functional edits, test the intended behavior.

Report which parts changed, unchanged semantics or deliberate differences, exact tests executed, and remaining issues. Do not overwrite user modifications or mask broken wires by deleting them.
