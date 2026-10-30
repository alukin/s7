# S7 Development Plan

This document retains the 0.1.x local-simulation plan and implementation
history. Current 0.2.0 ownership, distributed milestones and parallel
architecture/simulation/Studio tracks are in the
[0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md).

## 0.2.0 C++ goals — agreed 2026-10-23

After the preview, prioritize multithreading within a single runtime and
dependable multi-node execution on local and remote hosts. The
[0.2.0 plan](RELEASE_0.2.0.md) records causal/threading invariants, the existing
two-node MVP baseline, cross-language coverage and the acceptance work still
to define. This records future goals; it does not expand the 0.1.0 gates below.
The 0.2.0 scope also includes a software signal-generator element, added on
2026-10-25 with the UNI-T UTG932E as its hardware interaction reference.

## Additional 0.1.0 requirement — separate control/data listeners (2026-10-23)

User-directed before the preview release: keep control mutations on mTLS and
allow bulk data on an explicitly trusted loopback/LAN without encryption.
The [split transport contract](SPLIT_TRANSPORT.md) defines independent listeners,
node configuration, discovery and committed data endpoint/security fields.
This is intended to support Python and other headless implementations as well
as constrained devices; no target-device performance claim is made.

Acceptance: encrypted and trusted-plaintext data modes; control/data path
isolation; required client certificates on the split control listener; explicit
local and deployed policy with no fallback; IPv4/IPv6; verified Studio discovery;
real-node deployment, redeployment, restart/recovery and an independent Python
peer. Rebuild and run the complete Release suite and refresh the installed
Linux package before declaring the new gate complete. Local observation remains
in-process; adding remote observation APIs is separate work.

Local verification (2026-10-23): Release `build-tests` passed; all **81/81**
CTest entries passed in **58.57 s**, including the new two-node encrypted and
trusted-plaintext profiles, both TLS credential-source policy checks and all
four GUI suites. Clang ASan/UBSan transport coverage passed with leak detection
(1/1, 11.22 s), including channel rejection and IPv4/IPv6 reconnect behavior.
A refreshed relocated package passed **13/13** installed checks plus both
split-node deployment/redeployment/restart/recovery scenarios in the Fedora 44
container without a checkout mount. Each profile also passed the independent
Python WebSocket peer. The guide is included in the package. This completes
the added local preview gate; no target-device performance, native Windows,
hosted CI execution, release tag or published artifact is claimed.

## P0 release priority — continuous oscilloscope viewer (2026-10-22)

Current sequencing: FFT spectrum, constellation and correlation views, shared
paired cursors, interval measurements, frozen-window JSON/CSV export and sampled
edge-triggered Time acquisition are implemented as bounded increments. The
trigger supports Auto, Normal and Single modes, rising/falling edges, hysteresis,
pre-trigger coverage, holdoff and partial stopped captures. See
[triggering help](STUDIO_TRIGGERING.md). Full operational qualification remains
open; the implementation does not establish ADI OSC parity or sub-sample phase
interpolation. Verification on 2026-10-23: all 79 native CTest tests passed,
including the trigger GUI workflow; focused trigger/inspection ASan+UBSan tests
passed, and the Windows cross-build completed.
The [FFT spectrum increment](STUDIO_SPECTRUM.md) adds the private binary64 FFTW
backend, bounded worker scheduling, real/IQ normalization, windows, power averaging
and referenced dB. [XY/correlation](STUDIO_PAIR_ANALYSIS.md) adds raw paired plots,
model-step persistence/density and normalized linear correlation. Only the visible
tab of a visible, non-minimized window submits analysis; inactive tabs cancel
queued work and stop polling. Full operational qualification remains open.

The first [time-domain prototype](LIVE_OSCILLOSCOPE_PROTOTYPE.md) is implemented:
bounded worker-side acquisition, live/recording subscriptions, online retention
settings, freeze/resume, raw-sample hover and extrema-preserving display reduction.
The [design](LIVE_OSCILLOSCOPE_DESIGN.md) remains the target for four-view analysis,
triggers and qualification. Prototype implementation does not close the release
acceptance gate or establish ADI OSC parity.

Follow-up for sporadic Live Oscilloscope UI freezes: bounded acquisition snapshot
copying and Time reduction now run on the shared analysis pool, with one active
request and a replaceable latest pending request per viewer. Qt Graphs updates
remain on the GUI thread. The rendered GUI workflow passed a 35-second,
eight-channel load (30 seconds paced at 100 ksample/s, then 5 seconds unthrottled)
with a 90 ms maximum gap on its 10 ms UI timer under Xvfb/software rendering.
A 30-minute synthetic acquisition soak processed 1.492 billion samples at about
828,700 samples/s using 1.001 CPU cores and 33 MiB peak RSS; its 20 MiB acquisition
reservation remained fixed. These checks do not characterize runtime graph cost,
full Qt plot throughput, or native Windows rendering. The Windows cross-build
and Wine trigger test passed; the Live Oscilloscope Wine test aborted because
Wine's `mscms.dll` does not implement `WcsCreateIccProfile`, so Windows GUI
behavior remains unqualified. The refreshed relocated Fedora 44 Linux package
smoke check passed 13/13, now including a real-model Live Oscilloscope Time plot
and FFT/QML rendering. The final archive is prepared locally; no release tag or
GitHub release has been created.

Continuous execution already exists (`runtime_seconds = -1`). The current
Scope is an entire-run recording/comparison viewer: bounded RAM spills history
to temporary disk. An endless run therefore grows storage until it fails.
A bounded display cache alone does not fix the worker's authoritative journal.

Required outcome: a separate hardware-style oscilloscope viewer for live,
continuous inspection, retaining a bounded acquisition/time window. Both RAM
and disk consumption must stay bounded when full-run recording is not selected.
Preserve the existing Scope's explicit recording/export/comparison workflow.

Design/implementation tracking (see the design for decisions):

- Continuous-run UI: make endless operation discoverable rather than requiring
  users to know the `-1` sentinel; retain finite fractional-duration runs.
- Acquisition: model-time timebase/window length, channel and sample caps,
  eviction/decimation rules and a visible indication of skipped data.
- Hardware-style controls: channel visibility and scale/offset, timebase,
  acquisition pause versus simulation pause, and whether the first increment
  needs auto/normal/single edge triggering or starts with a rolling trace.
- Capture ownership: define viewer/probe subscriptions so oscilloscope-only
  operation does not create an unbounded full-run journal. Define mixed use
  with recording Scopes and make any full recording an explicit choice.
- Freeze/export: decide whether saving covers the current visible acquisition
  window or a separate explicit recorder; never imply unlimited retained data.

Delivery sequence:

1. Document the acquisition/capture policy and update the observation contracts.
2. Implement bounded worker-side acquisition and read-only viewer delivery,
   with explicit ownership and lifecycle handling; no causal stepping changes.
3. Register the new viewer in Studio and expose its agreed controls and status.
4. Add offline help/example coverage and deterministic tests, then verify the
   installed Linux package locally before recreating a release tag.

Acceptance: exercise many buffer wraps and multiple channels; prove sample and
storage bounds and preserve model timestamps/order. Verify visible acquisition
behavior, any agreed trigger modes, simulation pause/resume/stop/reset/rerun,
viewer open/close/reopen and shutdown. Show that oscilloscope-only continuous
runs do not accumulate a temporary recording, while explicitly requested Scope
recordings keep their completeness/error behavior. Measure bounded local UI
work and resource use; preserve headless execution and existing numerical tests.

### Shared inspection and window export — 2026-10-22

[User/offline help](STUDIO_WINDOW_INSPECTION.md) documents persistent A/B cursors
across all four tabs, raw/model-step or derived-bin bindings, keyboard movement,
copyable precision, inclusive measurements and bounded JSON/CSV export. Statistics
use scaled compensated sums and shifted variance, report nonfinite coverage and
overflow, and ignore Time display gain/offset. Derived-bin RMS is explicitly
distinct from signal RMS; latest-transform raw-input statistics are shown separately.

Export streams an immutable admitted snapshot asynchronously, with progress,
cancellation and atomic destination replacement. It preserves integer identities
above 2^53, source/stored types, rational rate, raw values, computed arrays,
normalization/settings and averaging/hold coverage. A/B annotate measurements;
the full pinned window is exported. `.s7window` is a documented JSON export,
not a complete-run `.s7scope` recording or an implemented import format.

Release `build-tests` built and the final **78/78 native CTest entries passed in
55.61 s with eight test jobs** on the eight-core, 15 GiB notebook. An earlier
two-job run passed in 84.98 s. Future builds use six compile jobs and tests use
eight jobs on this machine.
The new backend entry covers independent measurement references, extreme scales,
small variance, cancellation, destination preservation, nonfinite values, locale,
round-trip values and large identities. Rendered tests exercise actual plot clicks,
A/B movement/keyboard/copy, raw measurement independence from gain/offset,
retention changes during frozen export, all four views and reset. Screenshots
were reviewed. Final refinements keep right-edge cursor markers inside the plot,
show baseband versus display coordinates, and preserve the pinned owner even when
Freeze was already checked and an analysis request was queued. The final full
run includes these refinements, the deterministic queued-owner regression and
updated embedded help; its GUI workflow entry passes in **11.10 s**.

The Windows Studio and inspection backend cross-build succeeds; the new backend
entry passes under Wine (**5.08 s**). Clang AddressSanitizer/UndefinedBehaviorSanitizer
with leak detection passes the same backend entry (**0.62 s**). Native Windows
rendering, installed-package refresh, resource/latency soak, triggers, saved
workspaces, image export and window import remain open. No runtime stepping or
headless dependency change was made.

### XY/correlation and inactive analysis verification — 2026-10-22

Release `build-tests` built and **77/77 native CTest entries passed in 77.38 s**.
The new backend entry has 17 passing cases, including independent direct sums
for every correlation normalization, signed delays, maximum N, extreme scales,
zero energy, raw XY/density, persistence eviction/deduplication, repeated storage
wraps and cancellation. The rendered workflow runs the new causal IQ/six-Delay
example, checks the unit circle, actual QML hover, equal aspect, density image,
positive six-sample delay, frozen reanalysis, close/reopen and reset. Screenshots
of XY, density and correlation were reviewed. Scheduling assertions verify that
hidden tabs and minimized windows submit no analysis, while returning resumes it.

An initial full run exposed an existing lifecycle-test race: a paused worker can
stop before its transient Stopping notification, leaving the reused test gate
closed for the next run. The fixture now explicitly opens the gate after checking
replacement/teardown. Runtime behavior is unchanged; the final full run above
includes this correction. Constant subnormal XY inputs also retain valid density
bin bounds rather than dividing by a rounded-to-zero axis span.

The full Windows Studio cross-build succeeds with both views. FFT and paired
numerical/worker entries pass under Wine (**2/2, 10.05 s**); GUI execution still
has the Wine 11.0 color-management limitation recorded below. The same two backend
suites pass Clang AddressSanitizer/UndefinedBehaviorSanitizer (**2/2, 1.13 s**),
including leak detection outside the sandbox's ptrace restriction.

This increment adds no runtime/plugin dependency or stepping changes. It does not
claim an installed-package refresh, native Windows rendering, long-run resource
qualification or full design parity. Shared paired cursors, interval measurements,
saved-window export and trigger/workspace features remain open.

### FFT spectrum verification — 2026-10-22

The Release `build-tests` target built and **76/76 native CTest entries passed in
72.94 s**, including the new numerical/worker suite, the rendered AM carrier/bin
inspection workflow, frozen reanalysis after retention shrink, and the packaged
program's `--live-spectrum-check`. Numerical coverage includes independent
long-double DFT and time-domain power references, five windows, real/IQ, explicit
dB references, linear averaging, skipped hops, cancellation and raw-frame ownership
when a prior hop is reused. A tab-removal callback into an already deleted Time
plot was found by the rendered test and fixed before this full passing run.

The backend/worker suite also cross-built against the existing Windows Qt Core/Test
and FFTW import library, and passed under Wine (4.95 s). The full Studio cross-build
is blocked by the existing sysroot's missing Qt QML component; this does not establish
Windows UI parity. The native backend suite passed Clang AddressSanitizer and
UndefinedBehaviorSanitizer (0.40 s). A fresh headless configure succeeds without
discovering FFTW, and dependency-script dry runs retain it only where required.

A fresh 313 MiB package passed **13/13 installed checks** in the local Fedora 44
container with the package mounted read-only. The added check executes a causal
oscillator through live-only capture, verifies a 64 Hz / 2 V Hann FFT, loads the
QML plot and saves a screenshot, reviewed at 1280×720. The package contains the
double-precision FFTW library, GPL notices and exact Fedora source-package identity.

An initial backend-only timing sample on AMD Ryzen 7 4700U, Fedora 44, GCC 16.2.1,
FFTW 3.3.10 measured p50/p95/max **0.131/0.144/0.240 ms** at N=4096 and
**4.089/4.669/5.488 ms** at N=65536. Each size used 30 warmup and 300 timed real,
periodic-Hann, amplitude transforms at Fs=100 kHz, including input checks/windowing
and normalization, with cached plans and no GUI/acquisition cost. This ran while
other verification work was active and is not an end-to-end latency qualification.
The opt-in `s7studio_spectrum_benchmark` target reproduces this workload; no machine
timing threshold is imposed by CTest. Long-duration/full-workload qualification,
native Windows rendering, constellation/correlation and the shared inspection/export
features remain open.

### Windows QML dependency follow-up — 2026-10-22

Installed the missing target QML/Quick/QuickWidgets/Graphs/Quick3D dependencies
with the repository's verified MSYS2 sysroot fetcher (48 packages, matching Qt
6.12.0 modules). CMake now configures the full Studio cross-build, and Studio,
the FFT test and the live acquisition test all compile and link successfully.
The Windows FFT numerical/worker entry passes under Wine 11.0. The two GUI entries
abort on Wine's unimplemented `mscms.dll.WcsCreateIccProfile`; this is a separate
Wine runtime limitation after resolving QML discovery, not a passing UI result.
Native Windows rendering qualification remains open.

### Prototype verification — 2026-10-22

Resumed the interrupted implementation and completed the Release `build-tests`
build. Display gain/offset overflow now hides the affected trace with an explicit
message while preserving raw inspection; the hover cursor stays inside the visible
time range. Regression coverage includes the aggregate eight-channel limit across
workspaces and rejection before stepping when a ninth channel is requested.

The full native suite passed 65/74 entries inside the sandbox; the remaining nine
were blocked by socket/Xvfb restrictions and all passed with those restrictions
lifted (9/9, 16.26 s). After the final viewer changes, all seven focused acquisition,
viewer and rendered GUI entries passed together (7/7, 7.74 s). All 74 entries have
passing evidence across these runs; a second full-suite execution is not claimed.
Rendered screenshots of hover, freeze and multichannel AM/FM views were reviewed.

A fresh 310 MiB Linux package passed 12/12 installed checks in the local Fedora 44
container with the package mounted read-only, including bundled help/examples,
save/reopen and existing observation viewers. This is packaging/regression evidence,
not a live-acquisition soak or performance qualification. Four-view analysis,
triggers, saved workspaces/window export, native Windows rendering and the design's
long-duration resource/latency acceptance gates remain open. No release tag was made.

## Fractional run duration correction — 2026-10-22

Run Configuration, saved `studio_run.runtime_seconds` and execution config now
accept finite fractional seconds. Whole-step calculation uses the canonical
decimal duration and rational model rate, flooring without boundary overshoot.
The dialog warns below 100 steps without blocking a valid short run; invalid
negative values other than `-1` are rejected. Continuous execution remains
available. The original v0.1.0 tag was removed locally and from origin; Help
was already committed and pushed as 7babee8 before this correction.

Release `build-tests` and the optional operational-acceptance target built.
Model-time tests cover fractional execution, decimal/scientific boundaries,
values just below a boundary, invalid/nonfinite durations and document parsing.
Document safety now exercises fractional duration through save/reopen. Both
focused entries passed (2/2, 3.54 s). The broad Release suite passed 72/73;
the GUI test alone needed to wait for the queued completion update to re-enable
Run. After that test synchronization correction, the whole GUI workflow suite
passed in 6.33 s, including 125- and 99-step actual runs and warning boundaries.
All 73 entries have passing evidence across those runs; no second full-suite
execution is claimed. No oscilloscope implementation is included here.

## Help and About follow-up — 2026-10-22

Studio now has a Help menu with F1, embedded offline articles, full-text topic
filtering and Back/Forward/Home navigation. The guide includes a bundled-example
catalogue and all family Markdown notes, preserving the numerical validation
classification. About identifies author/maintainer Oleksiy Lukin, contributor
teren-ukr, acknowledgements, version/build/Qt/system details and licences.
Dialogs are modeless, reusable and owned by the main window.

Release `build-tests` passed; all **73/73 CTest entries passed in 71.06 s**,
including the four local GUI suites. The Help/About GUI regression verifies F1,
every article, search/no matches, internal navigation, reopening, author and
contributor credits, and unchanged diagram state. Screenshots were reviewed
locally. A fresh read-only Fedora package passed **12/12 installed checks**;
its embedded-demo check now also opens every help article and About credits
without a source checkout or system Qt. REUSE passes for 507/507 files.
No dependency sources are bundled. This follow-up does not move the existing
v0.1.0 tag or replace its earlier artifact; those precede the Help UI.

**0.1.0 scope update (2026-10-22):** ship a Linux preview under the narrower
[release checklist](RELEASE_0.1.0.md). That checklist supersedes broader release
gates below; multi-hour/resource, native-platform and expanded numerical work
is deferred explicitly. Historical evidence below retains its original scope.

Simulation follows the [analog-computer model](SIMULATION_MODEL.md): dynamics
are explicit element networks. RK4 and other independent solvers belong in
test references, not a new runtime simulation engine.

Date: 2026-10-21
Status: living document — update in place as phases complete or the sequence changes; do not create a new dated copy.

This is the sequencing companion to `docs/OPEN_PROBLEMS.md`: that document lists what is unsolved, this one orders the work. The next-release section defines the current commitment; the later phases preserve the longer-term backlog.

## R1 lifecycle completion — 2026-10-20

User priority: finish next-step 1, then commit and push. Starting revision
`b9b5285`; tracked tree clean, the two untracked user prompt files are preserved.
The preceding baseline passed 61 existing + 4 GUI tests; this increment will
rebuild and verify lifecycle changes before pushing, without rewriting history.

| Task | Priority / finding | Implementation and affected files | Risks/dependencies | Regression / acceptance | Status |
|---|---|---|---|---|---|
| R1a | P0 confirmed: pause is a request but immediately reports paused; polling waits; independently stored flags can obscure transitions | `is_schema_runner.hpp`, `local_schema_runner.*`, `model_run_controller.cpp`: explicit Idle/Preparing/Prepared/Running/Pausing/Paused/Stopping/Stopped/Completed/Error; condition-variable pause acknowledgment at a complete step boundary; state-aware actions and event generation guards | Preserve existing causal stepping; stop/reset remain cooperative and cannot interrupt arbitrary plugin code mid-step | Deterministic gated-element tests for pause boundary, stop while paused/in-flight, repeated pause/resume/stop/reset/rerun and terminal states; no arbitrary sleeps | Verified: focused lifecycle, GUI and full 67-test suite passed |
| R1b | P0 confirmed: worker inputs are copied but no exposed immutable configuration; observer routing still reads edited scene | Frozen run ID/schema/dt/ticks/probe snapshot in runner, run IDs on mailbox batches, frozen MainWindow observer mapping; persistent Run-panel summary and next-run edit notice; isolate changed/new observer IDs and types | Maintain ad-hoc viewers and saved comparisons; observation stays read-only; recording completeness is O1, not this task | Edit params/config/observer wiring during run: current values/routes stay fixed; next run uses edits; viewers created/deleted during transitions receive correct-generation samples | Verified: focused lifecycle, GUI and full 67-test suite passed |
| R1c | P0 confirmed: destructor disconnects worker but joins it only during later QObject-child destruction; callbacks do not check shuttingDown | MainWindow explicit stop/join/reset before member/plugin teardown; invalidate generation and guard all UI callbacks during close/destruction; runner invalidates pending owner events on reset/rerun | Shutdown latency bounded by current element step/build; no unsafe thread cancellation or GUI event-loop pumping while joining | Close/destruct running and paused windows; queue callbacks before close/reset/rerun and verify no view resurrection/status/log contamination; test element destruction after worker exit | Verified: focused lifecycle, GUI and full 67-test suite passed |
| R1d | P0 coverage: preparation errors covered incompletely, worker failure/recovery and rapid transitions lack deterministic gates | Dedicated Qt Test runner and MainWindow lifecycle regressions using test-owned semaphore/condition gates and throwing elements; extend existing lifecycle/GUI tests; update lifecycle docs | Qt owner-thread commands; test gate always released before teardown; no changes to core numerical contracts | Invalid config/build, runtime exception and recovery; immutable snapshot; controller actions; fresh step/run IDs; all targeted, GUI and broader regressions pass | Verified: focused lifecycle, GUI and full 67-test suite passed |

Order: commit this plan → runner state/snapshot implementation + deterministic
runner regressions → MainWindow routing/shutdown and integration tests → update
living documentation → build/tests → commit → ordinary push to origin/main.
No recording redesign, numerical-engine change, licensing change or vendor edit
belongs to this increment. Acceptance requires every R1 row above verified; any
inherently blocking user/plugin step is documented as a cooperative-stop limit,
not presented as a universal stop-latency guarantee.

Final R1 verification: `cmake --build build --target build-tests --parallel 2`
passed; all five focused lifecycle/viewer/document tests passed; all four GUI
tests passed under Xvfb/Mesa, including real Qt Quick 3D rendering and the
failure-artifact self-check, with no rendering skips. Final
`ctest --test-dir build --output-on-failure -j 2` passed **67/67** in 409.54 s.
Hosted GitHub Actions execution remains unverified. Stop/reset/close join after
the current element step returns; no universal latency bound is claimed.

## Release execution audit — 2026-10-20

This section supersedes the earlier work sequence where priorities differ.
The authorized scope is `UI_AND_SYM_IMPROVMENT_PROMPT.md`; the complete workflow
is Open → Edit → Configure → Run → Observe → Compare → Save → Reopen.
Do not change implementation or licenses until this documentation increment is committed.

### Reproducible starting point

- Revision: `0e8f695` (Add PLL and AM/FM/RMS measurement examples).
- Initial tracked tree clean; two untracked improvement prompts are user inputs.
- Existing `build`: Debug, Studio ON, `/usr/bin/c++` GCC 16.2.1,
  CMake 4.3.0, system Qt under `/usr/lib64/cmake/Qt6`.
- `ctest --test-dir build -N`: 59 registered tests. Registration is not execution.
- Baseline command: `cmake --build build --target build-tests --parallel 2`,
  then `ctest --test-dir build --output-on-failure -j 2`.
  Build/test results will be recorded below when complete.
- DISPLAY=:0 and WAYLAND_DISPLAY=wayland-0 are set; this does not prove an
  accessible GPU/session. Real rendering and native Windows remain unverified.
- Workspace writes cover `s7l2` and `/tmp`; repository root, `doc`, and `s7py`
  changes require filesystem approval. No vendored/submodule edits authorized.

### Final G1 verification — 2026-10-20

- `cmake --build build --target build-tests --parallel 2`: passed; focused
  workflow-target rebuild after diagnostic refinement also passed.
- GUI CI invocation: 4/4 passed in 2.81 s; no rendering skips. The refined
  failure-artifact/watchdog self-check separately passed in 2.48 s.
- `ctest --test-dir build -LE gui --output-on-failure -j 2`: **61/61 passed**
  in 429.33 s, outside socket-restricting sandbox. Together with the GUI suite,
  all 65 registered tests have passing evidence.
- Python syntax, root workflow YAML/source directory, actual CTest/JUnit summary,
  and positive/optional-skip/mandatory-skip/missing-test report policies checked.
- Headless Release configure passed; test flags contain `-DNDEBUG ... -UNDEBUG`.
  This verifies assertion configuration, not execution of a local Release suite.
- No push or hosted Actions run performed. No physical-GPU, native Windows,
  long-duration UI performance or complete-release readiness claim is made.
- G1 is locally complete. The remaining L1/N1/U1/P1/A1 work and measured O1 operational envelope are still tracked;
  contributor relicensing permission remains unresolved, and the broad release
  acceptance criteria have not been marked complete.

### Execution evidence

- Documentation-first commit: `b736b5e`.
- Baseline build-tests completed successfully (59 registered tests). Full CTest
  was started before implementation; Studio was subsequently rebuilt during
  its long numerical cases, so the eventual suite is an integration check,
  not an immutable-revision baseline run.
- D2: document snapshots include scene/probes/layout, deployment fields, nested
  schemas and persisted `studio_run` duration/clear-trace configuration. Shared
  Save/Discard/Cancel guards cover open/recent/demo/remote replacement, diagram
  close and application close. Failed Save cancels replacement. Replacement
  resets/joins the old embedded worker, including paused runs.
  Focused document regression passes cancellation, discard, save/reopen, undo
  cleanliness, injected failed Save, close-save, running/paused replacement and
  mailbox clearing. Nested-schema regression verifies dirty state and prompting.
  Serialization comparison is authoritative; modification indicators are debounced.
  R1 now also invalidates callbacks with a generation counter before disconnect,
  worker join/reset and reconnection. Start/reset/document replacement share the
  invalidation path. A deterministic queued-worker sentinel test verifies retired
  logs/completion/progress/sample callbacks cannot update the new document.
  `s7studio_document_safety`, `s7studio_scope_lifecycle`, `s7studio_scope_run`
  and `s7studio_recent_diagrams` all pass (4/4). Broader failure/observer stress
  and measured stop latency remain release acceptance work.
- D1: shared atomic writer used by diagram saves and schema-library exports;
  complete writes and commit checked, direct truncating fallback disabled.
  `s7studio_atomic_file` passes: replacement/reopen, invalid destinations,
  Linux child-process file-size-limit write failure preserving existing bytes,
  and successful recovery. Studio rebuild passed. Actual disk exhaustion and
  platform-specific filesystem semantics remain acceptance work.

- Integration run: 53/59 passed in the sandbox, including the long coupled
  pendulum Studio case (444.95 s). Five local-socket tests failed on sandbox
  restrictions; all five passed when rerun outside it. The recent-menu regression
  needed its title expectation updated for Qt's modification marker; it now passes.
  This is an integration check across successive builds, not a pristine baseline.

### Automated GUI suite — user-directed priority, 2026-10-20

Implement G1 before the remaining broader release phases. This is a testing
increment, not a claim that the release audit is finished.

| Task | Priority/category | Files / current behavior | Implementation / dependencies / risks | Tests and acceptance | Status |
|---|---|---|---|---|---|
| G1 | P1 missing coverage / release infrastructure | `s7studio/tests`, viewer QML, CMake, root CI; existing tests mostly custom checks/offscreen | Add Qt Test action/object workflows and Qt Quick Test of production viewer scenes; stable object names for dialogs/actions; Linux Xvfb wrapper with bounded timeouts, logs, failure screenshots, XML; probe OpenGL capability and explicitly skip genuine unavailable rendering; root CI provisions GCC15+/Qt6.9+/Xvfb/Mesa and uploads artifacts | Unattended edit/parameter/save/open/run/pause/stop/reset/viewer workflows; QML interaction; actual Quick3D pixel evidence under usable OpenGL; report unavailable display/3D as skipped, never passed; inject failure to verify artifacts; integrate CTest and hosted workflow | Implemented and verified locally (4/4); hosted execution unverified |

G1 evidence: `cmake --build build --target build-tests --parallel 2` passes.
The exact CI GUI invocation (`ctest --test-dir build -L gui --output-on-failure
--output-junit gui-results.xml -j 2`) passes 4/4 in 2.81 s. Qt Test reports
10 passed, 0 failed, 0 skipped; Qt Quick checks actual red curve/green trajectory
pixels, zoom/pan and camera changes. Mesa llvmpipe renders through OpenGL 4.6.
The intentional failure self-check verifies fresh screenshots, document JSON,
diagnostic logs, object hierarchy and failed JUnit; missing Xvfb verifies exit
77/skipped JUnit. A deliberately blocked child also verifies watchdog termination
and pre-termination screenshot/diagnostics/process-failure reporting.
`report_gui_tests.py` consumes the actual CTest report successfully; positive,
optional-render-skip, mandatory-skip and missing-test cases have verified exit
statuses. Root workflow
YAML parses and uses the s7l2 source directory with headless/Studio matrix;
actual hosted execution has not run. Test targets now undefine NDEBUG so assert
checks remain active in optimized builds. A separate headless Release configuration confirms `-DNDEBUG` is followed by
`-UNDEBUG` on smoke-test compile flags; no local Release suite execution is claimed.

Order: commit this documentation → test infrastructure/object identities →
Qt Test workflow → Qt Quick Test/viewer rendering → local Xvfb run and artifact
failure verification → root GitHub Actions integration → update evidence.
Do not automate by desktop coordinates when actions, widget objects, model
indices or QML objects exist. GPU tests must inspect rendered content, not only
QML load status. Hosted workflow execution is separate from local validation.

### Ordered tasks and acceptance

All rows require a separate logical commit, relevant tests, and synchronized
issue status. Status starts **pending audit** unless evidence is stated.

| ID / priority / category | Modules and current behavior | Required behavior / implementation | Dependencies and risks | Regression tests / acceptance | Status |
|---|---|---|---|---|---|
| L1 P1 release infrastructure / rights decision | Root LICENSE says GPLv2; root README grants version 2; `s7spectral` is described as third-party; history contains multiple contributors | Audit tracked ownership and notices, inventory dependencies/assets; migrate verified S7 material to GPL-3.0-or-later, official license plus explicit or-later grant, module notices/SPDX, package notices and automated metadata check; user output is not automatically GPL | First documentation commit; root write access; obtain missing contributor permission rather than assuming ownership; preserve QSchematic/GPDS/Tabler and other vendor licenses | Scan all tracked first-party metadata and modules; CI rejects stale declarations; inspect packaged notices | Relicensed 2026-10-21 (ADR-019, `LICENSING.md`): runtime/SDK LGPL-3.0-or-later, Studio/s7ctl GPL-3.0-or-later with plugin permission, other code GPL-3.0-or-later, docs CC-BY-4.0, examples/fixtures CC0-1.0, s7spectral MIT-0. Maintainer is the copyright holder; the one external contributor gave permission; s7spectral is first-party. SPDX headers + `REUSE.toml`; `reuse lint` passes locally. Packages ship S7's licences through `tools/package_notices.py`. Open: CI `reuse lint` job; bundled-library notices are P1 |
| D1 P0 confirmed defect | `s7studio/src/main_window.cpp` saveDocumentToPath and library export truncate destinations and ignore write results | Shared atomic QSaveFile writer, verify complete write and commit, disable direct fallback; update path/success state only after commit | Documentation commit; filesystem failure injection must work as root too | Existing-file preservation on open/write/commit failure, successful replacement/reopen and invalid path | Implemented and focused regression passed |
| D2 P0 confirmed defect / coverage | MainWindow closeEvent has no unsaved prompt; other replacement paths and nested tabs need audit | Track persisted document state across scene, parameters, probes, nested schemas and run config; central Save/Discard/Cancel guard for every replacement including remote loads and recent files | D1; nested commits and undo cleanliness must agree; failed Save cancels transition | Each dialog choice, nested edits, open/new/recent/close/load, failed save retains dirty state, full config round trip | Implemented; document safety and nested regressions pass; wider interaction coverage pending |
| R1 P0 lifecycle complete | LocalSchemaRunner, ModelRunController, MainWindow, ViewerManager, mailbox already have lifecycle tests | Audit Idle/Prepared/Running/Paused/Stopping/Stopped/Error; stop/join on replacement; generation isolation for queued work, immutable active config, actionable failures | D2; Qt queued callbacks and shutdown ordering | Deterministic start/stop/reset/rerun, document replacement running/paused, close, stale callbacks, observers added/deleted, edit-next-run, worker recovery | Implemented; deterministic runner/window and GUI checks passed |
| O1 P0 design decision / confirmed limitation | Worker mailbox drops oldest; ScopeRecording records only delivered values (`SCOPE_RESULTS.md`) | Authoritative worker-side bounded recording before lossy preview; choose synchronous disk/backpressure policy; propagate I/O failures, step/run IDs and completeness; preserve binary64 values | R1; disk latency reduces throughput; no unbounded buffers or claims of lossless preview | Deliberate preview overflow still yields complete authoritative results; disk-full/write failure produces incomplete status; unsupported complex probes rejected before execution | Implemented: separate sources and authoritative pre-preview capture; 69-test suite passed; final comparison refinement passed 11 affected + 4 GUI tests |
| N1 P1 incomplete coverage | s7core rate inference and numerical harnesses exist | Audit first/last samples, fractional intervals, finite durations and pause/rerun; derive model time from step/rational rate without cumulative quantization; capture model/effective params/rate/seeds/version/run ID | R1/O1; preserve causal ordering and existing integrators | Independent oscillator, pendulum, RC/RLC/mass-spring/control references and refinement; limited Van der Pol/Lorenz metrics; same saved model headless/Studio; physical-time vs discrete sample events | N1a timing/parity, N1b immutable provenance/replay and N1c linear analytical/Studio matrix validated; wider nonlinear/control references and external input histories remain |
| U1 P1 usability / coverage | Diagram scene, schema tabs, parameter metadata and Qt viewers exist | Audit deletion/probe undo, wires/types/layout, copy/paste/navigation and serialization; reusable metadata-driven typed parameter editor with advanced JSON; viewer axes/units, cursors, legend, comparison/run labels and warnings | D2/R1/O1; no vendor patches or GUI redesign | Diagram round trips/undo, probe cleanup, metadata validation, all three viewers multi-instance pause/hide/reopen/reset/export, long-run cleanup | U1a editing, typed properties, viewer interpretation and screenshot audit implemented; further interaction/accessibility/performance acceptance remains |
| C1 P1 release infrastructure | Workflow is `s7l2/.github/workflows/ci.yml`, uses repository-root cmake and no provisioning | Root workflow with source `s7l2`, explicit supported compiler/dependencies; headless/Studio/numerical/document/lifecycle/license jobs, appropriate sanitizers and cross-Windows/Wine; Release assertions active | L1; root write approval; runners/toolchain availability must be verified | Build build-tests before CTest; validate actual hosted runs, never count unavailable jobs as passed | Hosted headless and Studio jobs passed on 3026b84 and 89628cc; focused local ASan/UBSan passed; full sanitizer and Windows validation open |
| P1 P1 release infrastructure | Linux/Windows package scripts exist | Audit arbitrary-cwd launching, installed plugins/examples/QML resources, Qt runtime and license notices; actionable dependency errors | L1/C1, packaging tools and real display | Installed-package workflow from `/tmp`, distribution notice inventory; native Windows separate from Wine | Linux relocation/notice audit and 12 isolated package checks passed; release notice review and native Windows remain open |
| A1 P1 incomplete coverage | Automated references and viewer regressions cover selected cases | Execute four acceptance guarantees: document safety, numerical correctness, run/sample isolation and operational readiness; classify bundled models validated or experimental | All above; real GPU/native Windows are external evidence requirements | Representative linear/nonlinear full workflow; measure hardware, model rate vs throughput, UI responsiveness, memory/disk growth, stop latency, high DPI and long-run behavior | Linux 73-test Release suite, isolated package and 90-second operational measurement passed; broader acceptance remains open |

Order: documentation baseline commit → L1 audit/migration where rights permit →
D1 → D2 → R1 → O1 → N1 → U1 → C1 → P1 → A1.
A licensing blocker does not block unrelated P0 fixes. C1 preparation may proceed
after P0 correctness work while external rights are outstanding.
No global solver, distributed scheduling expansion, additional integration methods,
remote observation, hardware, multi-rate or checkpointing is added by this plan.

## Next release: dependable local simulation in Studio

**Decision: 2026-10-19. One release goal: good simulation quality in `s7studio` with its bundled runtime.** The target user builds or opens a causal model, runs it locally, and can trust the computed values, model time, plots, and recorded results. No release number or delivery date is assigned here.

“Bundled runtime” means Studio's existing in-process `LocalSchemaRunner`, using `s7core` validation, compilation and `NodeRuntime` on a worker thread. Local simulation must work without starting a separate `s7runtime` service, discovering nodes, provisioning certificates, or deploying a diagram. Core/runtime logic stays headless and independently testable; Studio configures execution and observes it. This release does not introduce a second numerical engine in the UI.

### Release boundary

In scope: single-node, single-rate simulation; correct rate/`dt` handling; causal state publication; the accuracy and documented limitations of existing integrators; deterministic run controls; reliable Scope and phase views; observation delivery, recording/export, bounded resource use and useful errors. Fix editor, loader, plugin or packaging problems when they prevent that local workflow.

Deferred: distributed scheduling/deployment expansion, remote observation, hardware acquisition, multi-rate execution/resampling, snapshots/restore, production credential/operations work, broad architectural refactoring, and new integrators or a systematic nonlinear integrator comparison. Existing network features remain available, but expanding them is not a next-release objective. Revisit deferred work after this release's acceptance criteria pass; a demonstrated local simulation blocker can bring a specific item forward.

### Current evidence and remaining work

| Area | Implemented evidence | Still required for release |
|---|---|---|
| Numerical execution | Mandatory rate validation; scalar analytical/convergence harness; Euler/symplectic oscillator checks; nine coupled-pendulum diagrams with continuous/discrete references at three rates; implicit trapezoidal stage; reference-backed nonlinear pendulum over 100 s at 1/10 kHz (Symplectic/Trapezoid pass; Euler fails accuracy as expected) | Reference-backed acceptance for the supported example/configuration matrix; model-time/state alignment and timestep refinement checks |
| Studio delivery and views | Worker-thread execution and bounded sample mailbox; `s7studio_scope_view`, `s7studio_scope_run` and `s7studio_scope_lifecycle` regressions (two-channel pause/resume, stop while paused, reset/rerun, mailbox overflow and temporary-directory failure) | Broaden phase-view controls, storage-write failures, long-run UI responsiveness and shutdown coverage |
| Scope lifecycle | Uniform preview stride; recording starts before opening a view and survives hiding/reopening; Auto Y preserves the time range; all 6.6 million nonlinear-pendulum CSV rows preserve exact double values; saved `.s7scope` comparisons, snapshot/append fidelity and disk-backed imported zoom checks | Actual disk exhaustion, large-import/export responsiveness, per-comparison controls and wider platform coverage |
| Reproducibility | Headless diagram harness, Studio oscillator test, and coupled-pendulum bundled/headless sample comparisons | Compare the same saved model/configuration across both paths; document supported platforms and resource envelope |

These checks establish specific behavior, not general numerical correctness. In particular, the Scope run regression's ten-second sine comparison at 1/10/100 kHz is a rendering/delivery regression with a tolerance; it does not prove long-term energy conservation or validate all integrators.

### Work sequence

1. **Establish the local simulation baseline.** Reproduce failures from saved diagrams through the complete open → configure → validate → run → observe → export workflow. Start with `examples/2int_osc.s7diagram` at 1, 10 and 100 kHz over the same model duration. Record effective parameters, method, initial conditions, rate, step count, first/last timestamps, probe mapping and any dropped samples. Distinguish numerical-method limits from scheduling, timestamp and display defects before changing semantics.
2. **Validate values and model time together.** Extend the existing headless harness and Studio tests with independent references and per-case tolerances. Cover scalar dynamics, the two-integrator oscillator, an energy-bounded oscillator realization, and representative coupled/nonlinear models selected from the bundled examples. Verify the first published state, its step/time label, the final sample and convergence as `dt` decreases. Characterize forward Euler's known oscillator drift rather than requiring impossible exact conservation. Preserve causal-memory publication order; any runtime change needs a core smoke regression.
3. **Make observation trustworthy throughout a run.** Test full-run and zoomed views, multiple channels, open-before/open-after-run, close/reopen, pause/resume/stop/reset/rerun, clear and overlay behavior, CSV export and disk failures. Preview decimation must retain the model-time extent and useful curve detail. Observation overload must be visible and bounded; a disk recording of delivered samples cannot recover samples dropped before delivery. Select and test a delivery/recording policy for the supported rate/duration/channel envelope before claiming full-resolution capture.
4. **Make the result reproducible and releasable.** Exercise Studio-enabled builds and the local workflow in CI, with explicit dependencies and a supported compiler; keep headless numerical gates too. Check worker/UI lifetime and shutdown under load. Record reference metrics and reviewable traces/plots for the release matrix, then smoke-test the packaged Studio with its required local runtime dependencies/plugins. State which platforms were actually tested; Wine does not establish native Windows support.

### Acceptance criteria

The next release is ready when all of the following have evidence:

- A fresh Studio installation can open, configure and run the supported examples locally without an external service. Every bundled example is classified as **validated within a stated envelope** or **experimental with stated limits**; no illustrative nonlinear/chaotic diagram is advertised as reference-validated merely because it draws a plausible curve.
- Each validated numerical case records equations, parameters, initial conditions, method, effective `dt`, duration, independent reference, tolerances and results. State error, convergence and applicable amplitude/phase/energy behavior pass within that envelope. The suite distinguishes known method instability from implementation defects.
- The oscillator's 1/10/100 kHz runs preserve the same requested model duration, correct step/time alignment and full plotted time extent. Display sampling does not change model values or timestamps. Observation enabled/disabled and different window states produce the same runtime trace at matching steps.
- Run controls and view lifecycle tests pass without stale or mixed run data. Scope fit/zoom/pan and Auto Y preserve the selected time semantics. Exported values/timestamps match the recorded trace, and all capture loss or storage failures are reported rather than presented as complete data.
- A documented rate/duration/channel/graph-size envelope has measured throughput, bounded preview/queue memory and disk-use behavior. UI responsiveness, pause/stop latency and clean shutdown have measured bounds on the stated test machine. No universal throughput or real-time guarantee is inferred from discretization frequency.
- CI runs the named numerical and Studio integration gates; the release build passes them and a manual packaged-workflow check. Remaining local-simulation defects have explicit dispositions against these criteria.

Detailed test design: [`VISUAL_NUMERICAL_TESTS_PLAN.md`](VISUAL_NUMERICAL_TESTS_PLAN.md). Current gaps: [`OPEN_PROBLEMS.md`](OPEN_PROBLEMS.md). Current UI/runtime behavior: [`STUDIO_DIAGRAM_EDITOR.md`](STUDIO_DIAGRAM_EDITOR.md).

## Next coding session TODO: educational examples

Build the educational examples below as explicit analog-computer graphs under
`examples/`, using the existing primitives and reusable schema elements described
in [CONTROL_ELEMENTS.md](CONTROL_ELEMENTS.md). Schema is element: reusable
controllers and model components belong in the separate schema-element resource,
not new hard-coded compound elements. Do not introduce a global numerical solver;
independent RK4 or other solvers may provide test references only.

- [ ] **First- and second-order response:** RC charging/discharging, RLC resonance,
  and a mass–spring–damper. Demonstrate step/impulse response, damping regimes,
  resonance, and electrical/mechanical analogies; reuse the existing oscillator
  and pendulum examples where appropriate.
  Initial diagrams and headless checks: [linear laboratory](../examples/linear/README.md).
- [ ] **PI/PID control laboratory:** a first-order plant and a DC motor with load
  torque changes. Compare open loop, P, PI and PID; demonstrate saturation,
  anti-windup, derivative filtering, and reference/disturbance responses using
  the reusable controllers.
  Initial diagrams and headless checks: [control laboratory](../examples/control/README.md).
- [ ] **PWM power conversion:** extend the existing `pwm_pid_rlc.s7diagram`
  example into a documented buck-converter exercise with changing load,
  startup, output ripple, and load-step recovery. Check the switching topology
  and energy balance before treating it as a physical converter model.
  Initial diagrams and headless checks: [synchronous buck](../examples/buck/README.md).
- [ ] **Relay control:** thermostat/heater with thermal inertia and hysteresis;
  show switching thresholds, overshoot, and the resulting limit cycle.
  Initial diagrams and headless checks: [thermostat laboratory](../examples/relay/README.md).
- [ ] **Nonlinear oscillation:** Van der Pol oscillator, with time traces and
  phase portraits showing convergence to a limit cycle.
  Initial diagrams and headless checks: [Van der Pol laboratory](../examples/van_der_pol/README.md).
- [ ] **Chaos:** Lorenz attractor, with a three-dimensional phase trajectory and
  nearby initial conditions. Document timestep sensitivity and avoid claiming
  long-term trajectory agreement as an accuracy criterion.
  Initial diagrams and headless checks: [Lorenz laboratory](../examples/lorenz/README.md).
- [ ] **Population dynamics:** Lotka–Volterra predator–prey model, with time
  traces, phase portrait, equilibrium, and parameter/initial-state variations.
  Initial diagrams and headless checks: [predator–prey laboratory](../examples/lotka_volterra/README.md).
- [ ] **Synchronization and measurement:** a phase-locked loop built from VCO,
  phase detector and loop filter; demonstrate acquisition, tracking, phase
  error, and frequency measurement. Use phase wrapping and the frequency meter
  where their documented contracts fit the model.
  Initial diagrams and headless checks: [PLL laboratory](../examples/pll/README.md).
- [ ] **Signal measurement:** AM/FM generation and detection, plus RMS
  measurement. Compare known source amplitude/frequency with measured outputs
  and explain smoothing/transient error.
  Initial diagrams and headless checks: [AM/FM/RMS laboratory](../examples/signal_measurement/README.md).

For each example, include a Studio-editable `.s7diagram` and a short README with
equations, units, parameters, initial conditions, suggested experiments, probes,
execution rate, duration, expected behavior, and numerical limitations. Add
headless reference checks appropriate to the model, check timestep refinement,
and manually verify open → run → observe → edit nested component → rerun in
Studio. Reuse existing examples instead of duplicating them; mark each model
experimental until its stated validation envelope is checked. Start with the
linear-response and PI/PID exercises before the nonlinear and synchronization
models. This checklist records the next session's example work; it does not make
the entire catalog a next-release acceptance gate.
The linked laboratories have automated checks within their documented envelopes;
their manual Studio workflow and edited-instance checks remain outstanding, so
their checklist items stay open.

## Later work — retained backlog, not next-release gates

The phases below retain the broader project direction. Their numbering is a backlog reference, not a requirement to finish them for the Studio simulation release. Only the local validation, CI and lifecycle work selected above is part of this release; the complete cross-language, distributed, security and operations programmes remain deferred.

## Phase 2: Make validation repeatable

- Extend the existing `.github/workflows/ci.yml` job rather than treating CI as absent: add an explicit dependency-install step (Boost, OpenSSL, zlib, and Qt6 or `-DS7_BUILD_STUDIO=OFF`) and pin a toolchain that satisfies AGENTS.md's GCC 15+/Clang 22+ requirement, then confirm the job actually passes against that pinned configuration.
- Add a compiler/dependency matrix (supported Linux combinations, headless and Studio builds).
- Add a Windows cross-build plus Wine job, with a clear note that Wine is not native Windows validation.
- Decide whether to move `tools/test_two_node_deployment.sh` (registered as ctest's `s7_two_node_deployment`) under `s7tests/`, and keep module maps consistent with that decision. Extend that scenario with a standalone clean-shutdown case and a multi-cycle restart/recovery case.
- Promote fixture currency, plugin SDK smoke, and Python decoder checks to required (non-skippable) CI checks, and make cross-language fixture conformance a release gate.
- Add sanitizer jobs selectively: ThreadSanitizer for transport/runtime concurrency, AddressSanitizer/UBSan for core, parsing, and lifecycle paths. These have been run ad hoc (see resolved findings in git history) but are not a standing gate.
- Perform the concurrency/lifecycle audit called for in Open Problems P1 (runtime graph replacement, stream ownership, endpoint shutdown, Studio worker/UI handoff) before adding scheduler or recovery features in Phase 5.

**Addresses:** Open Problems P1 (validation/CI/whole-system-testing items).
**Completion criteria:** a clean checkout has reproducible build/test instructions and CI reports the same named gates; the end-to-end behavior is covered without using a developer's home directory or shared state; the concurrency audit's findings are either fixed or tracked as new Open Problems entries.

## Phase 3: Reduce change risk in high-churn code

Refactor by behavior boundaries, preserving public APIs and adding tests before moving code:

- Split `transport.cpp` into endpoint lifecycle, connection/session handling, TLS/identity, and stream routing units.
- Split `control_plane.cpp` into request validation/dispatch, deployment transaction/persistence, stream management, and response serialization units.
- Split `runtime_service.cpp` into JSON loading, graph/deployment lifecycle, scheduling/serve loop, and clock/state handling units.
- Break `main_window.cpp` and `diagram_scene.cpp` into UI actions/controllers and scene/model operations, leaving thin coordination entry points.
- Decide whether schema-backed element libraries should move from plain JSON files to an archive/zip format (open preference from the earlier status plan, never acted on); only build it if still wanted.

Do not split solely to reduce line counts. Each extraction should establish a testable boundary, make ownership explicit, and reduce the number of reasons a file changes.

**Addresses:** Open Problems P2.
**Completion criteria:** each module has a clear ownership map; high-risk behavior can be tested without launching all of Studio or a real network; no refactor changes externally visible protocol semantics.

## Phase 4: Close the gap between an MVP demo and an operated system

- Finalize a production credential story: enrollment authority, certificate profiles and role OIDs (replacing the current development placeholder OID), rotation/revocation, key protection, bootstrap trust, and recovery after loss of the control Studio. Keep this work separate from test fixture generation.
- Specify resource limits and operator behavior: maximum graph/schema and library sizes, plugin trust policy, stream buffer/log sizing, persistent state backup/restore, disk-full behavior, service supervision, and upgrade/restart behavior.
- Decide whether crash recovery is in scope. If it is, define durable payload retention and acknowledgement semantics before implementation. If it is not, surface the expected data loss and required operator action consistently in CLI, Studio, and docs.
- Add operational traces/metrics or structured logs with stable identifiers for deployment revision, node identity, stream ID, and fault; ensure secrets and private material are excluded.
- Perform an independent security review of TLS setup, certificate parsing and authorization, plugin loading, file/path handling, canonical JSON and digest validation, and deployment persistence before production use.
- Publish the operational profile that turns "lossless"/"real time"/"distributed" into supported graph size/rate, latency and clock assumptions, expected hardware, restart behavior, and resource ceilings.

**Addresses:** Open Problems P1 (production-readiness items).
**Completion criteria:** a documented threat/operations model and runbook; a credential lifecycle that can be exercised from provisioning through rotation; explicit behavior for all restart and storage failure paths.

## Phase 5: Evolve the language-neutral model deliberately

After the MVP boundary is stable, choose one major model capability at a time:

1. Add an acquisition-time source and validate `SOURCE_TIME` from a real or deterministic test source.
2. Decide if relay semantics are required; specify identity, buffering, latency, and failure propagation before adding relays.
3. Define distributed scheduling and coherence domains with formal invariants and executable examples before introducing `NetRuntime` or synchronized multi-node execution changes.
4. Add snapshots/restore only after element state serialization has a versioning and validation contract.
5. Add remote observation through explicit network/data contracts that preserve local observation's non-invasive behavior.
6. Give `s7spectral` frame/block signal types so `MagnitudeSpectrum`/`PowerSpectrumDB` can come back.
7. The harder, deliberately out-of-scope half of `../../doc/S7_Sample_Rate_Model.md` (the inference/validation half is already implemented and required by the local simulation baseline): how a genuinely multi-rate graph actually *executes* — single global tick vs. per-rate-domain scheduling vs. a hardware element buffering internally like `NetSource`/`NetSink` — once the rate-changing element it defines actually exists and something needs more than one rate domain in one running graph.

These items should not be bundled together. Each changes either semantics, persistent state, or protocol behavior and needs a design decision, conformance fixtures, and a vertical end-to-end demonstration.

**Addresses:** Open Problems P3.
**Completion criteria:** each capability has its own design record, conformance fixtures, and a working vertical demo before the next one starts.

## O1a — validate observation separation before completeness

User-approved order: separate observation data ownership from presentation and
validate it first; then address authoritative capture before the lossy mailbox.
Baseline: `0de5bc6`, 67/67 tests passed; user prompt files remain untracked.

| Task | Current / required behavior | Files / approach | Risk and acceptance | Status |
|---|---|---|---|---|
| O1a P0 architectural prerequisite | ViewerManager creates hidden windows to retain samples; Scope owns save/export data. Data must survive without any window. | Add QtCore-only observation source with bounded memory and disk-backed paged reads, run/channel identities and explicit delivery-loss/storage status. Manager owns source; viewers attach read-only and consume bounded pages. Keep Qt Graphs/Quick3D. | No capture-completeness claim: input still follows mailbox. No unbounded replay or hidden window creation. Test zero-window capture, paging/spill/failure, view deletion/recreation, clear without deleting results, source-backed export and run isolation. | Implemented; six focused tests and four Xvfb GUI tests passed, including Quick3D; 69-test suite passed; final comparison refinement passed 11 affected + 4 GUI tests |
| O1b P0 completeness | Data discarded before UI delivery is still unavailable. | After O1a passes, move authoritative append to worker before preview; bounded storage/backpressure and explicit failure policy, typed probe validation. | Independent capture and preview status; deliberate preview overflow preserves full results; injected storage failure marks incomplete and stops/alerts under documented policy. | Implemented after O1a validation; overflow/I/O failure/probe-validation regressions passed; 69-test suite and final 15 affected/GUI checks passed |

O1a data contract: append is owned by the observation host, never a viewer;
viewers hold a const source, explicit input bindings, and a presentation cursor.
Source reads return bounded pages in production order with run, channel, step
and model time. Clearing/destroying a view must not delete source data. New runs
replace the active source; retained handles identify the previous run. Existing
Scope recording remains a disposable presentation/import/export projection
while raw delivered samples belong to the independent source. Disk/history reads
are bounded per UI turn; full save/export is explicit work, not a repaint path.
No runtime/core semantic changes, vendor edits or licensing changes belong here.

O1a validation: build-tests passed; source test uses QCoreApplication only.
Focused source/viewer/window/Scope/document tests passed 6/6; Xvfb GUI tests
passed 4/4, including actual Mesa Qt Quick 3D and diagnostic artifact self-check.

### O1b capture policy (after validated O1a)

Authoritative append belongs to LocalSchemaRunner's stepping thread, immediately
after a completed runtime step and before optional preview-mailbox publication.
The same window-independent source becomes the read-only source for all viewers
and result services; UI delivery must not append a second copy. Pre-register and
validate every selected probe before stepping; reject complex probes and invalid
ports/names with actionable preparation errors. Scalar conversion to stored
binary64 is explicit, preserving F64 exactly and identifying other source types.

Policy: bounded in-memory records, then synchronous temporary-file writes on the
worker. Storage backpressure reduces achieved wall-clock throughput, never the
model rate or causal order. Storage create/write/final-flush errors fail the run
and retain a visibly incomplete prefix; do not silently continue unrecorded.
Run status distinguishes active/completed/stopped/failed, capture failure and
preview loss. Temporary history is not crash-durable; explicit atomic save is
the persistence boundary. No unbounded queues or extra recording thread.

Tests: withhold preview draining until actual mailbox overflow and verify exact
full-source count/step/value and source-backed saved results; inject mid-write
and final-flush failures, verify incomplete metadata and recovery; reject complex
probes before the first runtime step; preserve pause/reset/generation guarantees.
Rebuild build-tests and run focused/GUI checks before the complete suite.

O1 review refinement: preserving previous traces must also retain source handles
when no viewer exists. Store retained run handles/labels in the observation
session, pass explicit per-view comparison bindings, and project on opening.
Add a no-window rerun/comparison regression. This preserves the existing checkbox
contract after removing hidden recording windows; no capture-policy change.

Final O1 verification — 2026-10-21: build-tests passed. The full CTest suite
passed 69/69 in 442.70 s, including headless numerical references and Xvfb GUI
rendering. Following the final session-owned comparison refinement, rebuilt
build-tests and reran all 11 affected source/capture/runner/window/viewer/Scope/
document/end-to-end tests (18.89 s), plus all four GUI tests (4.15 s), all passed.
Qt Quick 3D rendered under Mesa; no rendering skips. The independent source test
links QtCore/QtTest without QtGui/QtWidgets. Actual disk exhaustion, native GPU/
Windows, measured performance envelope, crash durability and asynchronous bulk
I/O remain unverified or deferred; injected I/O failures are identified as such.


O1b completion validation (2026-10-21): rebuilt `build-tests` successfully and
ran the complete CTest suite: **69/69 passed**, 428.77 seconds wall time,
including all four GUI tests and Qt Quick 3D rendering. Focused observation
source/capture/viewer tests also passed 3/3. The no-window retained-comparison
regression passes. `git diff --check` is clean. This completes O1 capture
correctness; measured throughput, asynchronous bulk operations, physical-GPU
acceptance and crash durability remain outside this increment.

## N1a — model time and local execution parity (2026-10-21)

Scope: audit/fix fractional timestep timestamps, first/last samples and finite
run boundaries; verify pause/resume/rerun step alignment and execute the same
saved diagram through the headless harness and Studio's embedded runner.

Confirmed defects: Rate::from_seconds and LocalSchemaRunner round each period
to whole nanoseconds; NodeRuntime multiplies that rounded period with unchecked
signed arithmetic. ModelRunController rounds duration/dt to the nearest tick,
which can exceed a finite requested duration. The headless diagram harness
also installs a rounded period.

Implementation contract: retain a reduced rational rate derived from configured
dt without whole-nanosecond period quantization. Compute each absolute timestamp
from the successful-step index, rounding only the final nanosecond representation;
provide seconds from the same rational base for recording. Reject timestamp
range overflow explicitly. Preserve causal ordering/state publication: step 1
is recorded at dt, and causal-memory outputs are the initial published state
(the update staged during that step appears on step 2). No step-zero or extra
terminal sample is added. Finite durations include only complete steps at or
before the requested end, including zero steps when dt exceeds the duration.
Pause consumes no model steps; rerun creates a new run and restarts at step 1.

Validation: module smoke coverage for fractional rates and timestamp overflow;
deterministic gated lifecycle coverage; finite-boundary edge cases; shared
saved linear/nonlinear diagrams with every sample's step/time/value compared
between the headless harness and Studio. Run relevant tests, then build-tests
and the complete suite. Distributed pacing/wire-period changes, solver changes,
full reproducibility metadata and extended numerical references are out of scope.
Status: complete for the four requested N1a items; validation evidence below.
This is a partial N1 increment.

N1a audit refinement: Schema::rate is intentionally only a fallback for unpinned
components; it does not prove that a requested run timebase matches pinned
integrators. Preserve that inference contract. Installing a runtime model-time
rate must check the compiled elements' declared rates and reject disagreement
before stepping. Add connected-pin sub-ns conflict coverage and a Studio
preparation regression for mismatched run dt vs integrator dt.


N1a validation: `build-tests` rebuilt successfully (GCC 16.2, Debug, Qt 6.11.2,
Linux). The final focused rate/model-time/Studio-window checks passed 4/4. The
complete 71-test run finished in 514.00 s with 70 passing, including the long
coupled pendulum, transport/deployment/SDK tests and all four GUI checks with
Quick 3D rendering. Its one failure was the old nonlinear-pendulum reference
converting integer ns to seconds and requiring exact equality against the new
direct rational-time seconds. Updated that shared reference to the same absolute
time API, retained exact timestamp/value and CSV round-trip checks, rebuilt all
three affected targets, and reran them: **3/3 passed** (65.38 s). Thus every one
of the 71 registered tests has passed against the final behavior; a second
complete suite was not run after the reference-only fix. `git diff --check`
is clean. Comment-only clarification of the harness's rate validation contract
followed its build; no executable behavior changed in that clarification.

Coverage: exact 44.1/48 kHz final timestamps, fractional-ns rounding, reciprocal
conversion agreement over 1,000 fractional intervals, invalid/range/overflow
errors, connected-pin conflicts, run dt/integrator mismatch rejection, initial
and final publication, finite truncation/zero steps, zero-duration Studio UI,
gated stateful pause/resume/rerun, and every saved-model sample through both
loaders/runners. These checks establish local same-platform equivalence for the
covered configurations, not cross-platform bitwise reproducibility. Broader
model/method references, complete run provenance, event-time configuration,
performance envelope, packaging and distributed rational-period timing remain
open. No integrator/state-publication rules or deployment wire periods changed.

## N1b — immutable run provenance and saved-configuration reproduction

Scope: attach the immutable requested/effective model, effective parameter defaults
and nested schema definitions, rational rate, initialization parameters, explicit
noise seeds, software/element versions and run identity to captured results.
Preserve provenance in saved results and show live/imported runs in a read-only
Run details dialog. Verify editor/configuration changes cannot mutate old results,
and reproduce deterministic sample values from configuration recovered from a
saved result, without using the edited diagram.

Approach: Studio-local preparation helper freezes the effective JSON before
compilation, using existing registry defaults/validation and embedding schema-backed
library definitions. Resolve the S7 White/PinkNoiseGenerator seed=0 convention to
an explicit nonzero seed before factories run, including nested schemas; retain
the original request separately. ObservationSource publishes provenance once,
before capture, and exposes copies through const handles. Store model JSON as
strings so Qt JSON never rounds uint64 model parameters. Hash canonical effective
JSON with SHA-256; capture project/build revision/compiler/Qt/platform and element
versions. Runtime run IDs remain generation identifiers; add a UUID for identity
across processes. State initialization remains defined by effective parameters
and the recorded software, not a checkpoint or a new solver.

Saved .s7scope v2 retains the provenance with a bounded 16 MiB JSON header (the
previous 64 KiB limit cannot hold realistic model snapshots); reject unsupported
versions and oversized/malformed metadata transactionally, without compatibility
shims. Raw/aligned observation CSV retains provenance in its metadata comment;
Scope interchange CSV stays numerical. Run details offers live and imported/
retained comparisons, including missing-provenance and incomplete states.

Acceptance: immutable metadata across edits/reset/rerun; exact integer/default/
nested-schema/seed round trips; large-header bounds and corrupt-metadata rollback;
UI details for live and imported results; deterministic linear and nonlinear
saved-config reproduction with identical step/time/value sequences; seeded S7
noise replay on the same software/environment. External I/O/plugin behavior and
cross-platform RNG/bitwise reproducibility require their own evidence and must
not be promised. Build relevant targets, run affected tests and broaden to the
full suite for shared observation/result contracts. No auto-execution on import.
Status: complete for the four requested N1b items; evidence below. Broader N1
numerical-reference and external-input work remains open.


N1b validation (2026-10-21): `build-tests` passed (GCC 16.2, Debug, Qt 6.11.2,
Linux). Seven affected provenance/source/capture/time/viewer/runner/window tests
passed 7/7. The full 72-test suite ran in 558.61 seconds: 71 passed, including
coupled/nonlinear pendulum, transport/deployment/SDK and all four GUI checks with
Quick 3D rendering. The one failure was a stale malformed-file fixture that
hardcoded version 2 as unsupported; version 2 is now the supported format.
Changed the fixture to use current-version+1, rebuilt its target and reran it:
`s7studio_scope_lifecycle` passed (1.90 s), retaining all rollback checks. Every
registered test has therefore passed against the final behavior; the entire
suite was not repeated after that test-only correction. `git diff --check` passes.

Coverage includes edit/reset/rerun isolation, const source handles and one-time
publication, exact uint64 JSON through binary files, default parameters, nested
library definitions after registry removal, generated and explicit White/Pink
noise seeds, single/multichannel deterministic linear/nonlinear saved-config
reproduction, exact step/time/value equality, read-only live/imported/missing
Run details, new-presentation metadata cleanup, >64 KiB metadata, 16 MiB bounds,
unsupported versions and corrupt-fingerprint transactional rejection.

Provenance is the effective constructor configuration plus declared element and
build identities, not a state checkpoint or captured external input stream.
Requested/effective JSON remains exact numeric text; the model SHA-256 identifies
configuration content and is not an authentication signature. S7 noise seed=0
is resolved explicitly in Studio before element factories/validation run. Saved
results require the new v2 reader; no v1 shim was added. Dirty-build source
changes and matching plugin implementations must be retained separately for
reproduction. Native GPU/Windows, cross-platform RNG/bitwise equivalence, broad
numerical-model acceptance and the measured operational envelope remain open.


## N1c — linear reference matrix and Studio parity (2026-10-21)

The [numerical matrix](NUMERICAL_VALIDATION_MATRIX.md) fixes parameters,
initial states, methods, rates, durations and tolerances for all ten bundled
linear diagrams. The existing analytical checks now share explicit closed-form
references with Studio tests and use rational model time. Twenty configurations
cover RC charge/discharge, RLC step/resonant drive and mass–spring–damper release.
Each configured diagram is saved once and independently loaded through the
headless loader and Studio document parser/controller/worker. Every recorded
channel is checked for exact path parity, state error, initial/final publication,
step/time alignment and complete capture; both rates must exhibit first-order
refinement. The matrix classifies the remaining bundled families conservatively.
External input histories and the wider nonlinear/control matrix remain open;
this increment introduces no solver or runtime behavior change.

N1c validation: both affected targets built; the two targeted CTest entries
passed in 17.35 s, including all 18 ModelTimeTest cases. Measured maximum
error was 0.0082783437 at 1 kHz and 0.0041311222 at 2 kHz (RLC resonance,
Euler); refinement ratios across all ten models/methods were 2.000287–2.003897.
All recorded channels matched exactly across paths. Full suite not repeated
for this test/document-only increment. Detailed results are in the matrix.


## U1a — editing integrity and visual engineering usability (2026-10-21)

The local visual audit (retained outside Git) records the production
application baseline, severity and proposed remedies before UI modifications.
Incremental captures and GUI runs cover probe/delete undo, clipboard subgraphs,
save/reopen, metadata-driven typed fields with advanced JSON and inline errors,
keyboard properties/editing/plot controls, simulation labels/model-time wording,
theme-aware menu/library text, units and visible run/completeness state. No
vendor sources or runtime numerical behavior changed. Unit annotations are
frozen in provenance and retained in saved Scope channel metadata.

This completes the four requested U1a items and the bounded heuristic review;
it does not close the entire U1 release gate. Human review of toolbar/legend
policy, physical DPI/legibility, assistive technology and keyboard-only wire
construction remains valuable. Broader operations/packaging/native platform
acceptance and long-run responsiveness remain separate work.

U1a validation: `build-tests` succeeded. All four GUI suites passed after each
substantial increment; screenshot review prompted theme-contrast and 3D-label
refinements. The broader affected Studio selection passed 28/28 in 41.50 s,
including all GUI/rendering, schema/document, routing/types, recording,
lifecycle, provenance and model-time checks. After final property type/default
safeguards and keyboard run/stop checks, rebuilt affected targets and passed all
six GUI/schema/document checks in 10.53 s. Full repository suite and the two
long Studio pendulum checks were not rerun. Actual production application
screenshots, severity and remaining human-review decisions are retained in the
audit directory. No release-ready/platform-accessibility claim is made.

Resumed-session verification (2026-10-21, revision `9b870c8`):
`cmake --build build --target build-tests --parallel 2` passed, followed by
`ctest --test-dir build --output-on-failure -j 2`: **72/72 passed** in
528.65 seconds. This includes both long Studio pendulum checks and all four
GUI suites. No implementation changes were made during this verification.
This closes the full-suite verification gap above; packaging, hosted CI,
platform/accessibility and broader release acceptance remain open.


## C1/P1/A1 — hosted CI, installed package and measured acceptance (2026-10-21)

The current bounded evidence and remaining release gates are in
[LOCAL_SIMULATION_ACCEPTANCE.md](LOCAL_SIMULATION_ACCEPTANCE.md). Raw logs,
packages, captures and JSON reports remain local; prompts and usability captures
are ignored and must not be staged.

C1: inspected actual GitHub Actions logs. The prior headless job passed, while
Studio failed two tests that incorrectly required lossless preview delivery.
Commit `3026b84` switches those tests to authoritative capture without weakening
value/step/CSV fidelity checks; preview loss is deliberately forced and counted.
Both affected Release tests passed locally. The rerun is
[39650453808](https://github.com/alukin/s7/actions/runs/39650453808): both
headless and Studio jobs succeeded. Hosted Studio passed 68 existing
regressions plus all four GUI suites. The subsequent package implementation
commit `89628cc` has a separate
[hosted run](https://github.com/alukin/s7/actions/runs/39652261660): both jobs
succeeded; Studio passed 69 existing regressions in 52.47 seconds and all four
GUI suites in 6.55 seconds. This verifies the implementation before the
subsequent license-only and final documentation updates.

P1: fixed executable-relative project-plugin discovery and packaged runtime
schema lookup; embedded Studio's startup demo. Packaging uses host ELF tools
instead of the AppImage's incompatible strip/patcher, ships Studio examples and
schema library, and copies project/source-vendor license notices. The relocated Release
package passed 12 checks inside a fresh Fedora 44 container without system Qt
or the source checkout. Unused Qt XR/OpenXR components are excluded from the
package. Nothing was published. The Windows platform helper cross-compiled; the existing MSYS2
sysroot lacks Qt Qml, so updated Windows Studio/package behavior remains unverified.

A1: a clean Linux Studio Release build passed 73/73 tests in 74.72 seconds,
including all four GUI suites and the new embedded-demo regression. A separate
90-second visible-Scope measurement (30 seconds per model rate at 1/10/100 kHz)
recorded about 425–429 thousand steps/wall-second, complete four-channel capture,
maximum event-loop gaps below 251 ms and measured controls/close below 122 ms.
Whole-process sampled RSS peaked below 752 MiB; capture storage grew about
1.53 GiB per case. These are observations for one four-element graph on the
stated machine, not guarantees for arbitrary rates, plugins or graph sizes.
Actual disk exhaustion, multi-hour behavior, large interactive export/import,
physical GPU/high-DPI/accessibility, native Windows and the broader numerical
matrix remain open; no overall release-ready claim is made.

Follow-up validation: Clang 22 headless ASan/UBSan build passed core execution,
plugin-loader and runtime-control smoke tests (3/3, 0.87 seconds), with no
sanitizer diagnostics. This is focused local coverage, not a full sanitizer CI
matrix. Fast-forwarded the other agent's license commit `c76cd75` before further
pushes and verified the packaged licensing map, third-party notice file and every
S7 license text byte-for-byte against the refreshed repository.


GUI testing policy: future GUI checks, helpers, captures and reports stay local.
Hosted CI now builds and tests the headless configuration only. Earlier hosted
Studio results above are historical evidence, not the current CI policy.
