# Studio Observation Viewers

Studio shows observed signals in viewer windows: **Scope** (signals over model time), **Phase Portrait** (one signal against another) and **3D Phase Trajectory** (three signals as a continuous curve in space). The 2D viewers draw with **Qt Graphs**; the 3D viewer draws with **Qt Quick 3D**. Both are embedded in the Qt Widgets application through `QQuickWidget`. This page describes the design, how to add a viewer type, how it is tested, and what remains to be checked by eye.

Observation stays outside computation (ADR-011, ADR-012, ADR-013 in `../../doc/S7_Architectural_Decisions.md`): viewers read samples and never step, pause or alter the runtime. Nothing here touches `s7core`, `s7runtime` or `s7observe`, which remain free of Qt.

## Data flow

```
LocalSchemaRunner worker            UI thread
  NodeRuntime::step()
  ObservationService::snapshot()  ─► ScopeSampleMailbox ─► MainWindow::onScopeSamplesAvailable
  (one sample per probe per step;      (bounded, drops       │
   step number + model time)            oldest, counts)      ▼
                                                    viewers::ViewerManager::deliver
                                                      │  resolves each observer once per batch
                                                      │  (diagram element type, else Scope),
                                                      │  creates missing viewers hidden,
                                                      ▼  routes by probe name
                                         ObservationViewer::appendObservedSample
                                           Scope:   ScopeRecording (preview + spill file)
                                           Phase:   StepAligner<2> → (x, y) pairs
                                           3D:      StepAligner<3> → (x, y, z) triples
                                                      │  repaint timer (33 ms, visible only)
                                                      ▼
                                         LinePlotView (Qt Graphs) / TrajectoryGeometry (Qt Quick 3D)
```

Simulation → observation → data adapter → viewer → renderer: the mailbox and `ViewerManager` are the transport, `ScopeRecording` and `StepAligner` adapt samples to what a viewer draws, and the renderers only see finished point lists. Samples cross threads only through the existing mailbox; viewers, their QML scenes and their data live on the UI thread.

## Components (`s7studio/src/viewers/`)

| Component | Role |
|---|---|
| `ObservationViewer` (`observation_viewer.hpp`) | The minimal interface every viewer implements: `widget()`, `subscribes(probe)`, `appendObservedSample()`, `clearSamples()`, `setDroppedSampleCount()`. Nothing about storage or drawing. |
| `ViewerType`, `ViewerInstanceSpec` | A registered kind of viewer: `typeId` (equal to the diagram observer element type), picker text, the probe suffixes of its fixed inputs (`x`, `y`, ...; empty for Scope's variable channels) and a factory. |
| `ViewerRegistry` (`viewer_registry.*`) | Process-wide list of viewer types; built-ins register in `registerBuiltinViewers()`. The element picker's **Observers** category and `MainWindow` both read it. |
| `ViewerManager` (`viewer_manager.*`) | Owns one Studio window's viewer instances keyed by (type, instance id), so a diagram can hold several viewers of one type. Creates on demand, opens, routes samples, propagates capture-loss counts, clears on a new run/reset, and destroys all viewers on diagram switch and shutdown. A viewer window destroyed elsewhere is forgotten immediately. |
| `StepAligner<N>` (`step_aligner.hpp`) | Joins N coordinate streams into points of one runtime step. Coordinates may arrive in any order; a step left incomplete after eight newer steps started is discarded and counted as dropped. |
| `LinePlotView` (`line_plot_view.*`, `qml/LinePlot.qml`) | 2D plot: `GraphsView` with `LineSeries` in a `QQuickWidget`. Axis ranges, titles, legend and series are C++ state in `LinePlotView`/`LinePlotBridge`; wheel zoom (about the cursor, both axes) and drag pan are forwarded from QML to C++. |
| `TrajectoryGeometry`, `TrajectoryFrameGeometry` (`trajectory_geometry.*`, `qml/Trajectory3D.qml`) | Custom `QQuick3DGeometry`: the trajectory as one line strip, the frame as a wireframe cube with ticks. `OrbitCameraController` rotates (drag) and zooms (wheel). |
| `quick_support.*` | Creating a viewer's `QQuickWidget`, loading its scene with required properties, and tearing it down safely. |

The viewer windows themselves stay in `s7studio/src`: `scope_view_window.*` (with `scope_recording.*`), `phase_portrait_view_window.*`, `phase_trajectory_3d_view_window.*`. All of this is the `s7studio_viewers` static library, shared by Studio and its tests.

### Scope

`ScopeRecording` holds the data: per channel a bounded preview of 20,000 points thinned uniformly as the run grows, plus a temporary spill file with every sample once the preview is full. Zooming into a spilled region reads full resolution back from the file. Save Run (`.s7scope`), Open Run (comparison overlays), CSV export and the completeness warnings are unchanged; see [SCOPE_RESULTS.md](SCOPE_RESULTS.md). Imports stage into a separate `ScopeRecording`, so no window is created for a failed or successful import.

`ScopeViewWindow` maps each recording series to a `QLineSeries`, keeps **Fit full run**, **Auto Y** (fits values within the visible time window), the time scroll bar and the legend. A hidden Scope keeps recording; its plot is rebuilt when shown.

### Phase Portrait

X/Y pairs of the same step go through `StepAligner<2>`. Every pair is retained (up to two million) and exported; at most 200,000 are drawn, as a uniform subsample that keeps the first and newest points when the trajectory is longer (the status line says so). Qt Graphs `LineSeries` join points in insertion order, so a parametric trajectory is drawn as such.

### 3D Phase Trajectory

Triples of the same step go through `StepAligner<3>`. The trajectory is drawn as one continuous line strip (at most 500,000 vertices, thinned like the Phase Portrait). Each data axis is scaled independently into the cube, with tick values at 10 %, 50 % and 90 % of each range and axis titles; data X points right, data Y into the screen, data Z up. Buttons **XY**, **XZ**, **YZ** turn the view to the orthogonal projections the previous viewer showed side by side; **3D** returns to the default view. A red marker shows the newest point. A Qt Graphs 3D scatter was not used: unconnected points do not show a trajectory.

## Qt integration

- **QQuickWidget per viewer.** Each viewer owns its widget and QML engine, so instances share no QML state. The scene is created on first show, unparented and stored before it is added to the layout: constructing a `QQuickWidget` as a child of a visible widget processes events and would re-enter `showEvent`.
- **C++ owns the state.** QML receives one bridge object as a required property (`plot`, `trajectoryView`). Series and geometries are C++ objects; tests check them without rendering.
- **Teardown.** `unloadQuickScene()` clears the source (destroying the QML objects synchronously) before deleting the widget, then the bridge and data objects go. Removed `QLineSeries` are emptied and kept until the plot is destroyed, because `GraphsView` releases removed series on its next polish.
- **Rendering backend.** Qt Quick picks the platform's RHI backend (OpenGL, Vulkan, Direct3D 11/12, Metal). Under `QT_QPA_PLATFORM=offscreen` Qt Graphs still renders (software), while Qt Quick 3D prints *"Qt Quick 3D is not functional"* and draws nothing; viewer data, geometry and QML loading still work, which is what the automated tests check.
- **High DPI and resizing.** `SizeRootObjectToView`; QML sizes in device-independent pixels.
- **Resources.** The QML files are compiled into the library as Qt resources (`qrc:/s7studio/viewers/`) with `qt_add_resources`; no QML module or `qmlcachegen` step, so the Windows cross build needs only the host `rcc`.

## Build requirements

- Qt ≥ 6.9 with Qml, Quick, QuickWidgets, Graphs and Quick3D (`find_package` in `s7studio/CMakeLists.txt`). 6.9 is the first release with `GraphsView.plotArea`, used to anchor zooming at the cursor. Ubuntu 26.04 ships 6.10, MSYS2 6.11.
- At run time the QML modules `QtQuick`, `QtQml`, `QtGraphs`, `QtQuick3D` and `QtQuick3D.Helpers` must be importable: Ubuntu packages are in `tools/install_deps_ubuntu.sh`; `tools/windows/package.sh` copies them into `qml/`; `tools/linux/package.sh` passes the scenes to linuxdeploy-plugin-qt.
- `-DS7_BUILD_STUDIO=OFF` builds no Qt at all, as before.
- Licensing: Qt Graphs and Qt Quick 3D are available under GPLv3 or a commercial license (not LGPL). S7 is GPL, so this is compatible.

## Adding a viewer type

1. Write a window class deriving from `QWidget` and `viewers::ObservationViewer`. Keep its data structures private: align steps with `StepAligner<N>` if it combines several probes, draw with `LinePlotView` or a QML scene of its own (`createQuickWidget()` + `loadQuickScene()`, passing a bridge object as a required property, and `unloadQuickScene()` in the destructor).
2. Give it a `static auto viewerType() -> viewers::ViewerType` with a unique `typeId`, picker text, `inputSuffixes` and a factory that validates the `ViewerInstanceSpec` (return `nullptr` to reject it). The window is created hidden and unparented; `ViewerManager` owns it.
3. Register it: add `registry.add(MyViewer::viewerType())` to `registerBuiltinViewers()` (or call `ViewerRegistry::instance().add()` from a plugin hook). The element picker, `MainWindow` routing and lifecycle need no change.
4. Diagram side: an observer element type must exist with that `typeId` and its input ports (`diagram/element_ports.cpp`: `isObserverElementType`, `isAxisObserverElementType`, `elementPorts`). That is the diagram's representation of observation, outside the functional graph, and deliberately separate from the viewer.
5. Add the sources to `s7studio_viewers` (and QML files to its `qt_add_resources`), and add tests: data/alignment checks against same-step headless values, closing/reopening, clearing, and QML loading (`quickWidget()->status() == QQuickWidget::Ready`).

## Tests

| Test | Covers |
|---|---|
| `s7studio_viewers` | Registry (built-ins, adding/replacing a type routed by the unchanged manager), several instances per type, routing and X/Y/Z step alignment with interleaved observers, capture-loss propagation, clear, close/reopen, viewers destroyed elsewhere, `closeAll` (diagram switch), shutdown with viewers open, plot range/zoom/pan/fit/legend/overlay state, LinePlot.qml and Trajectory3D.qml loading, trajectory geometry mapping and thinning, independent 3D instances. |
| `s7studio_scope_view` | Scope preview resolution and bounds at 1/10/100 kHz, Fit full run, Auto Y within the visible window, zoom/pan anchoring, CSV precision and escaping. |
| `s7studio_scope_lifecycle` | Spill-backed save/open, overlays, malformed-input rollback, zoomed disk-backed history; the real worker through pause/resume/stop/reset/rerun, also routed through `ViewerManager` into a Phase Portrait that must equal the same-step pairs; mailbox overload; storage failure. |
| `s7studio_scope_run`, `s7studio_phase_portrait`, `s7studio_phase_trajectory_3d` | MainWindow end to end with example diagrams: every recorded point equals the same-step headless state and the discrete analytical solution (`1e-10`), drawn data within axis ranges, labels, opening before/after the run, closing/reopening, clearing, out-of-order coordinates. |
| `s7studio_nonlinear_pendulum` | Scope recording and CSV fidelity over 100 s at 1/10 kHz. |

Screenshots: `S7_SCOPE_PNG`, `S7_PHASE_PORTRAIT_PNG` and `S7_PHASE_TRAJECTORY_3D_PNG` save the viewer from the corresponding `s7studio --*-check` run. Without `QT_QPA_PLATFORM=offscreen` (on a desktop session) the 3D view is rendered too.

## Manual visual test plan

Automated tests cannot judge rendering, interaction feel or GPU backends. Check on each platform build (Linux desktop, Windows):

1. **Scope** — open `examples/2int_osc.s7diagram`, run 10 s at 1 kHz, open `scope_1`: a sine over 0–10 s with legend `v`. Wheel zooms about the cursor; drag pans; with **Fit full run** off the scroll bar moves the window; **Auto Y** refits values only. Save Run, rerun, Open Run: the comparison is translucent and thinner, with a dashed legend swatch. Resize and maximize: no clipping, labels readable.
2. **Several Scopes** — a diagram with two Scope observers: each window shows only its own channels; closing one leaves the other running.
3. **Phase Portrait** — `examples/2int_osc_phase_portrait.s7diagram`, 10 s at 1 kHz: a slowly widening circle; axis titles are the X/Y probes; wheel/drag work; Clear empties it; reopening after closing shows the same trace. At 100 kHz the status says every Nth point is drawn.
4. **3D Phase Trajectory** — `examples/3int_helix_phase_trajectory.s7diagram`, 10 s at 1 kHz: a continuous rising helix with a red newest-point marker. Drag rotates, wheel zooms, **XY** shows a circle from above, **XZ**/**YZ** show sine-like side views, **3D** resets. Tick labels and axis titles stay readable from any angle.
5. **Live update** — run continuously and watch all three viewers update without stalling the editor; pause/resume/stop and reset behave as for the Scope.
6. **Diagram switch and shutdown** — with viewers open, open another diagram (viewers close) and quit Studio (no crash, no QML warnings on the console).
7. **High DPI** — at 150–200 % scaling, lines and text are sharp and sized consistently.
8. **Windows** — the packaged `dist/s7-windows` runs the above on Windows (Direct3D); Wine is not a rendering reference.

## Known limitations

- Qt Graphs 2D line series have no dash patterns; Scope comparison overlays are told apart by translucency, width and the legend swatch instead of dashes.
- Zoom changes both axes together; there is no box zoom or per-axis wheel zoom yet.
- Drawn-point caps (Phase Portrait 200,000, 3D 500,000) bound rendering work; exports always contain every retained point.
- Live rendering throughput is not characterized. With software rendering (offscreen CI, Wine) a visible Qt Graphs plot costs far more UI-thread time than QCustomPlot's raster drawing did: in `s7studio_scope_lifecycle` a Scope left visible during an unthrottled run made the UI fall behind and the mailbox drop samples under Wine, so that test records hidden. Check on GPU desktops that a visible viewer keeps up at the intended rates (manual test 5).
- Under offscreen platforms Qt Quick 3D renders nothing, so 3D pixels are only checked manually.
