---
name: s7-release-readiness
description: Evaluate an S7 release candidate against documented build, test, package, teaching and Studio qualification gates.
---

# S7 release readiness

Use for pre-release audits, CI/packaging checks, milestone acceptance and identifying release blockers. This is a **readiness report** unless explicitly authorized to tag or publish.

1. Read `AGENTS.md`, `s7l2/docs/RELEASE_ROADMAP.md`, the version-specific `RELEASE_<version>.md` and work plan (for example `RELEASE_0.2.0_WORKPLAN.md`). Treat them as the source of release gates; distinguish planning, implemented behavior and evidence.
2. Freeze candidate SHA, branch/tag, build options and supported platforms. Look at `.github/workflows/ci.yml`, CMake targets, docs, dependency installers and packaging. Do not claim that existing nightly/main CI qualifies a different commit.
3. Build and run appropriate targeted then complete tests. Verify headless correctness, numeric golden/reference checks, distributed/negative paths, Studio GUI tests, saved diagrams and educational statuses, optional hardware profiles only when required, and package/install/relocation checks.
4. For 0.2.0, diagram and viewer **PDF/print** validation is mandatory; follow `s7l2/docs/STUDIO_PRINTING_0.2.0.md`. Test that generated pages are nonblank, legible and not clipped; CLI rendering/screenshot or CSV export alone does not satisfy printing.
5. Review exposed API/protocol/schema compatibility, LICENSE/SPDX metadata, artifact provenance, known issues and release notes. Record the actual matrix of pass/fail/blocked/skipped; unrun physical or Windows tests stay unverified.
6. Prioritize blockers by user-visible and contract severity. Recommend minimal fixes and rerun impacted tests on the final candidate rather than using results from older commits.

Report a gate matrix with evidence links/paths, exact commands, environment and SHA; give a conditional ship/no-ship recommendation. Do not silently weaken the acceptance contract or publish a release.
