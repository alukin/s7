# S7 AI-assisted development toolchain

The Python tools here are standard-library-only development aids. They **do not** replace S7 runtime, Studio, CMake tests, mathematical oracles, or release gates. Start commands from the repository root. No Codex API access is required.

## Validate an editable S7 diagram

Static structure check (no GUI required):

~~~bash
python3 tools/ai/validate_diagram.py \
  --diagram s7l2/examples/2int_osc.s7diagram
~~~

Validate a *layout-only* change against the version on main, load/normalize with the actual Studio serializer, and capture a 2D PNG:

~~~bash
python3 tools/ai/validate_diagram.py \
  --diagram s7l2/examples/2int_osc.s7diagram \
  --base-ref main --layout-only \
  --studio s7l2/build-release/s7studio/s7studio \
  --capture --artifacts /tmp/s7-diagram-validation
~~~

This checks format v2, unique element/node/wire IDs, connections reconstructed from trunk/junction wire trees, and non-presentation changes. It does **not** infer element port metadata, geometry collisions, simulator correctness, or rendering quality. Studio normalization is optional and required before claiming the diagram loads in Studio. PNG capture checks basic file integrity, **not** pixel inspection or viewer/GUI qualification. Open and inspect the actual PNG and run relevant local Studio GUI tests per s7l2/docs/STUDIO_GUI_TESTING.md. A diagram without explicit wire trees is reported as requiring Studio reconstruction.

The JSON report is printed to stdout; --artifacts writes report.json. All output files remain separate from the original diagram.

## Select relevant tests during editing

~~~bash
python3 tools/ai/select_tests.py --changed \
  s7l2/s7studio/src/diagram/diagram_scene.cpp \
  --build-dir s7l2/build-release

# Include GUI tests only when working on a local graphical test host:
python3 tools/ai/select_tests.py --base main --build-dir s7l2/build-release \
  --include-gui --strict

# Run selected CTest cases from a configured and built directory:
python3 tools/ai/select_tests.py --base main --build-dir s7l2/build-release --run
~~~

This uses editable, conservative test_map.json. Without --build-dir it prints suggested patterns and supplemental instructions; with a build directory it discovers actual names using ctest --show-only=json-v1. Missing patterns and unmapped files stay visible. --strict fails on either. --run executes only matched cases, propagates CTest failures and refuses an empty selection. It does **not** build the targets, run Python/Java commands, or replace full CI or release qualification. Improve the mapping when components change. GUI tests are opt-in and stay local by maintainer policy.

## Create bounded handoffs between agents or sessions

~~~bash
python3 tools/ai/task_handoff.py init \
  --id improve-feedback-diagram \
  --goal "Improve layout without changing causal behavior" \
  --path s7l2/examples/2int_osc.s7diagram \
  --invariant "functional schema and probes unchanged" \
  --check "Studio normalization and actual image review" \
  --output /tmp/s7-task.json
python3 tools/ai/task_handoff.py check /tmp/s7-task.json
python3 tools/ai/task_handoff.py show /tmp/s7-task.json
~~~

The JSON format records baseline commit, scope, preserved invariants, acceptance checks, task status and command/evidence entries. init refuses overwriting an existing handoff. accepted requires actual execution results. This is a handoff, not permission to skip code inspection.

## Evaluate Codex instructions, skills and scripts

~~~bash
python3 tools/ai/benchmark.py list

# After actually running a controlled task on an isolated worktree:
python3 tools/ai/benchmark.py record \
  --results /tmp/s7-ai-results.jsonl \
  --task diagram-layout-preservation --variant skills-and-scripts \
  --model codex --status pass --attempts 1 \
  --duration-s 120 --evidence /tmp/s7-diagram-validation/report.json
python3 tools/ai/benchmark.py report --results /tmp/s7-ai-results.jsonl
~~~

The sample numbers above show command syntax; they are **not measured S7 results**. Input/output token counts should be recorded only when the usage source exposes them; otherwise they remain null. The benchmark accepts reported outcomes and evidence references but does not run Codex or independently certify completion. Compare the same task, baseline SHA, acceptance checks and comparable hardware/model settings. Also track time and correction attempts. Seven candidate tasks are defined in benchmark_tasks.json. Keep run results outside Git.

## Tool self-tests

~~~bash
python3 -m unittest discover -s tools/ai/tests -v
~~~

These tests run in headless CI. Actual Studio/Qt tests, 3D rendering, real SDR/audio operation and Codex invocations are **not** part of them.

Reuse existing S7 scripts first, including s7l2/tools/run_gui_test.py, existing generators, dependency installers and release packagers. Favor deterministic, safe operations with explicit flags, clear exit codes and no implicit hardware activation.
