# Draft JSON Schemas

This folder contains draft JSON Schema files for the current S7 UI architecture.

## Files

- `diagram-document.schema.json` — validates the editable diagram document (`schema.diagram.json`)
- `compiled-schema.schema.json` — validates the runtime artifact (`schema.json`)
- `visual-spec.schema.json` — validates reusable visual library entries (`visuals/*.json`)

## Notes

- These schemas reflect the current view of the system: diagram-first authoring, compiled runtime output, and separate visual metadata.
- There is intentionally no legacy compatibility or migration logic here.
- The schemas are draft contracts and can still evolve as the architecture settles.

