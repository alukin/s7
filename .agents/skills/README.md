# S7 repository-local Codex skills

These focused skills complement the root `AGENTS.md`; they do **not** replace its architectural rules. The small YAML descriptions enable Codex to select just the relevant workflow. Open Codex at the S7 repository root to discover them.

| Invoke | Use when |
| --- | --- |
| `$s7-diagram-drawing` | Create a **new runnable** `.s7diagram` |
| `$s7-diagram-editing` | Make **minimal changes** to an existing diagram |
| `$s7-visual-validation` | Check actual Studio diagrams, screenshots, viewers, layout, print/PDF |
| `$s7-numerical-verification` | Independent recurrence/oracle, convergence, DSP and feedback |
| `$s7-distributed-conformance` | Node/protocol/cross-language behavior and fault injection |
| `$s7-education-lab` | Develop and qualify a student laboratory |
| `$s7-hardware-io` | Simulated/replay/real device I/O and measured latency |
| `$s7-release-readiness` | Audit a release candidate against accepted gates |
| `$s7-development-scripts` | Convert recurring build, test, diagram, capture and experiment steps into reusable CLI tools |
| `$s7-change-verification` | Select focused tests and validate diagram functional/layout invariants |
| `$s7-ai-benchmark` | Compare Codex workflows using controlled, recorded tasks and known token usage |
| `$s7-task-handoff` | Create bounded engineering handoffs for agents or future sessions |

Shared helpers: see [tools/ai/README.md](../../tools/ai/README.md) for the working CLI toolchain and its unit tests.

## Suggested invocations

- `$s7-diagram-drawing Create a readable low-pass filter model as a .s7diagram, with a reference test.`
- `$s7-diagram-editing Improve the wiring layout of the buck converter without changing its functional schema.`
- `$s7-visual-validation Run Studio GUI rendering/capture checks on the modified buck diagram and inspect the pixels.`
- `$s7-numerical-verification Verify feedback-settling numerical behavior at two sample rates using independent references.`

- `$s7-development-scripts Make the repeated diagram normalize + Studio GUI verification steps a safe, tested CLI script; reuse existing tools.`

## Token-efficiency rules

- Keep `AGENTS.md` as canonical architecture; skills link to relevant docs instead of copying large contracts.
- The right skill is selected by short `name`/`description`; avoid activating all skills on every task.
- Read only relevant files, run targeted tests before broader suites, use deterministic scripts for repeatable checks and return concise evidence.
- When repeated commands are worth maintaining, apply the repository-wide automation policy in `AGENTS.md`; prefer extending existing `s7l2/tools/` or `s7py/tools/` utilities.
- More skills are **not** automatically cheaper. Track comparable tasks by total tokens, successful completion and correction cycles, not token counts alone.

These are workflow instructions, not new executable S7 features. The skills do not authorize live RF transmission, changes to real audio routes, or deployment of unapproved model changes. Verify actual Codex discovery/activation in your local installation.
