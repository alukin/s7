---
name: s7-change-verification
description: Select focused S7 tests and validate diagram changes against structural, semantic and Studio gates.
---

# Change-aware S7 verification

Use during implementation, especially after diagram, core-runtime, protocol, or Studio changes.

1. Read root AGENTS.md and tools/ai/README.md; identify the actual changed paths and intended acceptance contract.
2. From the repository root, run `python3 tools/ai/select_tests.py --base <ref> --build-dir <configured-build>`. Use `HEAD` for working-tree changes, the merge-base SHA for branch review, and `--include-untracked` for new files that are still untracked. Use `--changed` with repository-relative paths to select a narrower scope. Review unmapped files, unmatched test patterns, supplemental checks and deferred GUI checks.
3. For saved diagrams, use `python3 tools/ai/validate_diagram.py --diagram <file>`. For a layout-only edit, compare against the original with `--layout-only` and either `--baseline <file>` or `--base-ref <ref>`. See `s7-diagram-editing` for what this comparison covers.
4. To check Studio loading, pass `--studio <exe> --artifacts <fresh-output-dir>`. Add `--capture` for a 2D PNG and inspect it using `s7-visual-validation`. Static checks, normalization, pixel review and runtime verification each supply different evidence.
5. Build the required test targets, then run selected names with CTest, for example `ctest --test-dir <build> --output-on-failure --no-tests=error -R '^(s7studio_edit_check|s7studio_wire_types)$'`. Use names from `selected_ctest` for the actual change. Use the selector for recommendations until its `--run` expression is fixed. CTest rejects the generated noncapturing group `(?:...)`. Run supplemental checks separately.
6. Report the executed checks and results, including relevant gaps in the test map. Choose broader suites according to the change and applicable release gates.

The test selector is an inner-loop recommendation engine, not an authoritative release gate or a replacement for s7-release-readiness.
