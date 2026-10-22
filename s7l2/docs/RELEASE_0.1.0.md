# S7 0.1.0 — Linux preview

Scope agreed 2026-10-22. This checklist supersedes broader release gates in
DEVELOPMENT_PLAN.md, OPEN_PROBLEMS.md and LOCAL_SIMULATION_ACCEPTANCE.md for
0.1.0. Deferred work remains in the backlog.

## Supported scope

Local Studio workflow: open → edit → configure → run → observe → compare →
save → reopen, using the bundled runtime without a separate service.
The binary target is Linux x86-64, qualified on Fedora 44 with Mesa/Xvfb.
This build requires GLIBC_2.43, GLIBCXX_3.4.36 and CXXABI_1.3.15;
older distributions are not supported by this binary.

Numerical qualification is limited to the envelopes in
[NUMERICAL_VALIDATION_MATRIX.md](NUMERICAL_VALIDATION_MATRIX.md).
Experimental examples remain experimental. APIs, formats and protocols can
change in subsequent releases. This is a preview, with temporary observation
storage rather than crash-durable recording.

## Final checklist

- [x] Narrow release scope and document platform/feature limits.
- [x] Verify actual disk exhaustion: an 8 MiB tmpfs produces an explicit
  incomplete/error status; all 262,144 retained samples remain readable with
  exact step/time/value data (2026-10-22, revision 0f40caf).
- [x] Final Studio-enabled Release rebuild and complete regression suite:
  73/73 passed in 70.96 seconds, including all four local GUI suites.
- [x] Fresh relocated package smoke check without system Qt/source checkout:
  12/12 passed in a fresh Fedora 44 container with the package read-only.
- [ ] Bundle third-party notices with a per-binary inventory and exact source
  RPM identities; provide corresponding source artifacts alongside binaries.
- [x] REUSE licensing check passes locally (504/504 files at verification);
  required step added to headless CI. Hosted execution of that new step is
  not claimed; GUI testing remains local.
- [ ] Prepare Linux archive, source archives and SHA256SUMS; tag the verified
  release revision as v0.1.0. Publication uses the same artifacts.

Final storage verification also released the failed source and successfully
completed a fresh 4,096-sample disk-backed capture on the same filesystem.
The notice inventory covers 313 deployed ELF files; every referenced notice
exists. Linux-only archive/checksum generation passed through
`tools/release/make_release.sh`.

## Explicitly deferred

- Multi-hour soak, larger graph/channel/viewer envelopes and large interactive
  import/export performance. Bulk operations can block the UI.
- Native Windows packages and platform acceptance; other Linux distributions.
- Physical GPU, high-DPI and accessibility acceptance beyond local automated
  rendering/interaction checks.
- Broader nonlinear/chaotic numerical qualification, new integration methods,
  and distributed production-readiness work.

Existing bounded measurements and cooperative-stop/plugin limits remain in
[LOCAL_SIMULATION_ACCEPTANCE.md](LOCAL_SIMULATION_ACCEPTANCE.md). Preview
delivery may skip samples; authoritative capture is separate. Disk use grows
with recorded steps until the recording source is released. Keep disk space
available and save/export results explicitly.
