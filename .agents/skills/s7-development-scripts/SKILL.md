---
name: s7-development-scripts
description: Turn recurring S7 development commands into tested reusable scripts for builds, tests, diagrams, experiments and release evidence.
---

# Reusable S7 development scripts

Use when a task repeats a multi-command workflow, requires reproducible manual steps, or would benefit from a shared command-line entry point. Follow the automation policy in root `AGENTS.md`. **Avoid scripting trivial, one-time operations.**

## Discover the existing entry point

1. Read `AGENTS.md` and the relevant component documentation and test instructions.
2. Search `s7l2/tools/`, `s7py/tools/`, `s7l2/education/generators/`, `s7l2/tools/release/`, CMake targets and `.github/workflows/` for existing commands. Prefer improving an existing script over adding a competing one.
3. State the repeated manual sequence, inputs, outputs, environmental assumptions, and reason automation will save effort or prevent errors. Keep the script scoped to **one useful workflow**.

## Design the script

- Store a shared tool alongside related existing tooling, e.g. `s7l2/tools/<task>.py` for Studio/CMake/diagram work, `s7py/tools/<task>.py` for Python/runtime work, or an established education/release directory. Use Python stdlib for structured JSON/CSV/trace handling; use Bash for short command wrappers. No extra dependency solely for CLI polish.
- Give the command a clear interface: `--help`, relevant `--build-dir`, `--output`/`--artifacts`, `--timeout`, `--filter`, `--dry-run` only when useful. Infer repository paths from the script location; allow user overrides. Do not assume a specific checkout, hostname, screen size or device.
- Run existing canonical commands internally, preserving exit codes, stdout/stderr and truthful skipped/failed states. If several subtasks exist, report which succeeded and which failed. Avoid grepping human-readable success messages when structured results are available.
- Make re-execution deterministic: stable naming, bounded resource consumption, noninteractive defaults, safe reruns, clear cleanup on interruption, and no modification of unrelated files. Use temporary files plus atomic replacement for intended outputs where practical.
- Keep consequential actions opt-in: no implicit `sudo`, package installation, network deployment, destructive cleanup, audio device routing, hardware use or RF transmission. Require explicit flags and preferably `--dry-run` for operations that modify external state. Never embed real credentials or private endpoints.

## S7 workflows worth automating

Prioritize an actual repeated workflow rather than creating all wrappers preemptively:

- **Focused build and tests:** configure or reuse an existing CMake build directory, build relevant targets, run named CTest cases with artifacts; preserve full CI as a separate deliberate choice.
- **Diagram creation/edit validation:** normalize a saved `.s7diagram`, compare intended functional `schema` against a baseline, run applicable layout/edit checks, then launch `s7-visual-validation` capture paths. Use Studio's existing serializer, not a parallel format implementation.
- **Studio visual evidence:** wrap `s7l2/tools/run_gui_test.py` for a verified named GUI case with isolated Xvfb, known artifact paths and honest OpenGL skips. A script may collect PNGs, but the actual image still needs inspection.
- **Numerical experiments:** parameter sweeps, independent reference traces, convergence tables and benchmark comparisons, recording seeds, revision, environment and meaningful tolerances.
- **Distributed conformance:** isolated fixture checks or bounded local multi-process runs with ephemeral ports, test-only certificates and guaranteed cleanup; never silently touch deployed nodes.
- **Release checks:** collect build/test/packaging evidence on a candidate SHA, but never turn a report into an automatic tag/publish action.

## Verify, document and report

1. Run `--help`, one representative valid case, and at least one invalid/failure case. If present, verify `--dry-run` has no side effects. For long or hardware-dependent flows, use a mock/fake backend for script-level verification and clearly label untested integration paths.
2. Put a minimal invocation and expected artifacts next to the relevant source/test documentation; add a narrow smoke test or CI hook when its value warrants the cost.
3. Report the script location, its command syntax, executed tests and outputs, plus time/iteration savings only if measured. Do **not** claim a script executed merely because it was written.

If a script crosses numerical, diagram, GUI, hardware or release boundaries, invoke the corresponding existing S7 skill for its domain-specific acceptance checks; this skill supplies workflow automation, not new correctness guarantees.
