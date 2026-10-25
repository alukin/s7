# Automated Studio GUI regressions

The `gui` CTest label runs Qt Test workflows against the real Studio application
objects and Qt Quick Test against the production Qt Graphs/Qt Quick 3D scenes.
Tests run unattended on native Linux in separate Xvfb displays, including when
launched from a desktop session. They do not operate on the user's desktop.

## Run locally

Requirements: the normal Studio dependencies (Qt 6.9+), Qt Test, Qt Quick Test,
the `QtTest` QML module, Python 3, Xvfb, Mesa OpenGL and a working xcb platform
plugin. Fedora: `sudo dnf install xorg-x11-server-Xvfb mesa-dri-drivers
qt6-qtbase-devel qt6-qtdeclarative-devel`. Ubuntu additionally needs
`qml6-module-qttest`; install the normal Studio dependencies first.

Native library drag/drop checks additionally use `xdotool` (Fedora/Ubuntu
package `xdotool`) to send XTest mouse events to the wrapper's private Xvfb
display. Those gesture cases explicitly skip when it is unavailable. The MIME
rejection checks still run. No drag events are sent to the user's desktop.

```bash
cmake -S . -B build -DS7_BUILD_STUDIO=ON -DS7_BUILD_GUI_REGRESSION_TESTS=ON
cmake --build build --target build-tests --parallel 2
ctest --test-dir build -L gui --no-tests=error --output-on-failure -j 2
```

GUI test executables are excluded from the normal application build and included
in `build-tests`. The option defaults ON for native Linux Studio builds and OFF
for cross builds. Enabling it elsewhere reports that the wrapper is unsupported;
existing offscreen/Wine checks remain separate. `Qt Test` assertions stay active
in Release builds. Tests do not write settings or recordings under `$HOME`.

## Coverage

Observers dock workflows cover empty selection, chooser Cancel/Apply, all four
presentation types, no implicit full windows, independent full-viewer freeze,
pre-production saved triggers without a full window, acknowledged run states,
manual dock switching, suspended hidden work, active versus next-run bindings,
deletion/undo, reset, Save As/reopen and Recorded Signals. They retain card
captures and run at scaled resolutions. Source/presentation contract tests in
`s7studio_observer_presentations` cover sparse per-channel tails with zero
journal reads, bounded reduction and gaps, same-step phase joins, shared trigger
ownership, independent presentations, stale-worker rejection, undo/serialization
and deployment projection equality.

Optional observer load measurements use `s7studio_observer_panel_benchmark`
(excluded from build-tests/CTest). It publishes synthetic source samples at a
requested 1 kHz; it measures source delivery and GUI load, **not NodeRuntime
throughput**. Twelve selected observers and a tall virtual workspace exercise
the four-active-plot limit. `mixed` scrolls recording/phase/live cards, `full`
also opens four full viewers, and `live` supports a 30-minute journal-free soak.
Each run includes a 10-second hidden baseline and 10 seconds of warm-up. JSON
records achieved source rate, UI gaps, RSS checkpoints, active plots and separate
recording payload growth. Keep temporary recordings on a disk-backed filesystem
when `/tmp` is a small tmpfs; the run fails on capture or result-write errors.

```bash
cmake --build build-release --target s7studio_observer_panel_benchmark --parallel 2
python3 tools/run_gui_test.py --artifacts build-release/observer-artifacts/mixed \
  --capture build-release/s7studio/s7studio_gui_capture --screen-size 2000x2600 \
  --timeout 200 -- env TMPDIR=/var/tmp \
  build-release/s7studio/s7studio_observer_panel_benchmark \
  build-release/observer-artifacts/mixed.json 120 mixed full
```

Set `S7_OBSERVER_STRESS_PREVIEW=1` when invoking the existing
`liveOscilloscopeWorkerKeepsUiResponsive` GUI case to measure its eight-channel,
100 ksample/s, 35-second workload with an additional dock preview. The normal
suite keeps the full-viewer-only baseline. Sanitizer checks cover source/worker
contracts and both new dock workflows; Qt/global leak detection is disabled
for those ASan/UBSan runs, with process memory measured separately by the soak.

Instrument-control workflows cover selected-channel knobs, independent channel
visibility, unchanged raw samples, pre-trigger synchronization, labelled edge
buttons, crossing A/B sliders and Alt+Left/Right, independent section expansion,
field bounds and wrapping analysis settings at 1024×768 and 1440×900. Captures
include light/dark palettes. Separate exact-value checks cover logarithmic gain
endpoints and scientific entry, unquantized edits, offset interval stability,
clamping and precision limits. Finite/continuous run indicators are checked
without starting a synthetic percentage for continuous execution.

| CTest entry | Automated checks |
|---|---|
| `s7studio_gui_workflows` | Visible MainWindow; palette insertion; Edit undo/redo; element double-click/F4, typed and advanced JSON edits, inline error/cancel, exact uint64 and bounds; clipboard/probe undo/save-reopen; run configuration; Save As/close/Open round trip; deletion followed by Save/Discard/Cancel; Scope, Phase Portrait and 3D observer creation; start/pause/hide/reopen/resume/stop/reset/finite rerun; Scope result save/import/remove-comparison buttons; RLC unit legends and run labels; keyboard plot zoom/fit; missing-plugin feedback. |
| `s7studio_gui_lineplot` | Production `LinePlot.qml` with the real C++ bridge and a known reference series; rendered curve pixels, titles and legend; wheel zoom and drag pan through QML pointer handlers. |
| `s7studio_gui_trajectory` | Production `Trajectory3D.qml` with real trajectory/frame geometries; actual green trajectory pixels, wheel camera zoom and a camera preset changing the rendered image. A blank but successfully loaded View3D fails. |
| `s7studio_gui_failure_artifacts` | Deliberately fail one Qt Test case, require a failing JUnit result plus screenshots/logs/document JSON; verify unavailable Xvfb produces exit 77 and a skipped JUnit result; deliberately block a child and require watchdog termination, a pre-termination screenshot and a failing process report. The injected failure is expected and does not fail this self-check. |

Interactions use named QAction/widget/QML objects and model items. Pointer
positions are derived from the selected item or plot's current bounds, never
absolute desktop coordinates. Modal dialogs use non-native Qt widgets, an
object-based responder and a watchdog. Worker completion uses QSignalSpy and
QTRY conditions, rather than arbitrary waits. The existing numerical and
non-invasive-observation tests still provide value/reference accuracy coverage;
GUI tests do not replace those guarantees.

The workflow suite also covers diagram zoom shortcuts (Ctrl+=, Ctrl+Plus,
Shift+Plus and keypad Plus), a single zoom change per keypress, zoom-out,
View menu/Ctrl+wheel zoom, focused library text entry and nested-editor routing.
Run toolbar checks use real hover for every action, including disabled buttons,
verify accessible names and icon-only presentation, and click through
pause/resume/stop/reset. Successful toolbar state captures are retained in the
workflow artifact directory.

The menu increment additionally checks menu order and action ownership,
Page/drawing activation, shared trace-retention wording, editing in text fields,
standard example file selection, working-copy Save, replacement Cancel/Save/
Discard/failed Save, and rejected layout preserving a paused run. Font checks
cover the 10 pt default, a 4 pt user choice, and all bundled example frames.
Palette checks use rendered pixels for light/dark toolbar backgrounds and
normal/disabled icon colors after an application palette change.

The element-information increment checks library context-menu event targeting
with another row selected, context-menu key and Shift+F10, category/blank-space
handling, Edit information for ordinary/schema/observer elements and active
nested editors, metadata parameter coverage, and long plain-text plugin
information. Both dialogs support copying and keyboard document navigation;
small-dialog checks keep parameter fields and dialog buttons visible. Qt Test
mouse events do not synthesize the platform context-menu event, so its mouse
case explicitly dispatches that event to the real library viewport.

Library placement workflows use native Qt QDrag/XDND gestures for filtered and
unfiltered entries, root/nested editors, zoom/pan, built-ins, schema definitions,
the loaded noise plugin and all four observer types. They check grid positions,
unique IDs, unchanged library rows, one undo/redo step, double-click defaults and
save/reopen. Native Escape cancellation and outside drops leave the document and
undo history unchanged. Separate drag-enter/move/drop event checks reject bad
MIME types, malformed/oversized/versioned payloads, unavailable entries,
move-only offers and out-of-viewport positions; availability is rechecked at drop.

For scaled-display checks, enlarge the framebuffer with `--screen-size` so the
logical workspace stays within the acceptance envelope. For example, this uses
a 1440×900 logical workspace at 200% (rather than shrinking the default Xvfb
screen to 800×500 logical pixels):

```bash
python3 tools/run_gui_test.py --screen-size 2880x1800 \
  --artifacts /tmp/s7-information-200 \
  --capture build/s7studio/s7studio_gui_capture -- \
  env QT_SCALE_FACTOR=2 build/s7studio/s7studio_gui_workflows_test \
  elementInformationAccess longElementDocumentation
```

## Displays, rendering and skips

`tools/run_gui_test.py` starts Xvfb with `-displayfd` for atomic display allocation,
so parallel tests cannot race for the same display number. It disables TCP
listening, selects xcb/OpenGL, and uses Mesa software rendering by default
(`LIBGL_ALWAYS_SOFTWARE=1`). Set that variable to `0` to permit a supported
hardware renderer. This is real Qt Quick 3D rendering through OpenGL, not the
non-rendering Qt Quick software scenegraph backend. The renderer and OpenGL
version are captured by a separate context probe before the dedicated 3D test.

Missing Xvfb produces CTest **Skipped** (exit 77) and `skip.txt`/a skipped JUnit
case. An unavailable OpenGL context/surface does the same for the dedicated
3D rendering test. Once graphics is available, QML failures, blank images,
crashes and rendering regressions fail the test; they are never converted into
skips. The workflow's Qt Quick widgets may also fail if graphics cannot initialize:
the dedicated rendering skip does not waive application workflow failures.

Each process has a 120-second watchdog inside a 180-second CTest timeout. Qt
messages, QSG diagnostics and standard output/error go to `diagnostics.log`.
JUnit XML is written to `results.xml`. Qt Quick tests save scene PNGs. On Qt Test
failure, cleanup captures Studio, visible windows, Quick framebuffers, the object
tree and current document JSON. The wrapper captures the virtual screen after a
failed process and before terminating a timed-out process. A crash may close its
windows before that final screen capture; logs and available prior images remain.

Artifacts are under `build/s7studio/gui-artifacts/<suite>/`. Settings/caches are
isolated there, with document fixtures in a QTemporaryDir. For a manual artifact
check, the registered self-check invokes the workflow executable with
`S7_GUI_INJECT_FAILURE=1` and only `failureArtifactProbe`; do not set that variable
for normal suite runs.

## Continuous integration and measured evidence

GUI testing is local-only by maintainer policy. Keep future GUI test helpers,
captures and reports outside Git. The existing local commands above remain
available; GitHub Actions runs the headless configuration with
`S7_BUILD_STUDIO=OFF` and `S7_BUILD_GUI_REGRESSION_TESTS=OFF`. Hosted jobs do not
install Qt/Xvfb/Mesa or upload GUI artifacts. Earlier measurements below record
the historical validation before this policy change.

Local evidence, 2026-10-20: Fedora 44, GCC 16.2.1, Qt 6.11.2, Debug build,
Xvfb with Mesa 26.2.4 llvmpipe (LLVM 22.1.8), OpenGL 4.6. The final CI-command GUI run passed 4/4 in 2.81 seconds: 10 Qt Test cases,
Qt Quick 2D and 3D pixel/interaction checks, and the failure/skip artifact self-check.
No rendering tests were skipped. The 61 existing tests separately passed
in 429.33 seconds; the refined failure/watchdog self-check passed in 2.48 seconds.
See DEVELOPMENT_PLAN.md for the exact commands and remaining release work. Software-rendered virtual
display evidence does not establish native Windows rendering, physical-GPU,
high-DPI or long-duration performance acceptance.


Visual evidence can be retained on successful workflows with
`S7_USABILITY_CAPTURE_DIR=/absolute/path` when running the GUI suite. This is
separate from the existing failure artifacts. The tests capture actual visible
Qt widgets at engineering workflow boundaries, including integrator properties,
inline errors, missing elements, simulation state and saved comparisons. See
[the 2026-10-21 usability audit](usability/2026-10-21/AUDIT.md) for baseline
production-application desktop captures, incremental comparisons, severities
and assessment limits. Screenshots support a heuristic review; they are not
pixel-equality gates or substitutes for human usability testing.
