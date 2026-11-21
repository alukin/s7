---
name: s7-independent-review
description: Independently review high-risk S7 causal, numerical, protocol and concurrency changes against contracts and evidence.
---

# Independent review of high-risk S7 changes

Use for changes affecting causality, numerical methods, wire protocols, state ownership, concurrency, security or release guarantees. Do not spend multiple-agent review effort on routine formatting or minor prose changes.

1. Read AGENTS.md and the authoritative affected formal-model, algorithm, protocol or ABI specification before looking at the implementer's justification.
2. Review the actual diff and neighboring implementation. Separate **confirmed bug**, **contract ambiguity**, **missing test**, and **optional improvement**. Cite precise file and line references; do not infer defects merely from AI-generated prose.
3. Independently derive one or more reference expectations: state-publication steps, numerical recurrence/convergence, protocol bytes and errors, ordering/failure properties, or a regression reproducer. Avoid copying the implementer's test oracle.
4. Check negative cases, backwards compatibility, security boundaries, cancellation/ownership and changes under high load where relevant. Prefer small concrete counterexamples over large hypothetical rewrites.
5. Request actual test evidence, build configuration, commit SHA and platform. A green unrelated CI result, screenshot without pixel inspection, or local simulation without independent math does not substantiate a claim.
6. Produce a concise prioritized review with severity, observation, reproducible evidence, fix criteria and which findings remain uncertain. Ask the author to fix actionable issues and rerun targeted checks; verify the final revision, not only an earlier PR commit.

Independent review can be performed by another Codex session, Claude, a maintainer, or a person. This skill does not automatically launch or grant authority to external agents.
