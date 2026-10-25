# Studio Observation Viewers

Studio shows observed signals in viewer windows: **Scope** (signals over model time), **Phase Portrait** (one signal against another) and **3D Phase Trajectory** (three signals as a continuous curve in space). The 2D viewers draw with **Qt Graphs**; the 3D viewer draws with **Qt Quick 3D**. Both are embedded in the Qt Widgets application through `QQuickWidget`. This page describes the design, how to add a viewer type, how it is tested, and what remains to be checked by eye.

Observation stays outside computation (ADR-011, ADR-012, ADR-013 in `../../doc/S7_Architectural_Decisions.md`): viewers read samples and never step, pause or alter the runtime. Nothing here touches `s7core`, `s7runtime` or `s7observe`, which remain free of Qt.

The [Observers dock](STUDIO_OBSERVERS_DOCK.md) hosts independent compact
presentations for all four built-in types. `LinePlotView` and the extracted
`TrajectoryPlotView` are shared plot components, used by full windows and
previews without reparenting full windows. `ViewerType::createPresentation`
registers a preview factory. Bounded `preparePreview`/`PreviewWorker` data
preparation is separate from these widgets. `ViewerManager` owns live trigger
settings and applies them before production; preview creation is read-only.
`ObservationSource::tail` projects per-channel caches rather than replaying
the full journal when a preview opens late. The source owns at most 4,096 cached
records per channel in addition to its authoritative recording storage.

The separate [live oscilloscope prototype](LIVE_OSCILLOSCOPE_PROTOTYPE.md) uses a
bounded acquisition source and does not journal live-only channels. The data-flow
and full-run journal descriptions below apply to the existing recording viewers.
Its **Time**, [**Spectrum**](STUDIO_SPECTRUM.md), and
[**Constellation / XY** and **Correlation**](STUDIO_PAIR_ANALYSIS.md) tabs share
coherent raw frames. Only the visible tab of a visible, non-minimized window
submits analysis; inactive tabs stop polling and cancel pending work. Acquisition
continues independently. Returning restarts FFT averaging/hold and XY persistence
from retained data. Analysis uses a shared two-thread pool, separate from causal
execution. Use the recording Scope's Save Run/CSV workflow when full-run results
are required. [Live window inspection/export](STUDIO_WINDOW_INSPECTION.md) pins
immutable raw/derived data for A/B cursors, inclusive statistics and asynchronous,
atomic JSON/CSV export. Window files do not claim full-run capture or support
recording-Scope import.

[Edge triggering](STUDIO_TRIGGERING.md) runs in `LiveAcquisition::append()` on
the producer. Fixed double capture banks preserve one completed frame while the
next is collected; a trigger-relative Time axis leaves absolute raw timestamps
unchanged. Analysis workers, rendering and trigger capture remain separate.

## Data flow

```text
LocalSchemaRunner worker
  NodeRuntime::step() → ObservationService reads selected outputs
       ↓ synchronous append (bounded RAM, then temporary disk; backpressure)
  ObservationSource ──────────────→ result services → atomic save / CSV export
       ↓ const handle + bounded paged reads
  ObservationViewer → disposable display projection → Qt Graphs / Qt Quick 3D

  ScopeSampleMailbox → MainWindow → ViewerManager::present()
       bounded preview notifications/loss accounting; never authoritative capture
```

A source belongs to one run and outlives individual windows. Capture occurs on
the stepping thread and cannot change runtime state. Viewer refresh reads bounded
pages; no hidden window is created to record data. Source I/O is serialized, and
views receive const handles plus explicit role/channel bindings. Closing or
clearing a view does not erase recorded data. Preview loss is distinct from
capture failure. A storage error fails the run and retains an incomplete prefix.

## Components (`s7studio/src/viewers/`)

| Component | Role |
|---|---|
| `ObservationViewer` (`observation_viewer.hpp`) | Presentation contract: `widget()`, const-source attachment, explicit `InputBinding` roles/channels, bounded `refreshSource()`, presentation clear and protected sample consumption. It cannot append capture data. |
| `ViewerType`, `ViewerInstanceSpec` | A registered kind of viewer: `typeId` (equal to the diagram observer element type), picker text, the probe suffixes of its fixed inputs (`x`, `y`, ...; empty for Scope's variable channels) and a factory. |
| `ViewerRegistry` (`viewer_registry.*`) | Process-wide list of viewer types; built-ins register in `registerBuiltinViewers()`. The element picker's **Observers** category and `MainWindow` both read it. |
| `ViewerManager` (`viewer_manager.*`) | Owns one Studio window's viewer instances keyed by (type, instance id), so a diagram can hold several viewers of one type. Creates windows only on ensure/open, binds read-only sources, refreshes bounded pages and reports preview loss; destroys windows on diagram switch/shutdown. A viewer window destroyed elsewhere is forgotten immediately. |
| `StepAligner<N>` (`../observation/step_aligner.hpp`) | Joins N coordinate streams into points of one runtime step. Coordinates may arrive in any order; a step left incomplete after eight newer steps started is discarded and counted as dropped. |
| `LinePlotView` (`line_plot_view.*`, `qml/LinePlot.qml`) | 2D plot: `GraphsView` with line/scatter series and a bounded density image in a `QQuickWidget`. Axis ranges, titles, legend and series are C++ state in `LinePlotView`/`LinePlotBridge`; wheel zoom (about the cursor, both axes) and drag pan are forwarded from QML to C++. |
| `TrajectoryGeometry`, `TrajectoryFrameGeometry` (`trajectory_geometry.*`, `qml/Trajectory3D.qml`) | Custom `QQuick3DGeometry`: the trajectory as one line strip, the frame as a wireframe cube with ticks. `OrbitCameraController` rotates (drag) and zooms (wheel). |
| `quick_support.*` | Creating a viewer's `QQuickWidget`, loading its scene with required properties, and tearing it down safely. |

The viewer windows themselves stay in `s7studio/src`: `scope_view_window.*` (with `scope_recording.*`), `phase_portrait_view_window.*`, `phase_trajectory_3d_view_window.*`. The windows/renderers are in `s7studio_viewers`. QtCore-only `s7studio_observation` owns raw sources and shared step alignment; QtCore-only `s7studio_results` implements Scope result projections and serialization. These are independently testable without a GUI application.

### Scope

`ScopeRecording` holds a disposable display/import projection: per channel a bounded preview of 20,000 points thinned uniformly as the run grows, plus a temporary spill file with every sample once the preview is full. Zooming into a spilled region reads full resolution back from the file. Save Run (`.s7scope`), Open Run (comparison overlays), CSV export and the completeness warnings are unchanged; see [SCOPE_RESULTS.md](SCOPE_RESULTS.md). Imports stage into a separate `ScopeRecording`, so no window is created for a failed or successful import.

`ScopeViewWindow` maps each recording series to a `QLineSeries`, keeps **Fit full run**, **Auto Y** (fits values within the visible time window), the time scroll bar and the legend. Capture continues independently of the Scope. Its projection replays bounded source pages, and its plot is rebuilt when shown. Save/export scans the source rather than relying on the projection having caught up.

### Phase Portrait

X/Y pairs of the same step go through `StepAligner<2>`. The display retains up to two million pairs; export reads all available same-step pairs from the source, independently of that cap; at most 200,000 are drawn, as a uniform subsample that keeps the first and newest points when the trajectory is longer (the status line says so). Qt Graphs `LineSeries` join points in insertion order, so a parametric trajectory is drawn as such.

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

- Qt ≥ 6.9 with Qml, Quick, QuickWidgets, Graphs and Quick3D (`find_package` in `s7studio/CMakeLists.txt`). 6.9 is the first release with `GraphsView.plotArea`, used to anchor zooming at the cursor. Use the repository's dependency scripts for platform packages.
- Studio analysis also requires double-precision FFTW 3 (`fftw-devel` on Fedora,
  `libfftw3-dev` on Ubuntu, or the Windows sysroot's FFTW package). This private
  analysis library is separate from the out-of-tree `s7spectral` runtime plugin;
  headless builds do not acquire an FFTW dependency through Studio.
- At run time the QML modules `QtQuick`, `QtQml`, `QtGraphs`, `QtQuick3D` and `QtQuick3D.Helpers` must be importable: Ubuntu and Fedora packages are in `tools/install_deps_ubuntu.sh` and `tools/install_deps_fedora.sh`; `tools/windows/package.sh` copies them into `qml/`; `tools/linux/package.sh` passes the scenes to linuxdeploy-plugin-qt.
- `-DS7_BUILD_STUDIO=OFF` builds no Qt at all, as before.
- Licensing: Qt Graphs and Qt Quick 3D are available under GPLv3 or a commercial license (not LGPL). Studio is GPL-3.0-or-later, so this is compatible; the LGPL runtime libraries do not use Qt.

## Adding a viewer type

1. Write a window class deriving from `QWidget` and `viewers::ObservationViewer`. Keep its presentation caches private; consume the roles supplied in each sample, not probe-name conventions. Do not write recording data from the window. Align steps with `observation::StepAligner<N>` if it combines several probes, draw with `LinePlotView` or a QML scene of its own (`createQuickWidget()` + `loadQuickScene()`, passing a bridge object as a required property, and `unloadQuickScene()` in the destructor).
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
- Drawn-point caps (Phase Portrait 200,000, 3D 500,000) bound rendering work; source-backed exports are independent of display trimming.
- Time-view preparation now runs off the UI thread. The Linux GUI workflow keeps a 10 ms UI timer responsive during 30 seconds at 100 ksample/s plus 5 seconds unthrottled (8 channels); under Xvfb/software rendering its worst timer gap was 90 ms. This is a responsiveness check, not a plot-throughput or input-to-photon guarantee. A separate 30-minute synthetic acquisition soak completed at about 828,700 samples/s using one CPU core, with 33 MiB peak RSS and the fixed 20 MiB reservation. That backend soak does not exercise a runtime graph or Qt rendering. GPU desktop and native Windows rendering still need manual qualification (test 5).
- Offscreen QML/geometry checks do not establish 3D rendering; the separate Xvfb GUI suite checks actual pixels/camera behavior when OpenGL works.

## Run notification isolation

MainWindow invalidates a generation before disconnecting and joining a retiring
worker on start/reset/document replacement. Queued callbacks retain the previous
generation and are ignored. This applies to samples, progress, status, completion
and logs; document replacement closes viewers only after worker retirement.
The document-safety regression queues worker notifications without pumping GUI
events, replaces the document and verifies those callbacks cannot reach the UI.

## Automated rendered GUI checks

[STUDIO_GUI_TESTING.md](STUDIO_GUI_TESTING.md) documents the native Linux
Qt Test/Qt Quick Test suite (`ctest -L gui`). It exercises actual viewer windows
and the production QML scenes under Xvfb/OpenGL, including nonblank trajectory
pixels and camera interaction. Unavailable OpenGL is explicitly skipped for the
dedicated 3D gate; successful QML loading alone is not a rendering pass.

New-run isolation uses `ObservationViewer::beginRun`: phase viewers clear their
history and partial coordinates, while Scope may move its preceding recording
into a labelled comparison without copying its full spill history. MainWindow
freezes observer types/probe bindings, validates sample-batch run IDs and rejects
opening changed observers against old results. See the local-run lifecycle
contract in STUDIO_DIAGRAM_EDITOR.md. Authoritative capture now precedes the preview mailbox; preview losses do not make captured results incomplete. Storage failures remain explicit run failures.

## Observation data and presentation separation (O1a/O1b)

`observation/ObservationSource` is a QtCore-only run data journal, usable
with QCoreApplication and no windows. It owns one run ID, stable channel IDs,
step/model-time/value records, delivery-loss count and storage-error status.
Memory is bounded (20,000 records by default); subsequent records spill to a
private temporary file. Reads are bounded pages in production order (4,096 by
default, hard maximum 16,384). Private spill files are not a portable interchange
format. A storage error stops further retention, fails the run and remains explicitly incomplete.

`LocalSchemaRunner` owns authoritative capture; `ObservationSession` also supports standalone delivered-data ingestion. In both modes, retained const handles keep an old
run alive without changing it when another run starts. The manager hosts window instances and accepts the runner's const source; standalone delivery also does not create hidden windows. Window creation occurs only through ensure/open. Windows receive a
const source and explicit role/channel bindings; prefix/suffix interpretation is
confined to the diagram-to-binding adapter in ViewerManager. New viewers consume
bounded pages through the common ObservationViewer implementation. Reopening a
destroyed window replays retained source history. Existing instances refresh at
bounded intervals; a large history may take multiple turns to display.

ScopeRecording is now a disposable display/import/comparison projection, not the
owner of delivered results. Scope save/export rebuilds its projection from the
source, independently of display replay progress. Phase exports use the shared
step aligner and source data, not trimmed plotted trajectories. The raw source
also offers window-independent CSV export with run/channel/step/time identity.
Explicit save/export and Scope comparison archival currently perform synchronous
paged scans; making these user operations asynchronous is follow-up responsiveness
work, separate from bounded periodic display updates.

Clear display advances only that view's presentation cursor; it does not delete
recorded data. Creating another view or exporting can still access it. Diagram
replacement releases the session, while externally retained source handles remain
valid. Closing/hiding a window does not control capture. Source and presentation
limits are separate: a trimmed phase plot is not the complete source history.

O1b moves authoritative append to the stepping thread before preview publication.
All selected probes are validated before stepping; complex-valued outputs are
rejected with an actionable error. Other real scalars are explicitly converted
to stored binary64 with source-type metadata and a warning for possible rounding;
F64 values retain their bits. Source records preserve run/channel/64-bit step/time.
Synchronous storage backpressure reduces wall-clock throughput, not model rate.
Run completion flushes storage before publishing success; write/flush/runtime
failures preserve an incomplete prefix. Temporary capture is not crash-durable.

Preview drops are reported as skipped updates, independently of capture status.
The raw/aligned observation CSV formats include a JSON metadata comment with
run identity, source/stored channel types, capture state and errors. Scope results
include run/channel step-range provenance in their JSON header. Scope CSV retains
its existing interchange layout; use .s7scope or observation CSV for completeness
metadata. N1a derives local recording time directly from step/rational rate (see
SIMULATION_MODEL.md); N1b now retains immutable requested/effective model provenance in sources and
Scope v2 results, with read-only Run details for current/imported runs. External
input histories and cross-platform reproduction remain separate work.

`s7studio_observation_source` tests window-free paging/spill and run handles;
`s7studio_observation_capture` withholds preview delivery to force 20,000 dropped
updates while verifying all 220,000 authoritative samples and complete saved
results. It injects partial disk-full writes and final-flush failure, verifies
incomplete saved metadata/recovery, and rejects complex/invalid probes before
stepping. Actual disk exhaustion is not claimed; failures are deterministic I/O
injections. Performance-envelope and asynchronous bulk-operation work remains.

Retained comparison runs belong to the observation session, including runs for
which no window was ever opened. Explicit comparison bindings are passed to a
viewer when it opens. Removing comparison traces clears their presentation;
session-owned raw history remains until diagram replacement/shutdown. Imported
comparison files have their own projection lifetime. Creating a new window may
replay session comparisons; clearing plots is not a delete-recording operation.


### Units, run identity and keyboard navigation

A probe's supplied display unit is frozen with the run and used in Scope
legends, saved comparison legends and phase axes. Diagram observer links inherit
the unit of a named output probe on the same source port. Missing units stay
explicitly unspecified; names are never used to guess units. Scope uses one
shared numeric y-axis and explicitly labels mixed-unit overlays; it does not
convert units or imply that charge, current and voltage share a dimension.

All source-backed viewers show current run ID and capturing/completed/stopped/
INCOMPLETE state, with loading, preview-skip and capture-error details retained.
A completed source may still be loading its presentation history. Comparison
labels remain separate, with incomplete imported runs visibly marked.

Focus a 2D plot to use +/− for zoom, arrow keys for pan and Home to fit all data.
Mouse wheel/drag remain supported; these actions only change the view. Scope
separates display controls from result save/open/comparison controls. Clear
display does not delete authoritative captured results. Phase/3D status occupies
a separate wrapping row so lengthy error messages do not crowd control buttons.
