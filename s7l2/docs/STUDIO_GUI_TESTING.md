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
