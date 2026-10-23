# S7 0.1.0 preview

Scope agreed 2026-10-22. This checklist supersedes broader release gates in
DEVELOPMENT_PLAN.md, OPEN_PROBLEMS.md and LOCAL_SIMULATION_ACCEPTANCE.md for
0.1.0. Amended 2026-10-23 to include separate control/data listeners as requested
by the maintainer, and to publish an experimental Windows x86-64 build.
Deferred work remains in the backlog.

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

The preview also includes separate mTLS control and encrypted/trusted-plaintext
data listeners for headless nodes; see
[SPLIT_TRANSPORT.md](SPLIT_TRANSPORT.md).

Windows x86-64 binaries are cross-built with Clang 22 and MSYS2 Qt 6.12.
They are an **experimental preview**: 64 of 77 Release tests pass under Wine;
13 Studio GUI tests are blocked by Wine's unimplemented
`mscms.dll.WcsCreateIccProfile`. Native Windows GUI acceptance is still pending.
Packaged runtime/CLI startup, Studio headless smoke, default schema discovery
and noise-plugin loading pass without build-tree or sysroot DLL paths.
The Windows run also exposed a CRLF recording-reader defect, now fixed and
covered by real/complex LF and CRLF replay regressions on both platforms.

## Final checklist

- [x] Separate mTLS control and configurable encrypted/trusted-plaintext data
  listeners, with endpoint discovery, deployment and cross-language regressions;
  refreshed Release suite and installed Linux package. See
  [SPLIT_TRANSPORT.md](SPLIT_TRANSPORT.md).
- [x] **P0: continuous hardware-style oscilloscope viewer**, with bounded
  acquisition memory/storage and no implicit full-run journal. Implementation,
  edge-triggered acquisition, FFT/XY/correlation, window inspection/export and
  worker-side Time preparation are covered by the current native suite and
  documented in [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md).
- [x] Refreshed installed-package smoke check: the real-model live Time plot and
  FFT/QML workflows pass with the package relocated outside the source checkout.

- [x] Narrow release scope and document platform/feature limits.
- [x] Verify actual disk exhaustion: an 8 MiB tmpfs produces an explicit
  incomplete/error status; all 262,144 retained samples remain readable with
  exact step/time/value data (2026-10-22, revision 0f40caf).
- [x] Studio-enabled Release rebuild and complete regression suite: **81/81
  passed** in 57.26 seconds on 2026-10-23, including both split-node profiles,
  all four local GUI suites and the Live Oscilloscope responsiveness test.
- [x] Fresh relocated Linux package smoke check without a source checkout:
  **13/13 passed** from a relocated package under Xvfb/llvmpipe on Fedora 44,
  including the real-model Live Oscilloscope Time plot and FFT/QML workflows.
  Both split-node profiles also pass with the packaged runtime/CLI, including
  the independent Python peer, redeployment and restart/recovery. The package,
  test drivers and run state are outside the checkout, which is not mounted.
- [x] Windows Release suite: **64/77 passed**, with the 13 Wine GUI blockers
  described above; installed-package startup checks: **4/4 passed**.
- [x] Bundle third-party notices with per-binary inventories and exact source
  package identities: 313 Linux ELF files, 84 Windows PE files from S7 and
  28 MSYS2 packages. Qt/dependency sources are excluded from binary bundles.
- [x] REUSE licensing check passes locally (all tracked files at verification).
  The check is also required by headless CI.
- Final archives, checksums, validation evidence and source tag are published
  at [the v0.1.0 release](https://github.com/alukin/s7/releases/tag/v0.1.0).
  Its validation asset records the final artifact checks and source revision.

The original v0.1.0 tag was removed locally and from origin on 2026-10-22
before the fractional-duration correction. The publication above uses the
subsequently verified source revision.
The binary bundle contains runtime libraries and notices, without Qt or other
dependency source archives. Corresponding-source distribution is a separate
publication concern; package metadata records exact source-package identities.

Final storage verification also released the failed source and successfully
completed a fresh 4,096-sample disk-backed capture on the same filesystem.
The notice inventory covers 313 deployed ELF files; every referenced notice
exists. Linux and Windows archive/checksum generation uses
`tools/release/make_release.sh`.

## Explicitly deferred

- Multi-hour soak, larger graph/channel/viewer envelopes and large interactive
  import/export performance. Bulk operations can block the UI.
- Native Windows platform acceptance; other Linux distributions.
- Physical GPU, high-DPI and accessibility acceptance beyond local automated
  rendering/interaction checks.
- Broader nonlinear/chaotic numerical qualification, new integration methods,
  and distributed production-readiness work.

Existing bounded measurements and cooperative-stop/plugin limits remain in
[LOCAL_SIMULATION_ACCEPTANCE.md](LOCAL_SIMULATION_ACCEPTANCE.md). Preview
delivery may skip samples; authoritative capture is separate. Disk use grows
with recorded steps until the recording source is released. Keep disk space
available and save/export results explicitly.
