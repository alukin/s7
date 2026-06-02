# S7 UI Diagram Authoring Specification v1

**Status:** Active (Draft)  
**Purpose:** Diagram format and editor authoring semantics  
**Last Updated:** May 13, 2026

---

## 1. Purpose

This specification defines the authoring model for S7 graphical design in the UI.

The system has three distinct layers:

1. **Diagram document** — the editable visual document.
2. **Visual library** — reusable appearance and layout metadata for element types.
3. **Compiled schema** — the minimal executable graph consumed by the runtime engine.

This separation is intentional and strict.

- The diagram document is what the user edits.
- The visual library defines how nodes, ports, decorations, labels, and preview regions look and are placed.
- The compiled schema is generated from the diagram and must not contain visual layout data.

The project is still in early development, so this document intentionally does **not** preserve legacy file formats or migration rules. We are free to change the shape of the data while the architecture is being finalized.

---

## 2. File model

### 2.1 Diagram document

Suggested filename:

- `schema.diagram.json`

This file is the source of truth for the editor.
It contains:

- node instances
- schema instances
- GUI-only elements
- layout / position / size
- wire routing points
- grouping / labels / annotations
- lock state
- page / print metadata

In v1, the diagram document is also the home of composite schema definitions and user library schema bodies.

### 2.2 Compiled schema

Suggested filename:

- `schema.json`

This file is generated from the diagram document.
It contains only executable graph data.
It must not contain:

- positions
- wire control points
- print metadata
- editor-only annotations
- view state
- visual style data

The compiled schema is what the runtime consumes. It should be flat, deterministic, and free of editor concerns.

### 2.3 Visual library

Suggested directory:

- `visuals/`

This directory contains reusable appearance descriptors.
Examples:

- `visuals/default.json`
- `visuals/scope.json`
- `visuals/converters.json`
- `visuals/io.json`

The visual library is not a runtime schema and not a diagram instance.
It is a diagram-time visual catalog.

Visual entries describe a whole shape as a collection of sub-visuals in local millimeter coordinates, for example:

- overall `size`
- body / frame shape
- preview area / plot region
- label region / text placement
- connector decoration positions

Those values are editor-facing and must be compiled away when producing `schema.json` for the runtime.

---

## 3. Element kinds

An element type can be one of the following:

### 3.1 Primitive runtime element

A leaf node that compiles directly into the runtime graph.
Examples:

- constant
- adder
- filter
- source
- sink
- converter

### 3.2 Visual-only element

A diagram element that exists only in the editor document.
Examples:

- labels
- comment boxes
- grouping rectangles
- page markers
- rulers / guide overlays

These do not compile into `schema.json`.

### 3.3 Composite schema element

A reusable sub-schema treated as a single element from the outside.
It contains an internal diagram and exposes a formal interface.

Examples:

- custom signal chain
- reusable subsystem
- user library schema
- submodule graph

This kind of element is first-class.
It can be placed inside another diagram and double-clicked to open a nested editor.

---

## 4. Schema element contract

A schema element is a composite element with these properties:

- stable `id`
- display `name`
- type `schema`
- source reference or embedded internal diagram
- external port interface
- optional parameters
- optional description
- optional visual summary

### 4.1 Required fields

A schema element instance must define:

- `id`
- `type`
- `name`
- `interface`
- `body`

Where:

- `interface` describes exposed ports.
- `body` references or contains the internal diagram.

### 4.2 Interface

The interface is the public contract of the schema element.
It includes:

- input ports
- output ports
- port names
- port types
- port domains
- optional default values

The external user connects to the interface only.
The internal implementation remains hidden.

### 4.3 Body

The body is the internal graph of the schema.
It may contain:

- primitive nodes
- visual-only elements
- nested schema elements
- wires
- annotations

The body may be stored:

- inline inside the diagram document, or
- in a separate file referenced by the diagram

For v1, schema bodies are allowed to be inline or referenced, but the editor should treat the schema itself as a first-class diagram artifact that can be opened directly in a nested editor window.

---

## 5. Diagram node contract

Each node instance in `schema.diagram.json` must contain at least:

- `id`
- `type`
- `position`
- `ports` or explicit port information
- `visual`
- `locked` state if applicable

Optional fields:

- `name`
- `description`
- `size`
- `icon`
- `shape`
- `color`
- `tags`
- `collapsed`
- `layer`
- `metadata`

### 5.1 Positions

Positions belong to the diagram document only.
They are never compiled into runtime schema.

The diagram document is the canonical place to store element placement, wire routing, grouping, and locking.

### 5.2 Sizes

Sizes belong to the diagram document only.
They may be used for print/layout/rendering.

### 5.4 Visual library geometry

Visual library records may additionally define the geometry of a reusable appearance in the same mm-based local coordinate system:

- `size` — the full bounding box of the visual composition.
- `preview_area` — a sub-rectangle reserved for oscilloscope-style plots or other embedded content.
- future fields may describe label zones, icon zones, or connector decoration zones.

The key rule is that a visual entry describes the full composite look of the element, not just a single shape primitive.

### 5.3 Labels

Labels belong to the diagram document.
They may be shown in node headers, tooltips, side panels, or print layout.

---

## 6. Port contract

Ports are typed connection points.
They are part of both validation and rendering.

### 6.1 Port fields

Each port should declare:

- `name`
- `direction` (`input` or `output`)
- `signal_type`
- `domain`
- `max_connections`
- `label`
- `color`
- `locked` if needed

### 6.2 Signal types

Initial signal types may include:

- `real`
- `complex`
- `bool`
- `int`
- `float`
- `any`

### 6.3 Domain

Initial domains may include:

- `stream`
- `message`
- `control`

### 6.4 Validation rules

- output may only connect to input
- input may only accept the allowed number of connections
- signal types must match exactly unless an explicit converter is used
- domains must match exactly unless a supported bridge exists
- port names must resolve unambiguously

### 6.5 Converter policy

Cross-type or cross-domain conversion is explicit only.
Examples:

- `real -> complex`
- `complex -> real`
- `stream -> message`

These require dedicated converter elements.

---

## 7. Visual library contract

The visual library describes how an element should appear.
It is separate from the runtime graph and separate from the diagram instance.

### 7.1 Visual spec fields

A visual spec may define:

- `type`
- `display_name`
- `category`
- `shape`
- `icon`
- `size`
- `palette_icon`
- `node_style`
- `port_style`
- `label_style`
- `description`
- `tags`
- `capabilities`
- `fallback_behavior`

### 7.2 Shape values

Suggested initial shapes:

- `rect`
- `round_rect`
- `hex`
- `diamond`
- `custom`

### 7.3 Fallback rule

If the editor does not know the element type, it must render it using a safe default:

- rectangular node
- generic ports
- plain title text
- neutral colors
- no special icon

This keeps the editor usable even when runtime introduces unknown elements.

Known elements should prefer their native visual spec, but unknown elements must never block editing.

### 7.4 Known elements

If the visual library has a native spec for the element, the editor should use that shape and style.
Examples:

- scope sink should use a scope-like visual
- converter should use a converter-like shape
- schema instance should use a composite badge or grouped style

---

## 8. Diagram-only annotations

The diagram may contain elements that never compile into runtime schema.
Examples:

- comments
- section headers
- groups
- frames
- page marks
- lock overlays
- print headers

These elements may still be selectable, movable, lockable, and printable.

---

## 9. Locking rules

The editor should support explicit locking.

Lockable properties:

- position
- size
- ports
- connections
- visual appearance
- group membership
- annotation content
- whole sub-schema

A locked item may still be visible and printable.
It just cannot be modified in the locked dimension.

---

## 10. Hierarchical schemas

A schema may contain other schemas.
This is a first-class feature.

### 10.1 Double-click behavior

Double-clicking a schema element must open a nested editor window for that sub-schema.

### 10.2 Compilation behavior

Composite schemas compile by flattening unless an engine-native composite runtime is explicitly supported.

Recommended v1 approach:

- flatten the internal graph recursively
- map exposed ports to internal boundary ports
- emit only primitive runtime nodes in `schema.json`

This means a schema is both:

- an editable diagram element in the authoring environment, and
- a composable runtime unit that can be expanded during compilation.

### 10.3 Library behavior

User-defined schemas may be stored in a library and reused like any other element.
They should appear in the palette with a visual identity.

---

## 11. Compilation pipeline

The compiler converts `schema.diagram.json` to `schema.json`.

### 11.1 Steps

1. Load diagram document.
2. Resolve visual library entries.
3. Validate node ids and port references.
4. Validate connections.
5. Resolve schema instances recursively.
6. Flatten composite schemas.
7. Strip visual-only elements.
8. Emit runtime schema.
9. Produce diagnostics.

### 11.2 Diagnostics

The compiler should emit:

- errors for invalid graphs
- warnings for unknown visual types
- warnings for orphan visual elements
- warnings for missing non-critical metadata

Compile must fail on structural errors.

---

## 12. Remote model packaging

Remote models should transport both the schema and the diagram.

Recommended package contents:

- compiled schema
- diagram document
- visual library references or snapshot
- version and hash metadata

If a remote model lacks diagram data, the editor may still open the compiled schema using temporary auto-layout, but it should mark the diagram as inferred.

---

## 13. Versioning

Use explicit version fields.

Suggested fields:

- `diagram_version`
- `schema_version`
- `visual_version`

The project is in initial development, so backward compatibility is not required yet.
That means these versions may change freely as the spec evolves.

---

## 14. Minimal v1 design rule summary

- Diagram is editable source.
- Schema is compiled runtime artifact.
- Visual library defines native appearance.
- Unknown elements use a safe fallback rectangle.
- Schema elements can be nested and opened in their own editor.
- Cross-type connections require explicit converter blocks.
- Connection validation must be strict.
- Compile must flatten composite schemas into a runtime graph.

---

## 15. Open questions for the next draft

1. Do we store composite schema bodies inline or as separate files?
2. What is the exact canonical runtime edge format: `nets` or `wires`?
3. Which visual fields should be mandatory in v1?
4. Which port types do we support in the first release?
5. How should the palette expose user library schemas?

---

## 16. Proposed next step

Drafted JSON Schema files live in `ai_impl_docs/json_schemas/`:

- `diagram-document.schema.json` — validates `schema.diagram.json`
- `compiled-schema.schema.json` — validates `schema.json`
- `visual-spec.schema.json` — validates `visuals/*.json`

Then implement the compiler and live editor validation against that contract.

