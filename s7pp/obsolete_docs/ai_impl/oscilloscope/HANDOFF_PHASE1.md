# Oscilloscope Phase 1 Handoff (Historical Snapshot)

This file is preserved as historical context from an earlier milestone.
It is not the current source of truth for the repository state.

## Current Source of Truth

- `s7ui/src/oscilloscope_widget.hpp`
- `s7ui/src/oscilloscope_widget.cpp`
- `s7ui/OSCILLOSCOPE_WIDGET.md`
- `s7ui/INTEGRATION_GUIDE.md`
- `ai_impl_docs/S7_OSCILLOSCOPE_PROBE_DESIGN.md`
- `ai_impl_docs/OSCILLOSCOPE_ARCHITECTURE_DIAGRAMS.md`
- `ai_impl_docs/PROBE_OSCILLOSCOPE_VALIDATION.md`

## What Remains Useful From Phase 1

- Design intent: probe-backed visualization in UI.
- The conceptual mapping between `Probe`/`ComplexProbe` and oscilloscope traces.
- The principle of periodic UI polling from the GUI thread.

## What Is Historical Only

- Fixed line counts, timeline estimates, and milestone claims.
- Any references to paths such as `/docs/...` or `/DELIVERY_SUMMARY.md`.
- Old compile snapshots and one-time handoff checklists.

## Maintenance Guidance

When updating oscilloscope docs:

1. Update `ai_impl_docs/DOCUMENT_INDEX.md` first.
2. Keep links repository-relative.
3. Validate examples against current APIs in `s7ui/src`.

Last updated: May 13, 2026.
