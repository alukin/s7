---
name: s7-visual-validation
description: Visually validate actual S7 Studio diagrams, wire routing, viewers, screenshots and PDF/print output.
---

# Validate S7 Studio visuals

Use for diagram appearance, screenshot reviews, visual regression, GUI interactions, viewer/output plots, and print/PDF acceptance. This is **not** a substitute for numerical verification.

1. Read `AGENTS.md`, `s7l2/docs/STUDIO_GUI_TESTING.md`, `s7l2/docs/STUDIO_DIAGRAM_EDITOR.md` and relevant viewer docs. For printing, read `s7l2/docs/STUDIO_PRINTING_0.2.0.md`. Examine the exact diagram and intended workflow first.
2. For a saved diagram, use `tools/ai/validate_diagram.py` for static top-level document and explicit wire-tree checks. Use the layout-only baseline comparison only for edits intended to preserve non-presentation fields.
3. For a saved diagram image, use the validator with `--studio <exe> --capture --artifacts <fresh-output-dir>`. For interactive workflows or viewers, choose the relevant local target and procedure in `s7l2/docs/STUDIO_GUI_TESTING.md`. For a headless 2D capture, set `QT_QPA_PLATFORM=offscreen` explicitly if an inherited display is unusable. Use the local GUI workflow for live widgets.
4. Run named Qt Test cases with `s7l2/tools/run_gui_test.py` from `s7l2/` as shown in the GUI testing guide. The runner uses `--capture` for timeout and failure screenshots. For visuals from successful runs, select a case that saves the needed images or use the documented screenshot workflow. Check the exit status and skip record. Exit 77 records unavailable Xvfb or, for the dedicated graphics probe, an unavailable OpenGL context.
5. Open the actual captured images and inspect the properties the task affects, such as legible labels, attachment, junctions, clipping or viewer data. Choose scale, theme and window-size variants when the requested acceptance criteria call for them.
6. For changes to editor behavior, exercise the affected gestures and state transitions. For viewer changes, check the relevant channels, axes and interactions and verify trace data separately when numerical accuracy is in scope.
7. When printing is in scope, follow the relevant print contract and inspect actual PDF pages for content, page bounds and legibility. For release qualification, also account for any physical-printer checks the release gate requires.

Produce evidence paths, environment/commit SHA, commands, logs and captured screenshots with concise findings (pass/fail/skipped). Never claim a screenshot was visually inspected if the pixels were not actually opened.
