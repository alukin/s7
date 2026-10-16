# S7 — AI Collaboration Decision Register

> Status: initial register, 2026-10-16.
> **Only the project maintainer may approve architectural decisions.** An AI agent may propose or document a decision, but must not mark it accepted on its own.

## Status vocabulary

- **Accepted** — explicitly approved by the maintainer; binding within its documented scope.
- **Proposed** — under consideration; not permission to change semantics.
- **Superseded** — replaced by another recorded decision.
- **Rejected** — considered and not adopted.

This register complements existing architectural decision records in the repository; it does not override them. If an authoritative ADR already covers a subject, link it here rather than duplicating it.

## Accepted collaboration decisions

### AI-001 — Repository-based context sharing

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decision:** Use the S7 GitHub repository to exchange durable context, tasks, and findings among ChatGPT, local Codex, Claude, and development hosts.
- **Rationale:** Works across hosts, is tool-independent, and provides version history and reviewable changes.
- **Consequences:** No automatic sharing of chat transcripts or model session state. Material conclusions must be written to repository files or issues/PRs and synchronized via Git.

### AI-002 — One canonical agent instruction file

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decision:** Keep `AGENTS.md` as the canonical tool-neutral coding-agent instructions. Tool-specific files (such as `CLAUDE.md`) should reference it rather than duplicate rules.
- **Rationale:** Avoids divergent instructions between Codex, Claude, and future agents.
- **Consequences:** Durable architectural documentation remains separate from tool instructions; adapters may be required by individual tools.

### AI-003 — Separate context, decisions, and task reports

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decision:** Use `doc/ai/CONTEXT.md` for shared orientation, this register for approved decisions, and separate task handoffs/reports for short-lived work.
- **Rationale:** Prevents speculative chat ideas from becoming accidental requirements and avoids overloading `AGENTS.md`.
- **Consequences:** Each AI contribution should clearly identify whether it is a proposal, finding, implementation, or verified result.

## Architectural topics requiring confirmation

The following are **questions**, not accepted decisions. Refer to existing S7 formal and architectural documents for already established rules.

| Topic | Question / decision needed | Status |
| --- | --- | --- |
| Causal self-loops | Is the formal cyclic-SCC wording sufficient, and does validation implement the intended rule? | Investigation |
| Evaluation semantics | What are the exact publication and state-commit boundaries for all element classes? | Verify existing contract |
| Distributed activation | What is guaranteed when some nodes activate a revision and others fail? | Verify/specify |
| Stream recovery | What behavior is guaranteed across reconnect, process restart, and data loss? | Verify existing contract |
| Coherence domains | How are sample alignment and phase comparability represented and tested? | Proposed design work |
| Numerical acceptance | Which reference problems, tolerances, and invariants are required for a release? | Proposed validation work |

## Template for new decisions

Copy this section when the maintainer explicitly decides an issue:

```markdown
### AI-NNN — Short descriptive title

- **Status:** Proposed | Accepted | Rejected | Superseded
- **Date:** YYYY-MM-DD
- **Owner/approver:** Maintainer
- **Context:** What problem or conflict prompted this decision?
- **Decision:** What exactly is required or forbidden?
- **Alternatives considered:** Briefly list meaningful alternatives.
- **Rationale:** Why this option?
- **Consequences:** Compatibility, implementation, testing, documentation.
- **References:** Issue / PR / commit / authoritative spec.
```

## Change-control rule

Before changing causal semantics, state transitions, protocol compatibility, or distributed deployment guarantees, an agent must identify the relevant existing specification and request a maintainer decision if the change alters that contract. Passing tests alone does not authorize a semantic change.
