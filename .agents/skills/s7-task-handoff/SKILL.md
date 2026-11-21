---
name: s7-task-handoff
description: Prepare concise S7 engineering task contracts for handoffs between Codex, Claude, reviewers and development sessions.
---

# S7 agent task handoffs

Use for substantial multi-file, multi-agent, or multi-session changes, not trivial edits.

1. Review AGENTS.md and the relevant current implementation and specification. Separate implemented behavior, planned work and unresolved decisions.
2. Create a concise handoff with python3 tools/ai/task_handoff.py init --id ... --goal ... --path ... --invariant ... --check ... --output <outside-Git.json>. Include baseline SHA, affected components, stable invariants and independently verifiable acceptance steps.
3. Inspect and refine the generated JSON rather than pasting long conversation history. Keep source documents as file references and record only task-specific constraints, assumptions and known obstacles.
4. Before accepting the handoff, execute required checks and attach actual command/results evidence. Set status to accepted only when tests really passed; validate using task_handoff.py check.
5. Use task_handoff.py show when transferring to a new agent. Review agent conclusions against evidence; do not let an agent silently redefine causal semantics or protocol guarantees.

The handoff document is a coordination artifact and does not grant permission to operate physical hardware, merge or publish.
