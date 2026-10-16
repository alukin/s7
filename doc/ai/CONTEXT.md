# S7 — Shared Development Context

> Status: working context for human developers and AI agents. Updated: 2026-10-16.
> This document records project intent and review priorities; it is **not** a substitute for the repository's formal specifications or source code.

## Purpose

S7 is an open-source platform for constructing, executing, and observing **causal computational graphs**. Intended application areas include digital signal processing (DSP), numerical simulation of dynamic systems, and distributed streaming/sensing workloads. A visual editor is useful, but the computational model and runtime must remain usable without a GUI.

The guiding idea is **causality before clock time**: logical dependencies, element state, and graph semantics are distinct from wall-clock time, threads, and network scheduling. This does **not** imply that ordering, sample rates, time alignment, or synchronization can be ignored.

## Sources of truth and precedence

1. `AGENTS.md` — instructions for coding agents, not a replacement for the model.
2. Current formal model and protocol specifications under `doc/` — intended semantic contracts; verify exact filenames and current revisions.
3. Current source code and tests — evidence of what is implemented, not automatic authority to redefine the specification.
4. Approved architectural decisions — rationale for deliberate changes to contracts.
5. This context document — orientation, known goals, hypotheses, and open questions.

When sources disagree, **report the discrepancy**. Do not silently change either code or specifications to force agreement.

## Conceptual architecture

- **Graph/model:** typed elements and connections; elements may be stateless or hold instance-local state. Nested/composite schemas are an intended modeling mechanism.
- **Validation and compilation:** check graph structure and types before execution; cycles require a precisely defined causal-memory mechanism.
- **Runtime:** execute compiled graphs with explicit rules for input consumption, output publication, state staging, and state commitment. Keep it headless.
- **Studio/UI:** graph authoring and visualization, separated from execution semantics.
- **Observation:** probes, plots, recording, and external monitoring should not silently alter computational results.
- **Distribution:** separate control operations (identity, configuration, deployment) from typed data streams (ordering, flow control, failure reporting). Do not infer cross-stream causal order from packet arrival order.
- **Multiple implementations:** C++ is the primary implementation; an independent Python implementation provides an opportunity for protocol and semantic conformance checks. Verify supported subsets in the current tree.

These are architectural goals and themes, **not a claim that every feature is complete**.

## Non-negotiable engineering questions

Before changing graph execution, answer:

1. Which inputs and state version does an element read at logical step `n`?
2. Which outputs are visible during that step, and when are they published?
3. When is next state staged and committed?
4. How are feedback loops broken by causal memory, including single-node self-loops?
5. Which scheduling orders are permitted, and what equivalence of results is guaranteed?
6. How are sample rates, logical step boundaries, and multiple input streams paired?

For distributed execution, also answer:

1. What are the guarantees of prepare, commit, and activation during partial failure?
2. What delivery/replay guarantees apply during reconnection versus process restart?
3. How are stream identity, sequence, configuration revision, and errors exposed?
4. Which ordering and timing relationships are *not* guaranteed across nodes?

Do not assume distributed atomicity, crash-durable delivery, or bitwise floating-point determinism without explicit specification and tests.

## Validation roadmap (proposed, not approved release criteria)

1. **Linear reference models:** known analytical solutions, sample-rate handling, and convergence with step size.
2. **Nonlinear mathematical pendulum:** trajectory, phase portrait, period, and long-run energy behavior. Compare against a high-accuracy reference; separate integration error from runtime-ordering error.
3. **Van der Pol oscillator:** transient dynamics and limit-cycle properties.
4. **Lorenz system:** short-term trajectory comparison and long-term statistical/geometric properties, not indefinite pointwise agreement.
5. **Distributed equivalence:** compare local and multi-node execution under documented assumptions; inject connection loss, restart, and deployment changes.

These tests evaluate both numerical methods and graph-execution semantics. An attractive plot alone is not evidence of correctness.

## Open investigations — do not treat as confirmed defects

- **Self-loop validation:** check whether the formal cyclic-SCC criterion and the actual validator correctly reject combinational self-loops.
- **State/output timing:** verify direct-feedthrough versus causal-memory behavior against the implementation and tests.
- **Multi-input determinism:** determine how inputs from independent sources are associated with logical steps.
- **Deployment consistency:** inspect partial activation and recovery guarantees across nodes.
- **Conformance:** compare C++ and Python behavior on their common supported feature set.
- **Coherence domains:** formalize ordering, sample alignment, and phase comparability separately before expanding implementation.

Each investigation needs code/spec references and a reproducible test before it is called a defect.

## Collaboration across ChatGPT, Codex, Claude, and hosts

- GitHub is the **shared transport and audit trail**. Conversation histories are not automatically shared between AI tools.
- `AGENTS.md` is the common engineering instruction set; tool-specific adapters should be minimal.
- This file holds durable project context. `DECISIONS.md` records only human-approved decisions.
- Task-specific requests and evidence belong in handoff/report files or GitHub issues/PRs, not in this document.
- Agents must distinguish **proposal**, **accepted decision**, **implementation**, and **verified result**.
- Use branches/PRs for competing or parallel changes; pull/rebase before pushing and avoid treating one shared handoff file as a lock.
- Do not commit secrets, private data, or raw chat transcripts by default.

## Updating this document

Keep it concise. Update it when project intent, boundaries, or priorities materially change. Prefer links to authoritative repository docs over copying entire specifications. For concrete changes, include a commit or PR reference when available.
