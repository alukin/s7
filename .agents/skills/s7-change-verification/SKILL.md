---
name: s7-change-verification
description: Select focused S7 tests and validate diagram changes against structural, semantic and Studio gates.
---

# Change-aware S7 verification

Use during implementation, especially after diagram, core-runtime, protocol, or Studio changes.

1. Read root AGENTS.md and tools/ai/README.md; identify the actual changed paths and intended acceptance contract.
2. Run python3 tools/ai/select_tests.py --base HEAD (or a review base such as main), optionally --build-dir <configured-build>. Review both the selected tests and unmapped files or unmatched expressions. A recommendation is not a pass.
3. For saved diagrams, run python3 tools/ai/validate_diagram.py --diagram <file>. Use --base-ref <base> --layout-only only for intended presentation-only edits. If Studio exists, add --studio <exe> --artifacts <outside-Git-dir> and --capture for a 2D render.
4. Inspect actual pixels with s7-visual-validation; a generated PNG or Studio normalize pass cannot prove visual quality, numerical correctness, or viewer rendering.
5. Build required targets before using --run in the test selector. Run tests appropriate to the changed behavior; use broader CI-equivalent/full tests before merge when needed. GUI tests remain opt-in local workflows; never silently treat missing graphics as a pass.
6. Report exact commands, SHA, selected/omitted tests, numeric/oracle evidence, diagram functional diff, screenshot paths and actual inspection findings. Explicitly flag unmapped or unrun checks.

The test selector is an inner-loop recommendation engine, not an authoritative release gate or a replacement for s7-release-readiness.
