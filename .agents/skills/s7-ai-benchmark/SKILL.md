---
name: s7-ai-benchmark
description: Evaluate S7 Codex skills, scripts, prompt configurations and agent workflows with controlled tasks and measured outcomes.
---

# Evaluate S7 AI development changes

Use when comparing Codex workflows or deciding whether a skill, context strategy or multi-agent step actually pays for itself.

1. Read tools/ai/README.md and inspect the versioned tasks in tools/ai/benchmark_tasks.json.
2. Hold the task, baseline SHA, acceptance checks, host and model configuration constant except for the factor under test. Use separate worktrees from the same clean source baseline when runs may edit files. Capture `env_probe.py --output <outside-Git.json>` before each run. Its fingerprint includes the repository SHA and dirty-worktree flag, so generating variant-specific changes inside a worktree before probing can split otherwise comparable runs.
3. Choose a versioned task with `python3 tools/ai/benchmark.py list`. Run each variant and assess it against the same independent acceptance checks. The benchmark CLI records and summarizes results supplied to it.
4. Record elapsed time, attempts, acceptance evidence and actual token telemetry when available. Use the notes for correction counts or configuration details the CLI has no dedicated field for.
5. Use `benchmark.py record` with `--baseline-sha` pinned to the test baseline and `--environment` pointing to the probe file. Compare with `benchmark.py report`. Reports group by task, variant, baseline and environment fingerprint. If the same variant label mixes models or settings, separate those records before interpreting the aggregates.
6. Report pass rates alongside time and token measurements. The report computes medians for successful runs and shows how many have known token usage. Include failures and blocked runs when assessing the cost of completion.

Store all logs and measured results outside Git unless the maintainer intentionally publishes anonymized evidence.
