# s7ui (skeleton)

`s7ui` is the initial control/compute/visualize desktop shell for S7.

## Current skeleton

- Qt6 desktop app with a dock-based main window
- QSchematic editor canvas as the central graph view
- Palette for S7 compute/IO nodes plus UI visual sink nodes
- Visual sinks are schema elements (connectable nodes) with inline low-resolution live graphs
- Double-click on a visual sink opens a dedicated detail window with controls (knobs + input count)
- Every schema element renders `type`, editable `name`, and `id` inside the node block
- Double-click on regular nodes opens a parameter popup with editable name/id and JSON params
- Model toolbar actions: `run`, `pause`, `resume`, `stop`, `reset` (local runtime service)
- Schema properties panel with editable schema `name` and sampling interval `dt`
- Remote node browser panel (stub)
- Runtime log panel
- Starter scene seeded with a visual sink connected to two source nodes

## Visual sink node behavior

- Visual sink nodes are resizable and have configurable input count.
- Default input count is `2`.
- Input count can be changed in the detail window opened via double-click.
- Sink preview updates live in-canvas; the detail window provides a larger graph and simple controls.

## Build notes

- Qt6 is required (target baseline: Qt 6.2+)
- In this skeleton, vendored `external/QSchematic` and `external/QCustomPlot` are used
- `s7ui` links to local `s7core` and `s7io` for in-process runtime evolution

## Quick try

```bash
cmake -S . -B tmp/s7ui-build -DS7_BUILD_UI=ON
cmake --build tmp/s7ui-build --target s7ui -j
./tmp/s7ui-build/s7ui/s7ui
```

If your build tree layout differs, search for the produced `s7ui` executable under `tmp/s7ui-build`.

