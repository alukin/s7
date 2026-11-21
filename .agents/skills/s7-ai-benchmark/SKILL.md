---
name: s7-ai-benchmark
description: Evaluate S7 Codex skills, scripts, prompt configurations and agent workflows with controlled tasks and measured outcomes.
---

# Evaluate S7 AI development changes

Use when comparing Codex workflows or deciding whether a skill, context strategy or multi-agent step actually pays for itself.

1. Read tools/ai/README.md and inspect the versioned tasks in tools/ai/benchmark_tasks.json.
2. Capture a redacted environment profile with `python3 tools/ai/env_probe.py --output <outside-Git.json>` before each controlled experiment. Pass it via `benchmark.py record --environment <file>` so incompatible environments are not averaged together. Select comparable runs: same task, source SHA, acceptance checks, model configuration and host profile. Run each variant in a separate worktree. Do not use an agent's self-reported success as a substitute for independent acceptance checks.
3. Use python3 tools/ai/benchmark.py list to choose tasks. Manually run Codex using the variants (for example default vs focused skills vs skills+scripts); the benchmark utility does not invoke Codex.
4. Capture elapsed wall-clock time, total attempts and corrective iterations, accepted result, actual independent evidence paths, and input/output tokens only if usage telemetry makes them available. Do not estimate missing usage or present unrun experiments as results.
5. Record with python3 tools/ai/benchmark.py record --results <outside-Git.jsonl> --task ... --variant ... --model ... --status ... --evidence ..., then compare with benchmark.py report.
6. Treat each benchmark as an engineering experiment: inspect failed cases, total successful-task cost and quality, not just shorter prompts. Recommend changes only when gains outweigh maintenance and lost coverage.

Store all logs and measured results outside Git unless the maintainer intentionally publishes anonymized evidence.
