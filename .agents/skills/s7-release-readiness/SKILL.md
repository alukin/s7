---
name: s7-release-readiness
description: Evaluate an S7 release candidate against documented build, test, package, teaching and Studio qualification gates.
---

# S7 release readiness

Use for pre-release audits, CI/packaging checks, milestone acceptance and identifying release blockers. This is a **readiness report** unless explicitly authorized to tag or publish.

1. Read `AGENTS.md`, `s7l2/docs/RELEASE_ROADMAP.md`, the version-specific `RELEASE_<version>.md` and work plan (for example `RELEASE_0.2.0_WORKPLAN.md`). Treat them as the source of release gates; distinguish planning, implemented behavior and evidence.
2. Pin the candidate SHA, supported platform, build options and artifact under test. Inspect the CI and packaging workflows used for that candidate. Record which checks apply to installed artifacts and which run against source.
3. Build and run appropriate targeted then complete tests. Verify headless correctness, numeric golden/reference checks, distributed/negative paths, Studio GUI tests, saved diagrams and educational statuses, optional hardware profiles only when required, and package/install/relocation checks.
4. For 0.2.0, apply G4/P using `s7l2/docs/STUDIO_PRINTING_0.2.0.md`. Record automated diagram and viewer PDF checks and the documented opt-in physical-printer evidence separately. Keep unavailable printer or platform coverage open.
5. Review exposed API/protocol/schema compatibility, LICENSE/SPDX metadata, artifact provenance, known issues and release notes. Record the actual matrix of pass/fail/blocked/skipped; unrun physical or Windows tests stay unverified.
6. Identify release blockers against the documented gates. Use evidence for the final candidate and rerun affected checks when it changes.

Report a gate matrix with evidence links/paths, exact commands, environment and SHA; give a conditional ship/no-ship recommendation. Do not silently weaken the acceptance contract or publish a release.
