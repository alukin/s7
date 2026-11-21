---
name: s7-independent-review
description: Independently review high-risk S7 causal, numerical, protocol and concurrency changes against contracts and evidence.
---

# Independent review of high-risk S7 changes

Use for changes affecting causality, numerical methods, wire protocols, state ownership, concurrency, security or release guarantees. Do not spend multiple-agent review effort on routine formatting or minor prose changes.

1. Read AGENTS.md and the authoritative affected formal-model, algorithm, protocol or ABI specification before looking at the implementer's justification.
2. Inspect the actual diff and surrounding code. Anchor each actionable finding to a file and line, describe the observable consequence, and distinguish established defects from gaps in the contract or evidence.
3. Derive expectations from the contract when correctness hinges on state publication, numerical recurrence, wire format or failure ordering. Use a safe local check where it adds evidence.
4. Check negative cases, backwards compatibility, security boundaries, cancellation/ownership and changes under high load where relevant. Prefer small concrete counterexamples over large hypothetical rewrites.
5. Check whether the available evidence covers the reviewed SHA, build configuration and platform. Run a focused check when practical and flag any claim that remains unverified.
6. Report actionable findings in priority order, with the conditions under which each applies and evidence needed to verify a fix. If asked to review only, preserve the code and provide findings.

Independent review can be performed by another Codex session, Claude, a maintainer, or a person. This skill does not automatically launch or grant authority to external agents.
