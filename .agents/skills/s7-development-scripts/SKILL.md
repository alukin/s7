---
name: s7-development-scripts
description: Turn recurring S7 development commands into tested reusable scripts for builds, tests, diagrams, experiments and release evidence.
---

# Reusable S7 development scripts

Use when a recurring S7 workflow needs a reusable command. Follow the automation policy in root `AGENTS.md`.

## Discover the existing entry point

1. Read `AGENTS.md` and the relevant component documentation and test instructions.
2. Search `s7l2/tools/`, `s7py/tools/`, `s7l2/education/generators/`, `s7l2/tools/release/`, CMake targets and `.github/workflows/` for existing commands. Prefer improving an existing script over adding a competing one.
3. Identify the workflow inputs, expected outputs and the source of truth for completion. Reuse the existing entry point when it already supplies those guarantees.

## Design the script

- Store a shared tool alongside related existing tooling, e.g. `s7l2/tools/<task>.py` for Studio/CMake/diagram work, `s7py/tools/<task>.py` for Python/runtime work, or an established education/release directory. Use Python stdlib for structured JSON/CSV/trace handling; use Bash for short command wrappers. No extra dependency solely for CLI polish.
- Give the command a clear interface: `--help`, relevant `--build-dir`, `--output`/`--artifacts`, `--timeout`, `--filter`, `--dry-run` only when useful. Infer repository paths from the script location; allow user overrides. Do not assume a specific checkout, hostname, screen size or device.
- Delegate to the canonical commands. Preserve their diagnostics, return codes and skip states. For CTest selections, verify that the named tests actually match and use `--no-tests=error` for execution.
- Make re-execution deterministic: stable naming, bounded resource consumption, noninteractive defaults, safe reruns, clear cleanup on interruption, and no modification of unrelated files. Use temporary files plus atomic replacement for intended outputs where practical.
- Keep hardware use, deployment, installation and other consequential actions explicit. Prefer a dry-run mode for commands that change external state, in line with root `AGENTS.md`.

## S7 workflows worth automating

Prioritize an actual repeated workflow rather than creating all wrappers preemptively:

- **Focused build and tests:** configure or reuse an existing CMake build directory, build relevant targets, run named CTest cases with artifacts; preserve full CI as a separate deliberate choice.
- **Diagram creation/edit validation:** normalize a saved `.s7diagram`, compare intended functional `schema` against a baseline, run applicable layout/edit checks, then launch `s7-visual-validation` capture paths. Use Studio's existing serializer, not a parallel format implementation.
- **Studio visual evidence.** Reuse `s7l2/tools/run_gui_test.py` with an existing test case that produces the required evidence. The runner's capture executable supplies failure and timeout screenshots. Successful images depend on the case or the documented screenshot command.
- **Numerical experiments:** parameter sweeps, independent reference traces, convergence tables and benchmark comparisons, recording seeds, revision, environment and meaningful tolerances.
- **Distributed conformance:** isolated fixture checks or bounded local multi-process runs with ephemeral ports, test-only certificates and guaranteed cleanup; never silently touch deployed nodes.
- **Release checks:** collect build/test/packaging evidence on a candidate SHA, but never turn a report into an automatic tag/publish action.

## Verify, document and report

1. Run `--help`, one representative valid case, and at least one invalid/failure case. If present, verify `--dry-run` has no side effects. For long or hardware-dependent flows, use a mock/fake backend for script-level verification and clearly label untested integration paths.
2. Put a minimal invocation and expected artifacts next to the relevant source/test documentation; add a narrow smoke test or CI hook when its value warrants the cost.
3. Report the entry point and the checks actually executed. Include measured savings only if the task measured them.

If a script crosses numerical, diagram, GUI, hardware or release boundaries, invoke the corresponding existing S7 skill for its domain-specific acceptance checks; this skill supplies workflow automation, not new correctness guarantees.
