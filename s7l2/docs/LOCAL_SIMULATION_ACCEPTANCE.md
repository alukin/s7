# Local simulation acceptance — Linux baseline

Measured on 2026-10-21. This is a bounded acceptance record, not a release-ready
claim. Numerical qualifications remain in
[NUMERICAL_VALIDATION_MATRIX.md](NUMERICAL_VALIDATION_MATRIX.md).
Raw logs, captures, packages and measurement JSON stay outside Git.

## Build and regression evidence

Fedora 44, AMD Ryzen 7 4700U (8 logical CPUs), 16 GiB RAM, GCC 16.2.1,
Qt 6.11.2. A clean Studio-enabled Release configuration built `build-tests`;
all **73/73 CTest entries passed** in 74.72 s, including four Xvfb GUI suites,
real Qt Quick 3D rendering, the embedded-demo check and both long pendulum
checks. No rendering skip was counted as a pass.

Hosted CI originally failed because the nonlinear-pendulum and Scope-lifecycle
tests treated intentional preview loss as recording loss. Commit `3026b84`
reads the authoritative worker source instead. The pendulum test withholds
preview delivery, checks its exact skip count, compares every captured value
and step against the headless trace and verifies every CSV timestamp/value
round trip. Both affected Release tests passed in 20.98 s before pushing.
Both hosted jobs passed for `3026b84`: Studio passed 68 existing regressions
and four GUI suites. Both jobs also passed for the package implementation
`89628cc`: Studio passed 69 regressions and four GUI suites. Run links are
recorded in DEVELOPMENT_PLAN.md. A focused Clang 22 headless
ASan/UBSan build additionally passed core, plugin-loader and runtime-control
smoke checks (3/3, 0.87 s), without sanitizer diagnostics; no full sanitizer
matrix is claimed.

## Installed workflow

A local Release AppDir was built in a path containing spaces and mounted
read-only at `/opt/S7 installed` in a fresh Fedora 44 container. Only desktop
base dependencies, Python, Xvfb and Mesa were installed; no system Qt and no
source checkout were available. The working directory was a separate temporary
directory. All **12 package checks** passed:

- Library resolution and CLI startup.
- Packaged default schema and executable-relative noise-plugin execution.
- Embedded startup demo and diagram serialization/save/reopen.
- Scope at 1/10/100 kHz, Phase Portrait at 1/10/100 kHz and 3D trajectory at
  1/10 kHz, including exact headless/discrete-reference pairing, late opening
  and reopening.

The package requires GLIBC_2.43, GLIBCXX_3.4.36 and CXXABI_1.3.15.
Other distributions and older runtimes are not qualified. See
[Linux packaging notes](../tools/linux/NOTES.md) for layout, commands and notice
inventory. CSV fidelity is covered by the regression suite; the installed
package check does not repeat the full pendulum export matrix.

## Measured operational envelope

The opt-in `s7studio_operational_acceptance` tool opens a saved RLC step model,
sets the rate through the production Run dialog, and runs continuously with
one visible four-channel Scope. The four runtime elements are Constant,
SymplecticIntegrator (Euler–Cromer), Adder and Gain; observer wires provide the
four channels without duplicate ad-hoc probes. No network service is needed.
Each rate ran for 30 wall seconds under Xvfb/Mesa llvmpipe, without concurrent
local builds or tests. Results below are rounded from the final measurement.

| Model rate | Steps / wall second | Captured samples | Recording GiB | Peak sampled RSS MiB | Maximum 10 ms timer gap ms | Pause ms | Stop ms | Reset/start/close ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 kHz | 427750 | 51394460 | 1.532 | 638.5 | 243.635 | 0.019 | 121.615 | 17.278 |
| 10 kHz | 424823 | 51104780 | 1.523 | 702.1 | 221.708 | 0.023 | 0.046 | 19.444 |
| 100 kHz | 429014 | 51592332 | 1.538 | 751.7 | 250.178 | 0.023 | 111.726 | 21.235 |

All captures finished without storage/run errors or missing channels. First
and final step/time extents matched the requested model rate. Pause stopped
advancement at an acknowledged boundary. The shutdown measurement includes
reset, starting a fresh continuous run, and destruction of its MainWindow;
it is not a pure destructor-only timing. Timer gaps measure event-loop service,
not input-to-photon latency or a physical display's frame rate. These local
latency checks use a two-second budget per operation/timer gap; recorded maxima
are observations, not universal upper bounds.

The runtime is unpaced here: model frequency controls dt, not wall throughput.
Recording bytes equal captured samples times the 32-byte native Sample size,
excluding metadata, filesystem overhead, comparisons and export copies. At
four channels this is 128 bytes per step. Keeping up with 100 kHz in real time
would therefore consume about 12.8 MB/s (46.08 GB/hour) of capture storage;
the measured unpaced workload writes roughly 54 MB/s. A filled storage device
must end capture with an explicit incomplete/error status; it is not a circular
or indefinitely retained recording service.

The authoritative journal spills after 20000 samples; the preview mailbox
retains at most 200000 samples, and view refresh reads bounded pages. RSS is
the whole GUI process, including Mesa and allocator caches, sampled every
10 ms. At 7.5/15/22.5/30 seconds the final run's RSS fluctuated rather than
tracking its gigabytes of captured data. This 90-second workload establishes
only the reported memory envelope; it does not prove a multi-hour plateau or
bounds for arbitrary graph/viewer counts. Disk history grows with captured
steps until its source is released. Preview skips occurred, while authoritative
capture stayed complete.

Reproduce (artifacts belong in `/tmp`):

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DS7_BUILD_STUDIO=ON -DS7_BUILD_GUI_REGRESSION_TESTS=ON
cmake --build build-release --target build-tests s7studio_operational_acceptance --parallel 2
ctest --test-dir build-release --output-on-failure -j 2
python3 tools/run_gui_test.py --artifacts /tmp/s7-operational-release --capture build-release/s7studio/s7studio_gui_capture --timeout 240 -- "$PWD/build-release/s7studio/s7studio_operational_acceptance" "$PWD/examples/linear/rlc_step_symplectic.s7diagram" /tmp/s7-operational-release.json 30
```

## Remaining release gates

- Distribution notices: this host lacks installed license files for bundled
  `openxr-libs`, `xcb-util-keysyms` and `kwin-common`. The default package command
  fails and records the missing inventory. Only the explicitly marked local
  test package used `S7_ALLOW_INCOMPLETE_NOTICES=1`; nothing was published.
  Relicensing is settled (ADR-019, `LICENSING.md`); source/offer obligations
  remain separate from collecting notices.
- Native Windows, physical GPU/high-DPI/accessibility checks and multi-hour
  resource behavior remain unverified. The Windows executable-location helper
  cross-compiled successfully; the installed MSYS2 sysroot lacks Qt Qml, so no
  updated Windows Studio/package validation is claimed.
- Existing storage tests inject write/flush failures. Actual device exhaustion,
  large interactive import/export latency, and a broader graph/channel/viewer
  envelope still need acceptance evidence.
- Existing example classification remains authoritative: the linear matrix and
  selected pendulum baselines are qualified only within their stated envelopes;
  the remaining control/nonlinear/chaotic teaching families remain experimental.
  This pass adds no solver, integrator or numerical qualification.
