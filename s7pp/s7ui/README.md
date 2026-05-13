# s7ui

`s7ui` is the Qt-based schema editor and local run control UI for S7.

## Current architecture view

- Qt6 desktop app with a dock-based main window
- QSchematic editor canvas as the central graph view
- Palette for runtime nodes and UI/visual-only elements
- Visual sink nodes render inline live graphs and open a dedicated detail window on double-click
- Every diagram element renders `type`, editable `name`, and `id` inside the node block
- Double-click on regular nodes opens a parameter popup with editable name/id and JSON params
- Model toolbar actions: `run`, `pause`, `resume`, `stop`, `reset` (local runtime service)
- Schema properties panel with editable schema `name` and sampling interval `dt`
- Remote node browser panel (stub)
- Runtime log panel
- Starter scene seeded with a visual sink connected to two source nodes

## Current file model

- `schema.diagram.json` is the editable authoring document.
- `schema.json` is the compiled runtime schema produced from the diagram.
- `visuals/*.json` describe reusable native appearance metadata for element types.
- Composite schema elements are stored as reusable first-class blocks and can be opened in nested editor windows.
- GUI-only elements such as `ScopeSink` / `ComplexScopeSink` remain part of the diagram document and do not need to exist in the runtime schema unless compiled intentionally.
- Unknown runtime elements fall back to a generic rectangular rendering so the editor remains usable.

### Diagram document responsibilities

- node and schema instances
- layout / position / size
- wire routing points
- groups / labels / annotations
- locks / layer metadata
- print / page metadata

### Compiled schema responsibilities

- executable runtime nodes
- runtime interfaces for composite schema instances
- typed connections used by the computational engine
- no visual layout or editor-only metadata

### Visual library responsibilities

- element shapes and icons
- port layout and styling
- display names and tooltips
- fallback rendering policy for unknown elements

## Minimal authoring shape

```json
{
  "id": "example",
  "dt": 0.001,
  "diagram": {
	"version": 1,
	"nodes": [
	  {
		"id": "src",
		"type": "Constant",
		"position": { "x": 0, "y": 0 },
		"input_ports": 0,
		"output_ports": 1,
		"ports": [
		  { "name": "out0", "direction": "output", "signal_type": "real", "domain": "stream" }
		],
		"params": { "value": 1.0 }
	  },
	  {
		"id": "scope_1",
		"type": "ScopeSink",
		"visual": true,
		"position": { "x": 240, "y": 0 },
		"input_ports": 1,
		"output_ports": 0,
		"ports": [
		  { "name": "in0", "direction": "input", "signal_type": "real", "domain": "stream" }
		],
		"params": { "inputs": 1 }
	  }
	],
	"nets": [],
	"wires": []
  }
}
```

## Editor rules

- Only `output -> input` connections are valid.
- Signal types must match exactly unless a converter element is used.
- Complex and real ports are not implicitly interchangeable.
- Composite schema elements may be edited in a nested window and compiled by flattening.
- Unknown elements render with the default visual fallback until a visual spec is added.

## Visual sink node behavior

- Visual sink nodes are resizable and have configurable input count.
- Default input count is `2`.
- Input count can be changed in the detail window opened via double-click.
- Sink preview updates live in-canvas; the detail window provides a larger graph and simple controls.

## Remote model behavior

- Remote models should carry both compiled schema data and diagram data.
- If a remote model lacks diagram data, the UI may infer a temporary layout, but the diagram remains the preferred representation.

## Draft JSON Schemas

Draft JSON Schema contracts live in `ai_impl_docs/json_schemas/`:

- `diagram-document.schema.json`
- `compiled-schema.schema.json`
- `visual-spec.schema.json`

See `ai_impl_docs/json_schemas/README.md` for the short index.

## Build notes

- Qt6 is required (target baseline: Qt 6.2+)
- Vendored `external/QSchematic` and `external/QCustomPlot` are used
- `s7ui` links to local `s7core` and `s7io` for in-process runtime evolution

## Quick try

```bash
cmake -S . -B tmp/s7ui-build -DS7_BUILD_UI=ON
cmake --build tmp/s7ui-build --target s7ui -j
./tmp/s7ui-build/s7ui/s7ui
```

If your build tree layout differs, search for the produced `s7ui` executable under `tmp/s7ui-build`.

