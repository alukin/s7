---
name: s7-diagram-editing
description: Safely edit an existing S7 .s7diagram including elements, wiring, layout, annotations, nested schemas and undo behavior.
---

# Edit an existing S7 diagram

Use for **modifying** a saved diagram, not creating a new one or merely reviewing a screenshot.

1. Read `AGENTS.md`, `s7l2/docs/STUDIO_DIAGRAM_EDITOR.md`, and the original `.s7diagram`. Inspect the actual element and connection declarations, nested schemas, observers, plugin requirements, and current geometry. Check `git status`; preserve unrelated edits.
2. Before changing anything, record a **semantic baseline**: `schema` elements, parameters, connection endpoints, time/rate configuration, plus the diagram's visible nodes/wires/annotations and an existing headless or GUI test if available. Distinguish a layout-only edit from an intentional functional-model change.
3. Make the minimum change. Maintain stable element and wire IDs where possible; do not regenerate entire diagrams just to move one element. Keep schema connections equal to those represented by wire trees; a branch is a junction on a wire, not a visually touching crossing. Keep grid positions integral and port indexes valid.
4. If changing element port counts, nested interfaces or parameters, update affected wires and metadata deliberately. Ensure explicit state and valid feedback, preserve observer-only semantics, and do not silently reset initial conditions, model rate, or required plugins.
5. Treat cached `route` coordinates as expendable routing hints, not authoritative semantics. Use Studio's serializer/`--normalize` and routing rather than inventing arbitrary orthogonal segments. Preserve unaffected routes and user-placed bends where practical.
6. Verify **before/after**: parse/load and normalize; `s7studio_diagram_test` or `s7studio --edit-check <diagram>` where applicable; targeted model and GUI tests; Save/Reopen; Undo/Redo for interactive changes; nested editing and wire attachment where relevant. For visual edits invoke `s7-visual-validation`.
7. For layout-only changes compare functional `schema` before and after, ignoring only clearly documented serialization-only differences. For semantic edits show intended numerical or protocol impact. Don't assert a visually plausible circuit is equivalent without checking.

Report which parts changed, unchanged semantics or deliberate differences, exact tests executed, and remaining issues. Do not overwrite user modifications or mask broken wires by deleting them.
