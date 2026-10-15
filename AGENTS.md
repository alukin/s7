# S7 — Instructions for AI Coding Agents

This file is the canonical, tool-neutral engineering guidance for AI coding agents working on S7. It applies to Codex, Claude Code, and other agents. Tool-specific entry points (for example, `CLAUDE.md`) should refer to this file rather than duplicate its contents.

## Mission and scope

S7 is an open-source system for describing, executing, and observing causal computational graphs, including numerical simulation, digital signal processing, and distributed streaming computations. Keep the computational model independent of any particular GUI, transport, or deployment mechanism.

The project is evolving. Do not assume that an architectural aspiration is already implemented. Distinguish the specification, the current implementation, and proposed changes.

## Read before editing

1. Read this file and the repository README.
2. Locate and read the relevant formal-model, architecture, protocol, and architectural-decision documents under `doc/` (or their current locations).
3. Inspect the affected implementation and tests before proposing a change. Follow the actual repository structure; do not invent paths or APIs.
4. Check the working tree (`git status`) and preserve unrelated user changes.

If documents disagree with code, report the discrepancy. Do not silently treat the implementation as a new specification or rewrite the specification to justify existing behavior.

## Architectural invariants

- **Causality is fundamental.** Separate logical dependencies and logical steps from physical time, threads, and network scheduling.
- **State is explicit and instance-owned.** Do not introduce hidden shared state or accidental coupling between instances.
- **Feedback requires causal memory.** Combinational cycles, including self-loops, must not be accepted merely because graph traversal succeeds. Verify the exact formal contract before changing validation.
- **Preserve evaluation semantics.** Distinguish current inputs, previously published outputs, staged state updates, and committed state. Do not introduce order-dependent behavior inadvertently.
- **Types and conversions are explicit.** Do not add implicit signal conversions or rate changes unless the model explicitly authorizes them.
- **Headless core first.** Keep runtime and computational semantics independent of Studio/Qt and visualization.
- **Observation is not computation.** Probes, plotting, and file output must not silently change model results or scheduling guarantees.
- **Distributed boundaries are explicit.** Preserve stream identity, ordering, failure visibility, and the separation of control plane from data plane. Do not infer cross-stream causality from packet arrival order.
- **No invented guarantees.** Do not describe delivery as durable, deployment as atomic, or execution as bitwise deterministic unless implementation and tests support those guarantees.

## Implementation practices

- Follow the existing language standards, build system, formatting, naming, and error-handling conventions in each subproject. Do not downgrade the C++ standard or introduce dependencies casually.
- In C++, prefer RAII, clear ownership, standard-library facilities, and predictable lifetime management. Avoid unnecessary heap allocations in hot paths, but measure before optimizing.
- Prefer small, reviewable patches. Avoid unrelated refactors, broad formatting changes, and speculative abstractions.
- Keep public APIs, serialized schemas, and wire protocols backward compatible unless a breaking change is explicitly approved.
- Treat input data, configuration, and network messages as untrusted; preserve validation and authentication boundaries.
- Never add secrets, private keys, credentials, or generated build artifacts to the repository.

## Working procedure

1. **Understand:** state the goal, affected components, existing behavior, and relevant specification.
2. **Plan:** identify invariants, risks, and the smallest viable change. For substantial changes, present the plan before editing.
3. **Implement:** modify only necessary files and add or update focused tests.
4. **Verify:** run relevant builds, tests, and static checks when available. For runtime or protocol changes, include negative and failure-path tests.
5. **Report:** summarize changes, exact commands executed, results, and remaining limitations. Clearly label anything not tested.

Ask for an architectural decision before changing causal semantics, state-transition behavior, protocol contracts, or the meaning of a deployment. Do not silently resolve an ambiguity by choosing whichever implementation is easiest.

## Testing priorities

- Graph validation: type errors, missing connections, illegal cycles, self-loops, and nested schemas where supported.
- Causal execution: publication/evaluation/commit order, independent element state, and scheduling-order independence where guaranteed.
- Numerical behavior: reference solutions, convergence with step size, long-run invariant or energy drift, and meaningful tolerances rather than plots alone.
- Distributed behavior: sequencing, reconnect/replay, backpressure, node failure, revision transitions, and recovery according to the documented contract.
- Cross-language behavior: shared fixtures and conformance between C++ and Python implementations for their supported common subset.
- Build and memory safety: run existing CI-equivalent checks and sanitizers when practical; never claim a test passed without executing it.

For floating-point results, distinguish bitwise reproducibility from numerical equivalence. Record tolerances and why they are appropriate.

## Documentation and communication

- Update the relevant documentation and tests when externally observable behavior changes.
- Record important design tradeoffs in the project's architectural-decision mechanism, if one exists.
- Cite concrete file paths and line numbers in audit findings. Separate confirmed defects, specification ambiguities, missing tests, and optional improvements.
- Be candid about uncertainty and incomplete verification. Do not fabricate benchmark results, test outcomes, or implementation details.
- Default to concise technical explanations, with explicit assumptions and actionable next steps.

## Agent interoperability

`AGENTS.md` is the source of truth for agent instructions. For Claude Code, a root `CLAUDE.md` may contain:

```markdown
# S7 agent instructions

@AGENTS.md
```

Keep tool-specific files minimal. Place temporary task plans and session notes outside this file; keep durable architectural decisions in project documentation.
