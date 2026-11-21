---
name: s7-task-handoff
description: Prepare concise S7 engineering task contracts for handoffs between Codex, Claude, reviewers and development sessions.
---

# S7 agent task handoffs

Use for substantial multi-file, multi-agent, or multi-session changes, not trivial edits.

1. Review AGENTS.md and the relevant current implementation and specification. Separate implemented behavior, planned work and unresolved decisions.
2. Create a concise handoff with python3 tools/ai/task_handoff.py init --id ... --goal ... --path ... --invariant ... --check ... --output <outside-Git.json>. Include baseline SHA, affected components, stable invariants and independently verifiable acceptance steps.
3. Keep the handoff scoped to the task. Include source references, verifiable acceptance checks and known blockers. Capture the baseline before work begins because `init` records the current checkout's HEAD.
4. Before marking the task accepted, verify every required check against the delivered result and record the executed commands and evidence references. `task_handoff.py check` validates record structure and declared results. Review the actual logs or artifacts to establish coverage and outcomes.
5. Use `task_handoff.py show` when transferring the task. Include open checks and blockers the next contributor needs to pick up.

The handoff document is a coordination artifact and does not grant permission to operate physical hardware, merge or publish.
