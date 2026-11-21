---
name: s7-visual-validation
description: Visually validate actual S7 Studio diagrams, wire routing, viewers, screenshots and PDF/print output.
---

# Validate S7 Studio visuals

Use for diagram appearance, screenshot reviews, visual regression, GUI interactions, viewer/output plots, and print/PDF acceptance. This is **not** a substitute for numerical verification.

1. Read `AGENTS.md`, `s7l2/docs/STUDIO_GUI_TESTING.md`, `s7l2/docs/STUDIO_DIAGRAM_EDITOR.md` and relevant viewer docs. For printing, read `s7l2/docs/STUDIO_PRINTING_0.2.0.md`. Examine the exact diagram and intended workflow first.
2. Before rendering a changed saved diagram, run `python3 tools/ai/validate_diagram.py --diagram <file>` to verify functional connections; for layout-only edits also compare against the baseline with `--base-ref <ref> --layout-only`. The structural tool never replaces Studio's real rendering or pixel inspection.
3. Use a real Studio render. The repository supplies `s7l2/tools/run_gui_test.py` (isolated Xvfb, logs, JUnit, capture), the `s7studio_gui_workflows_test` and `s7studio_gui_capture` targets, and `s7studio_diagram_test`. Run only focused tests first; use the broader `ctest --test-dir <build> -L gui --output-on-failure` when scope warrants. The build directory and target availability must be verified locally.
4. Example targeted capture workflow, from `s7l2/`: `python3 tools/run_gui_test.py --artifacts /tmp/s7-visual-check --capture build-release/s7studio/s7studio_gui_capture --graphics --screen-size 1600x1000 --timeout 240 -- build-release/s7studio/s7studio_gui_workflows_test <selected-Qt-test-case>`. Discover test-case names from the local binary/documentation; do not invent them. `--graphics` may skip when OpenGL is unavailable; report this as unverified, not passed.
5. Inspect **actual captured pixels**, not only logs: readable symbols, annotations and labels; port-to-wire alignment; no collision with elements or edge-hugging wires; correct wire-junction dots; distinguish crossings from connections; clipping, zoom, contrast, and font legibility. Exercise representative normal/fit-page/fit-diagram scales and, if relevant, light/dark and large/small windows.
6. For editing workflows test placement/move/resize, interactive routing, fan-out, undo/redo, save/reopen, and nested diagram traversal when affected. Check the Problems pane and confirm routing changes don't alter functional connections.
7. For Scope, live oscilloscope, FFT/STFT, 2D/3D viewers check visible plotted data, correct channel selection, axis units/ticks, legends, finite ranges, scale, interaction, and nonblank rendering. Verify runtime traces separately; a matching picture never proves numerical accuracy. Qt Quick 3D may fail under offscreen rendering: require a real supported graphics session or explicitly mark that check incomplete.
8. When printing, test document PDF as well as display captures: paper size/orientation, nonblank pages, content bounds, legends, vector/text readability or documented raster fallback, and snapshot consistency. A PNG is not evidence that PDF/print works.

Produce evidence paths, environment/commit SHA, commands, logs and captured screenshots with concise findings (pass/fail/skipped). Never claim a screenshot was visually inspected if the pixels were not actually opened.
