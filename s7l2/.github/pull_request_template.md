## Summary

- Describe what changed and why.

## Architecture Check

- [ ] I verified this change against `docs/S7_Architecture_Overview.md`.
- [ ] I verified this change against `docs/S7_Formal_Model.md`.
- [ ] I verified this change against `docs/S7_Runtime_Architecture.md`.
- [ ] If implementation and docs differ, I updated implementation or opened an explicit ADR/doc decision.

## Invariant Check

- [ ] Connections remain the source of causality.
- [ ] `step()` semantics are preserved for affected elements/runtime paths.
- [ ] Cycles with no causal memory are rejected.
- [ ] Observation remains read-only (if touched).
- [ ] Deployment concerns do not alter computational semantics (if touched).

## Human Simplicity For Element Development

- [ ] New/changed element APIs are simple for humans (minimal boilerplate, clear errors, predictable usage).
- [ ] Any added complexity is justified in docs or PR notes.

## Validation

- [ ] I ran local build and tests.
- [ ] I added/updated tests for semantic behavior changes.

