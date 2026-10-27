# S7 0.1.1 — interface design and implementation plan

Date: 2026-10-24; updated 2026-10-25. Status: ST-001/ST-002/ST-003/scoped ST-004/ST-005/ST-006/ST-007/ST-008 and font/palette fixes implemented and locally verified;
release qualification remains.

The maintainer accepted the proposed order of work and requested step 1:
settle the interface design and acceptance checks. This document makes the
recommendations concrete. It does not claim approval of every design choice,
implementation completion, visual qualification or release readiness.
The findings and original requirements remain in
[STUDIO_0.1.1_TRIAGE.md](STUDIO_0.1.1_TRIAGE.md); the existing menu inventory
is in [STUDIO_MENU_REVIEW.md](STUDIO_MENU_REVIEW.md).

## Implemented increment — ST-001/ST-002, 2026-10-24

The maintainer requested both isolated fixes after the design step:

- Zoom In registers the standard platform bindings plus Ctrl+=, Ctrl+Plus and
  Ctrl+Shift+Plus. Root/nested editor shortcut scope leaves focused text fields
  alone; menu zoom remains available. No vendor changes.
- The six Run toolbar actions use icons only, retaining menu labels and
  accessible names. All have descriptive tooltips, including when disabled.

Baseline Linux Qt 6.11.2/Xvfb tests reproduced ignored Ctrl+= and the different
Shift+Plus fallback increment. After the fix, all four tested zoom-in variants
(including keypad Plus) produce exactly one 1.15× zoom change. Focus, nested
routing, zoom-out, View menu and Ctrl+wheel checks pass. Real hover verifies
every toolbar description, including disabled Pause/Resume/Stop and disabled
Run during execution; toolbar clicks cover pause/resume/stop/reset. State
screenshots are retained outside Git in the workflow artifacts.

Verification: native Studio Release rebuild; **7/7** selected CTest entries pass
(all four GUI suites, run/window lifecycle and Studio smoke), with **33 passing
workflow cases** and no skips. Windows Studio Release cross-build and the
**1/1 Wine Studio smoke** pass. This is not native Windows keyboard/rendering
qualification, physical-display/theme/high-DPI acceptance, or the complete
0.1.1 release gate. Offline help and triage are updated with the new behavior.

## Implemented increment — ST-005, fonts and palette, 2026-10-24

Menus now follow File, Edit, View, Diagram, Run, Nodes, Settings and Help.
Page and existing drawing/text-frame actions are under Diagram; zoom/fit are
under View without duplicate Fit Diagram placement. Nodes groups the existing
network actions. Library operation labels describe their actual behavior.
Keep Previous Scope Traces has the same positive wording and setting in Run
and Run Configuration. Editing commands register real shortcuts, respect text
focus, and use the active root/nested diagram when editing the canvas.

Example Diagrams opens the standard file chooser in the distribution's examples
directory, as requested. Selected documents are parsed and their layout staged
before current-document replacement. Cancel, bad layout and failed Save preserve
the document; cancellation and preflight failure also preserve a paused run.
Examples open as working copies, with a new destination required by Save.
There is no custom example dialog/catalog or `example_diagrams.hpp`.

New text frames and the toolbar font-size control default to 10 pt. Smaller
user-selected sizes remain available, including 4 pt in the regression check.
All 62 bundled frames specify at least 10 pt; only the two previously at 9 pt
needed file changes. Toolbar backgrounds and icon colors follow the Qt/KDE
palette, including subsequent palette changes and disabled icon colors.
Light/dark rendered captures are kept outside Git.

Verification: **9/9** selected native Studio CTest entries passed (53.54 s),
including all four GUI suites, run/window lifecycle, document safety, schema
editing and smoke; **39 workflow cases** passed without skips. The native GUI
test executable also passed the example-file-dialog workflow from both Linux
and Windows package directory layouts after relocation, with `/tmp` as its
working directory. These are directory-lookup checks against host Qt, not a
fresh binary-distribution/platform qualification. Windows Studio Release
cross-build and **1/1 Wine Studio smoke** pass; native Windows GUI acceptance
remains separate.

## Implemented increment — ST-007, JSON metadata, 2026-10-25

The maintainer selected complete metadata in module-owned JSON, replacing the
initial C++ prose approach. The 49 definitions across core, I/O, networking,
noise, the reference spectral plugin and Studio observers are embedded at build
time. Existing schema elements keep their JSON library definitions. C++ retains
factories and executable behavior. ElementMetadataLibrary decodes the same
parameter/type/port declarations used by validation and information; the SDK
exports the embedding helper for out-of-tree plugins. Missing definitions,
duplicate type/parameter names, malformed fields, invalid defaults and unknown
type rules fail explicitly. Comparison of all 49 definitions before/after the
move preserved defaults, types, bounds, choices, ports, variables, variadic rules
and visual hints; executable factory bodies are unchanged.

Element Information is available from a library row's context menu (including
the context-menu key and Shift+F10), and from Edit for the active root/nested
canvas selection. Categories and blank space offer no element action.
Properties and Information share a read-only, selectable plain-text view.
Properties uses an adjustable vertical splitter with independently scrollable
documentation and parameters; buttons remain outside it. Long plugin text and
markup-like strings are preserved without loading external content. Schema and
observer double-click behavior is unchanged. The parameter inventory and audit
sources are in [STUDIO_ELEMENT_INFORMATION.md](STUDIO_ELEMENT_INFORMATION.md).

The nested-selection workflow exposed a shutdown crash: a deferred nested page
could emit selectionChanged after toolbar actions were destroyed. MainWindow
now disconnects every editor/scene callback before tab teardown. The new
information workflow retains that selected-nested-editor shutdown regression.

Verification: native GCC 16.2.1 / Qt 6.11.2 Release `build-tests` and the complete
CTest suite pass **82/82** (105.61 s), including all four GUI suites and **42
workflow cases**, without skips. The out-of-tree spectral SDK test passes.
A fresh Release headless configuration builds and passes **5/5** selected
core/metadata/I/O/network/noise smoke tests. Windows Studio cross-build passes;
Wine passes **2/2** metadata and Studio smoke checks.

Focused information/long-text workflows also pass at 150% and 200% on Xvfb,
using 2160×1350 and 2880×1800 framebuffers respectively (1440×900 logical).
The GUI wrapper now accepts `--screen-size` to preserve a meaningful logical
workspace during scaled checks; its failure/skip/watchdog self-check passes.
Small-dialog coverage uses 560×460 logical pixels. Captures and numerical
metadata comparison snapshots remain outside Git under `/tmp/s7-st007-*` and
the normal GUI artifact directory. These automated checks do not establish
native Windows interaction/rendering, every physical desktop theme or full
release/package qualification. No vendor code or runtime causal semantics changed.

Commands used for the primary checks:

```bash
cmake --build build-release --target build-tests --parallel 2
ctest --test-dir build-release --output-on-failure -j 2
cmake -S . -B /tmp/s7-st007-headless -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/s7-st007-headless --target s7core_metadata_json_smoke s7core_smoke s7io_smoke s7net_smoke s7noise_smoke --parallel 2
ctest --test-dir /tmp/s7-st007-headless -R '^(s7core_metadata_json_smoke|s7core_smoke|s7io_smoke|s7net_smoke|s7noise_smoke)$' --output-on-failure
cmake --build build-win-release --target s7studio s7core_metadata_json_smoke --parallel 2
ctest --test-dir build-win-release -R '^(s7core_metadata_json_smoke|s7studio_smoke)$' --output-on-failure
```

The scaled GUI command is documented in [STUDIO_GUI_TESTING.md](STUDIO_GUI_TESTING.md).
Developer, plugin and offline Studio guides describe the JSON workflow and
information access. ST-008 drag placement is recorded in the following increment.

## Implemented increment — ST-008, library drag placement, 2026-10-25

Library leaf entries now support native copy drags into the receiving root or
open nested diagram. The view maps the pointer through its current zoom/pan;
the existing scene path snaps the element's upper-left anchor to the grid and
creates one undo command. The new instance is selected and the canvas receives
keyboard focus. Categories cannot start drags; Escape, outside drops and
unsupported data do not edit the document or undo history. Double-click remains
available and uses the same creation function with its free-spot suggestion.

The shared path allocates unique IDs (including avoiding text-frame ID
collisions), copies registered schema definitions, preserves ordinary metadata
defaults and initializes Scope/Live channel counts as before. It does not create
runtime/plugin instances during placement. All changes stay within Studio and
its documentation; no vendor, runtime or network behavior changes are involved.
The short description label now wraps within the existing dock width, so
selecting a long description does not move the canvas/library boundary under
the pointer.

The app-local drag payload is `application/x-s7studio-library-element`, a UTF-8
JSON object with exactly `version: 1` and a `type` string, at most 4096 bytes.
Type identifiers are registered runtime types or `StudioObserver:<viewer type>`.
Drag-enter, movement and drop require copy support and a currently available
entry. Drop rechecks availability and rejects out-of-viewport positions. No
serialized node objects, parameter overrides or plugin-loading requests are
accepted. Implementation lives in `library_element`, the project-owned library
tree and DiagramView; both placement gestures call DiagramEditor::addLibraryElement.

Verification: native Release `build-tests` and the complete CTest suite pass
**82/82** (110.82 s), including all four GUI suites and **53 workflow cases**,
with no skips. Nine native XTest/QDrag cases cover filtered/unfiltered entries,
zoom/pan, root/nested targets, built-ins, schema elements, the loaded noise
plugin and all four observers. Checks include snapped coordinates, stable dock
width, selection/focus, unique IDs, exactly one undo step, redo, double-click
defaults and save/reopen. Further cases cover Escape, outside/category drags,
bad MIME/version/size/structure, move-only offers, missing types and entries
removed after drag-enter. The native gesture cases require `xdotool` and
explicitly skip without it; it was available for these runs.

Representative filtered built-in and nested-schema drags also pass at 150% and
200% using a 1440×900 logical Xvfb workspace. Coordinates for XTest are converted
to physical pixels; application placement remains in logical/scene coordinates.
Windows Studio Release cross-build and **1/1 Wine Studio smoke** pass. Native
Windows drag/drop, physical desktop/GPU themes and relocated release-package
qualification remain separate. Captures and test logs remain outside Git in
`/tmp/s7-st008-*` and the usual GUI artifacts directory.

Primary verification commands:

```bash
cmake --build build-release --target build-tests --parallel 2
ctest --test-dir build-release --output-on-failure -j 2
cmake --build build-win-release --target s7studio --parallel 2
ctest --test-dir build-win-release -R '^s7studio_smoke$' --output-on-failure
```

The offline user guide describes drag placement and cancellation; the GUI
testing guide documents native-event dependencies and scaled runs. The maintainer
subsequently moved increment 6 (instrument controls) ahead of 5a/5b. That controls
increment and the subsequent 5a/5b work are implemented below.

## Implemented increment — ST-003 and scoped ST-004, 2026-10-25

The Live Oscilloscope now has eight channel selectors with independent visibility
and one selected channel's display controls. Non-wrapping gain/offset knobs sit
beside wide, exact decimal/scientific fields. Offset uses an explicit local
interval and editable half-span. Numeric edits are not quantized through the
dial; endpoint key/wheel actions and mouse release apply the final position.
Pre-trigger has a synchronized slider/percentage field; edge and A/B selection
use labelled exclusive buttons. Independent sections collapse and scroll.
Spectrum and paired-analysis settings wrap inside bounded-height, collapsible
scroll areas, preserving plot height on small desktops. Busy indicators retain
their layout space so pending analysis does not move the plot.

Inspection has independent A/B sliders and exact indices, preserving cursor
identity across crossings and existing raw measurement semantics. Export shows
its selected format in a split-menu button and reports worker progress beside
Cancel. Finite execution shows completed/total progress and model time;
continuous execution shows steps/model time without a completion percentage.
Existing trigger preset persistence and acquisition/analysis semantics remain.
The [design notes](STUDIO_INSTRUMENT_CONTROLS.md) record reviewed hardware and
software screenshots, including Rigol, Scopy, OpenHantek and ngscopeclient.

Verification was batched. The integrated Release suite passed **82/82**
(110.99 s), including **56 GUI workflow cases** without skips and the existing
live UI responsiveness workload. Screenshot review then led to wider numeric
fields, bounded analysis sections and stable processing-indicator geometry.
Final affected observer/inspection/trigger workflows passed **9/9**, the exact
value/cursor/trigger executable passed **15 cases**, and representative layout
checks passed **5/5 at each of 150% and 200%**. The scaled Xvfb workspaces are
1440×900 logical; the cases also resize to 1024×768 and explicitly set light/dark
palettes. The XY hover test follows current plot geometry as Qt Quick settles
after Fit. Checks include full gain-value visibility, plot height, field bounds,
raw-value preservation, cursor crossing/keyboard movement, export format/success
progress and an analysis indicator while work is queued.

Final Linux Studio and Windows Release cross-builds pass, as does **1/1 Wine
Studio smoke**. Native Windows interaction, physical display/GPU review and
relocated release packaging remain release qualification work. No vendor code,
runtime/core changes or Qt Quick Controls dependency were introduced. Offline
help and triage are updated. Final captures/logs are outside Git in
`/tmp/s7-controls-final-{100,150,200}`. The subsequent **5a — observer presentation
boundary** and **5b — Observers dock** are recorded below.

## Implemented increments 5a/5b — reusable presentations and Observers dock

ST-006 now replaces the Probes dock with **Observers**, retaining Alt+3 and the
right tool-window area. The root document owns the ordered preview selection,
collapse state and optional plotted-channel subset. Choose Observers supports
search, Apply and Cancel; no observers are selected automatically. Recorded
Signals preserves named-tap management and ad-hoc Scope/Phase access in a
separate dialog. Starting a run activates Observers once; subsequent sample
updates respect manual dock changes. Cards preserve active bindings through
document changes/deletion, label next-run differences, and retain stopped or
completed data. Reset and document retirement invalidate pending work.

Time/XY previews reuse LinePlotView; 3D full windows and cards now share the
extracted TrajectoryPlotView. Cards never reparent a full viewer. Their zoom,
freeze and camera state are independent. ViewerRegistry owns preview factories;
ViewerManager owns live trigger settings and applies saved presets before the
producer starts, including short runs without an open full viewer. Opening a
presentation neither rearms nor reconfigures retention. Full-viewer trigger
edits use the same owner and invalidate old preview preparation.

ObservationSource now maintains bounded independent tails, including sparse
channels. Worker preparation reads those tails without journal I/O or scanning
from the start. Live cards use const acquisition snapshots. Same-step phase
joins do not invent coherent frames from independent latest values. Time traces
break at missing/nonfinite samples; isolated samples use dots. Phase plots show
the latest continuous segment. The dock reports source outputs, names, supplied
units, individual sample steps, loss/errors and trigger/partial-capture state.
Compact 3D axes and Time legends keep narrow cards readable; full source names
remain available in channel details.

At most four visible expanded plots are active; initialization is staggered to
one surface per panel tick. Refresh is capped at 10 Hz, eight plotted channels,
4,096 recent positions and 1,024 vertices per channel, with one active/replaceable
pending request per card. Hidden/offscreen/collapsed/minimized surfaces stop
preparation and are retired safely. Requests carry immutable bindings and
revision tags; late results are rejected. Quick scene creation is guarded against
host destruction, and surfaces retire after their current GUI event unwinds.

`diagram.observers_panel` round-trips through scene serialization and undo.
Deletion/type changes prune selection in the same undo command; undo restores
it. Clipboard/library export omit root panel selection, and deployment projection
strips it. Graph/configuration projection equality is tested. Dock geometry stays
in QSettings. The [offline guide](STUDIO_OBSERVERS_DOCK.md) documents the workflow.
Live observer rebinding is explicitly deferred beyond 0.1.1, with no target
release assigned; the maintainer's follow-up is in
[OPEN_PROBLEMS.md](OPEN_PROBLEMS.md#live-observer-rebinding--follow-up-to-011).

Verification: the grouped native suite passed **83/83** entries after rerunning
two recording regressions with disk-backed temporary storage (their initial
failures were a `/tmp` quota exhaustion). Its main GUI workflow run passed
**58 cases**, without skips. After final presentation refinements, the rebuilt
source passed the observer contract test, both dock workflows at 100%, 150%
and 200%, ASan/UBSan contracts and dock workflows, and the Windows cross-build
plus Wine Studio smoke. Final captures and sanitizer reports are under
`build-release/observer-artifacts/`; these are local, untracked artifacts.
The final eight-channel 100 ksample/s GUI stress case with both a full viewer
and dock preview passed its 35-second workload with a 124 ms maximum UI timer
gap.

A 30-minute live-only published-source soak selected 12 observers, held the
four-active-plot limit, produced no recording samples or journal payload and
achieved 1,000.001 source steps/s. Maximum measured UI timer gap was 195 ms.
Process RSS was 595 MB after warm-up, peaked at 644 MB and finished at 513 MB;
these figures include acquisition buffers and Qt. This soak preceded the final
label/legend refinements. The mixed preview-only two-minute measurement achieved
1,000.007 source steps/s, with a 158 ms maximum UI gap and 422→451 MB RSS;
its recording payload grew separately by 175 MB. These synthetic published-source
measurements do not measure NodeRuntime throughput or physical GPU behavior.

The final two-minute mixed run with four full viewers open maintained four
active previews and 1,000.007 published source steps/s, with no capture errors.
However, its **458 ms maximum UI gap exceeds the 250 ms target** (hidden baseline:
11 ms). RSS grew from 928 MB after warm-up to 3,026 MB, with a 3,053 MB peak;
the existing recording captured 5,466,825 samples / 175 MB payload. Full-viewer
data/geometry and Qt allocations are included in RSS; this run does not isolate
preview overhead. The result is retained in
`build-release/observer-artifacts/mixed-full.json`. Full-viewer mixed-load
responsiveness and memory attribution remain **unresolved release qualification
work**, rather than a passed performance gate. See OPEN_PROBLEMS.md.

### Mixed-load qualification follow-up — 2026-10-27

A full-viewer-only mode was added to the opt-in observer benchmark, preserving
the same synthetic 1 kHz sources and four open full viewers while keeping the
dock hidden. The 60-second measurement reached a 187 ms maximum UI gap and
823 MB peak RSS. Isolated 30-second runs identified Scope as the main geometry
load (258 ms, 1,815 MB peak RSS) and Phase Portrait as the next contributor
(175 ms, 586 MB final RSS); 3D and Live viewers were below 100 ms individually.

Scope Time now reduces each displayed series by physical plot columns, keeping
the first/last sample, per-column minimum and maximum, and a gap marker.
Phase Portrait reduces its drawn trajectory to a plot-size budget while
retaining ordered local X/Y extrema and endpoints. Zoom and resize recalculate
the display from Scope's bounded preview or zoomed spill-file readback and
Phase Portrait's retained points; capture, source history and export remain
at full resolution. The isolated Scope run fell to 107 ms and 452 MB
peak RSS; Phase Portrait fell to 41 ms and 339 MB final RSS. Focused tests
cover peaks, gaps, endpoints, export, trajectory order and extrema.

The two-minute mixed workload with four full viewers and four active previews
then sustained 1,000.007 published source steps/s with no capture error,
5,461,911 recorded samples / 174.8 MB payload and 948 MB peak RSS on the final
guarded run (`qual-guarded-120.json`). Its maximum UI gap was **310 ms**, with
seven gaps over the 250 ms target. A prior candidate run reached 279 ms with
seven gaps; an unguarded title-update run reached 1,528 ms with four gaps,
including two over one second. The latter illustrates run-to-run sensitivity,
so the result cannot be reported as passing. Timestamped
60-second traces placed the longer gaps shortly after dock scrolls. A 5 Hz
dock refresh, a shared preview QML engine, and retained hidden dock surfaces
were tried locally, then removed: their full two-minute measurements still
exceeded the target. The retained-surface run reached 293 ms with six gaps
over 250 ms and 1.04 GB peak RSS (`qual-cache-120.json`); this weakens the
initial scene-construction hypothesis. In one diagnostic 60-second run the
panel refresh itself stayed below 50 ms while the maximum UI gap was 237 ms;
the 120-second failures remain unexplained. The gate remains open pending isolation
of the remaining UI work or an
explicit release-scope decision. These measurements used Xvfb/software OpenGL
at 2000×2600 and do not qualify a physical GPU, native Windows, or NodeRuntime
throughput. JSON evidence is in `build-release/observer-artifacts/qual-*-*.json`
outside Git; commands are in STUDIO_GUI_TESTING.md.

The next implementation increment is **7 — release qualification**. Native
Windows interaction, physical desktop/GPU review and relocated package checks
remain separate from the local implementation evidence above.

## Implemented refinement — ST-011/ST-013/ST-014/ST-015, 2026-10-25

Element Properties now has an editable diagram display Name and separately
labelled Type, Library and Internal ID. Renaming leaves internal IDs and their
references intact. Module-owned metadata resources declare their library;
schema registration supplies the schema library's name, and the plugin loader
identifies loaded plugin ownership. Missing provenance is explicit. F4/Enter
opens Properties for schema and observer elements as well as runtime elements;
double-click still opens nested schemas and observer viewers.

Diagram > Diagram Properties edits the root schema name and a multiline diagram
description. The optional top-level `description` string is owned by
DiagramDocument and preserved independently of runtime schema semantics. Missing
means empty; a present non-string is an error. Metadata edits share root undo
history and activate the root editor on commit, retaining nested tabs. The root
tab/window title uses schema.name, with file-basename and Untitled fallbacks,
and refreshes after loading, renaming, Save As and close.

Tools is a top-level menu between Nodes and Settings. Its existing tool-window
actions, shortcuts and visibility states are preserved; View retains layout
commands. Selecting a text frame reveals the drawing toolbar. The frame text
editor includes font and size controls with a selection-preserving preview.
Text and style changes now share undo/redo, cancellation and file persistence.
The user guide and embedded offline help describe the updated workflows.

Verification: native Studio Release build, seven focused core/metadata/diagram/
document/schema/lifecycle/demo CTest entries, and the out-of-tree spectral SDK
smoke pass. The full GUI workflow suite passes **64 Qt Test cases**, without
skips, including four local refinement workflows for metadata, identity,
text/Tools and nested schemas. The final font-size preservation adjustment passes
the affected workflows again (**9 Qt Test cases**, no skips). Local GUI helpers and captures stay
outside Git under `build-release/ui-refinement-*` and `/tmp/s7-ui-*`; existing
menu expectations and model/serialization regressions are updated in the
repository. Native Windows/physical desktop qualification remains separate.

## Live time/preview correctness refinement — 2026-10-25

The maintainer authorized the five reported time/preview issues. Live Time keeps
its seconds/division setting across source replacement and dt changes, including
triggered views; preview bounds also use a model-time interval. Raw acquisition,
trigger capacities, FFT/correlation retention and simulation stepping are
unchanged. Partial history is explicit. Magnifying a retained raw interval now
uses original samples, while the wider overview remains independently available;
older intervals without fine samples keep the selected display envelope and an
explicit limitation message. Real gaps are never joined.

The Time knob now has a prominent adjacent exact `s/div` field. Its direction
matches the local Rigol DS1000Z guide (HORIZONTAL SCALE, page 1-14): clockwise
reduces the time base. Range updates synchronize dial and readout, and invalid
signed values cannot silently become valid positive values. Preview navigation
preserves Y for Live Oscilloscope; XY/phase navigation retains both axes.
Recorded Signals is explicitly the named wire-tap list, with an actionable
empty state and unchanged recording/viewer access for configured taps.

Verification: Studio Release rebuild and **10/10** affected module CTest entries
pass, covering Live acquisition/worker/hover behavior, trigger capture, previews,
Scope/viewer/lifecycle, FFT, paired analysis and inspection/export. The full
local GUI label passes **4/4** suites (**66 workflow cases**, no skips), including
Qt Quick line/3D rendering and artifact self-checks. The first sandboxed GUI
invocation could not allocate an Xvfb socket; the permitted rerun passes.

Before/after focused checks reproduce and resolve coarse zoom (six line
intervals despite 50 available raw samples), time-knob direction/readout sync,
preview Y scaling and the unexplained Signals list. `2int_osc_live` runs for 60
model seconds at dt=0.01/0.001/0.0001 s with the same 20-second view and three
rising crossings; a mouse-selected span survives source replacement. Exact
entry, invalid values, repeated zoom, real wheel dispatch, trigger-mode dt
changes and named-tap capture/reopening pass. Rendered before/after sine and
input images were reviewed. New GUI helpers/captures remain local under
`build-release/scope-fixes-*`; permanent data/worker regressions are in the
module tests. Native Windows/physical desktop qualification remains separate.

## Rigol controls, item 6 increment 6a — 2026-10-25

The maintainer requested small, resumable increments to conserve usage. The first
increment groups Time controls under Vertical/Horizontal, compacts channel
selection to four columns, and separates legend-name selection from visibility.
A fixed eight-division vertical graticule supplies per-channel raw units/division
readouts, including mixed and unspecified units. Existing independent gain and
position settings remain the display transform; the shared axis explicitly
shows display levels. Readouts stay in the Widgets panel so streaming scale
updates do not rebuild the QML legend model. Original samples and acquisition
settings are unchanged.

This increment is extended by 6b below. Remaining work includes coarse/fine
1–2–5 steps and position reset, followed by markers. The optional history
overview is a later increment.
The local DS1000Z manual's selected-channel Vertical controls (page 1-12) guide
this work.

Validation: Studio Release build, the Live Oscilloscope module regression and
Qt Quick line-plot regression pass. Seven focused GUI cases pass without skips,
covering mixed/unspecified units, independent gain, legend selection versus
visibility, raw-data preservation, light/dark 1024×768 layouts, long runs and
hover. Final grid-label formatting and compact legend layout pass five affected
GUI cases again; the rendered capture was reviewed. Local helpers/captures are
under `build-release/rigol-6a-*` and remain outside Git. Broad suites are not
repeated for this small presentation increment.

## Rigol controls, item 6 increment 6b — 2026-10-25

The Vertical Scale field now accepts exact units/division entry. It adjusts
only the selected channel's gain and disables Auto Y while holding the plot
range fixed. Exact entries survive selection and position changes, synchronize
with independent gain/range changes, and support scientific notation. Streaming
range changes leave partially typed text intact; commit uses the current range.
Invalid or unrepresentable values leave the applied gain unchanged.

The selected channel supplies the Y-axis title, color and raw-unit tick labels,
including its independent gain and position. Selecting a different channel
changes labels without moving traces or changing their visibility. The shared
graticule and plotted display transforms remain unchanged; raw acquisition,
measurements, trigger thresholds and exports are unaffected. Generic line plots
retain their existing axes. The user guide describes the controls.

Item 6 remains in progress. Resume with 6c: coarse/fine 1–2–5 steps and position
reset, followed by position/trigger markers. The optional history overview
remains later work. Keep subsequent work in similarly bounded increments.

Validation: Studio Release build and five focused GUI cases pass without skips.
Coverage includes exact entry and invalid/range-limit input, active editing
during range changes, independent gains, mixed/unspecified units, selected-axis
gain/position mapping, unchanged raw samples, light/dark controls and a long run.
The Live Oscilloscope module, generic Qt Quick line-plot and bundled-demo checks
pass. Rendered labels and controls were reviewed at 1024×768. Local helpers and
captures remain outside Git under `build-release/rigol-6b-*`; broad suites are
not repeated for this bounded increment.

## Rigol controls, item 6 increment 6c — 2026-10-26

The larger vertical Scale knob now adjusts the existing units/division value
instead of a continuous gain multiplier. Vertical scale and horizontal timebase
default to coarse 1–2–5 stepping, with independent Fine buttons selecting 100
logarithmic steps per decade. Clockwise reduces units/division or seconds/division.
Exact numeric entry is preserved; changing adjustment mode leaves values intact.
An off-ladder exact value steps to the next stop in the requested direction,
and non-ladder range bounds remain reachable. Keyboard, numeric spin arrows and
mouse drags use the selected mode. One wheel detent applies one scale step,
independent of desktop text-scroll settings; partial wheel deltas accumulate.

The smaller position knob has a Zero button and double-click reset. Both reset
only the selected channel's display offset. Exact gain remains a separate field;
its range, scale conversion and selected-channel axes remain unchanged. Fine/Zero
sit beside the vertical headings to keep the controls compact. Controls use
existing presentation settings, with no changes to sampling or causal execution.

Item 6 continues with channel-zero and trigger-level/time markers. The optional
history overview remains later work.

Verification: native Release `build-tests` and **83/83 CTest entries pass**
(80.09 s, eight jobs), including all four GUI suites and the live responsiveness
workload. An initial run exhausted the temporary-storage quota and encountered
an empty executable left by an interrupted sandboxed build; after rebuilding
that generated executable, the complete run passed with `TMPDIR` on the project
disk. No production code was changed for those environment failures.

After the final heading/spacing adjustment, Studio rebuilds, the Live
Oscilloscope module passes again, and seven focused rendered cases pass at
100%. Four cases pass at each of 150% and 200%, without skips. Checks cover
coarse/fine and exact values, one edit per key action, single and partial wheel
detents, clockwise dragging, non-ladder bounds, reset, channel isolation,
selected-axis units, raw-sample preservation, frozen/finished sources and the
600-second saved symplectic oscillator workflow. Screenshots were reviewed at
1024×768; the larger-scale runs use a 1440×900 logical workspace. Light/dark
behavior checks pass, but screenshot review exposed low-contrast labels with
the synthetic light application palette; this is tracked as ST-016 rather than
claimed resolved. Local helpers/captures remain outside Git under
`build-release/rigol-6c-*`. Windows and relocated package qualification are not
repeated for this increment.

## Compact Live Oscilloscope controls — 2026-10-26

Maintainer-requested follow-up to 6c: scale, position and timebase knobs now all
use 72×72 logical pixels. Input fields use compact, font-based heights and less
vertical padding. Reduced section/page spacing leaves room for the default
Time controls and collapsed settings headings above the sample readout.

The former fixed eight-line Time readout now starts at approximately three
lines, below an adjustable vertical divider. Drag to enlarge it or down to hide
it and give the settings more room. Settings scroll when sections expand; the
readout and controls occupy separate splitter panes. Hover text does not resize
the panes or plot. Analysis readouts retain their existing sizing. These are
presentation changes; coarse/fine behavior, exact entry and sample data remain.

Verification: native Studio Release rebuild and the Live Oscilloscope module
pass. Existing light/dark instrument, 600-second symplectic oscillator and
prototype workflows pass. Local rendered geometry checks pass at 100% and 200%,
including eight channels at 1024×768, equal knob sizes, compact field heights,
all default Time controls/headings above the readout, divider hide/restore,
expanded Trigger scrolling and stable hover geometry. Captures/helpers remain
outside Git under `build-release/compact-live-*`; broader suite, Windows and
packaging checks are not repeated for this layout-only follow-up.

## Rigol controls, item 6 increment 6d — 2026-10-26

Time now has colored channel-zero badges, an orange L trigger-level handle and
an upper P pre-trigger percentage handle. Coincident channel badges fan out in
a reserved margin while leaders/arrow tips indicate their true raw-zero levels.
Dragging selects and moves one channel; double-click/Home resets its position.
Off-screen handles indicate direction and can be dragged into range. Channel
visibility, independent gains, raw data and the selected-channel axes are kept.

Trigger handles preview during drag and apply once on release through the
existing owner/validation path. L uses the trigger channel's gain and offset,
independently of selected-channel axes, with original-unit hover help. P edits
the next acquisition's pre-trigger percentage. The captured T reference remains
at the sampled event, including after pan/zoom. Release has the existing fields'
clear/rearm/leave-Freeze behavior; canceled or rejected pending edits preserve
the accepted settings. Source/preset revisions invalidate stale gestures.
Unrelated draft fields are not committed. Rejection appears in compact status.

Mouse, keyboard and accessibility actions are supported without graph pan/zoom
or cursor-placement conflicts. Generic plots and previews have no new handles.
Runtime stepping, source sampling, analysis retention and raw measurement/export
semantics are unchanged. No vendor code or Qt Quick Controls dependency is added.

Narrow Time plots now number fewer X ticks without rounding away precision;
subticks preserve ten model-time divisions. This keeps fractional trigger-fit
labels readable after reserving the marker margins. All observation-view
callbacks are disconnected before any plot is destroyed: XY hover teardown
previously reached the already deleted Time instrument bridge, as caught by
the full GUI workflow during this increment.

Verification: native Release `build-tests` and Studio build pass. The final
**83/83 CTest entries pass in 117.60 s with two jobs**, including all four GUI
suites and 58 GUI workflow cases without skips. The 35-second eight-channel
100 ksample/s plus unthrottled workload records **87 ms maximum UI gap** in that
run, against the unchanged 250 ms test limit. One eight-job run recorded a
370 ms gap and failed this check; an isolated rerun passed at 133 ms. Keep that
parallel-load outlier explicit; these checks do not qualify arbitrary host load.

Three local marker-gesture cases pass at each of 100%, 150% and 200% with no
QML warnings. They cover coincident badges, direct arrow-tip and badge drag,
selection/reset/keyboard behavior, hidden/off-screen channels, raw-value and
other-channel preservation, trigger-channel calibration, preview versus commit,
owner rejection, Escape, stale source/preset edits and captured-T identity under
pan/zoom. Tick stride/subticks retain ten divisions. Final screenshots were
reviewed at 1024×768 logical pixels. Helpers/captures remain outside Git under
`build-release/rigol-6d-*`. The user guide and trigger help describe the handles;
Windows, sanitizer and relocated package qualification are not repeated here.

## Live controls wheel routing follow-up — 2026-10-26

Wheel events delivered to a focused observation plot now use the pointer's
global position to find the receiving widget within that viewer window. Events
over controls propagate through labels and blank content to the scroll viewport,
skipping disabled editors. The instrument panel contains unhandled events,
including those at scroll limits. Normal plot zoom and enabled controls' wheel
adjustments remain available.

The initial native X11 left/middle/right scrolling checks passed; a synthetic
focused-plot fallback event over the sidebar reproduced the lost scrolling
before this fix. Final local checks pass at 100%, 150% and 200% display scale:
normal/wide sidebars, correct/stale plot-local coordinates, disabled editors,
native wheel dispatch, scroll limits, all four tabs and intentional plot zoom.
The Live Oscilloscope CTest and focused instrument/live-analysis GUI workflows
also pass. Studio builds successfully. Helpers and diagnostics remain outside
Git under `build-release/wheel-routing-*`; the full suite and Windows/Wayland
qualification were not repeated for this follow-up.

## Scope and decisions to review

Improve the existing local 0.1.0 workflows: edit, configure, run and observe.
Embedded views reuse existing observation capabilities. Remote node control,
restart, Trusted Networks, node configuration editing and other networking
work belong to [0.2.0](RELEASE_0.2.0.md), alongside printing, certificate tools
and parallel runtime execution. Menu cleanup may regroup existing network
actions without extending their behavior.

| Topic | Recommended decision |
|---|---|
| Application shell | Qt Widgets controls; retain Qt Graphs/Quick 3D for existing plots. Keep the current Qt 6.9 minimum. |
| Menu order | File, Edit, View, Diagram, Run, Nodes, Settings, Help. Nodes has its own menu so local execution and remote deployment have explicit homes. |
| Examples | File → Example Diagrams… opens the standard file chooser in the distribution's examples directory; descriptions and qualification remain in offline help. |
| Observers | Replace the Probes dock with a selectable list of compact observer previews in the right dock area. |
| Initial preview selection | None selected until the user chooses; never expand every observer automatically. |
| Preview persistence | Save ordered observer identities and collapsed state in diagram presentation metadata. Keep dock geometry in application settings. |
| Run layout | Activate Observers once when a local run starts; preserve results and subsequent deliberate panel changes. |
| Named signal taps | Preserve existing taps, recordings and ad-hoc Scope/Phase actions through Recorded Signals… inside Observers. |
| Instrument controls | Non-wrapping gain/offset knobs with exact numeric entry, a pre-trigger slider, short exclusive option groups, collapsible advanced sections and actual progress indicators. |
| Networking | Deferred to 0.2.0, including Trusted Networks, connection settings, runtime node configuration editing, remote control and restart. In 0.1.1, only regroup existing network actions during menu cleanup. |

## 1. Shell, menus and keyboard behavior — ST-001/ST-002/ST-005

Use one shared action per operation, with stable object names, real shortcut
registration, accessible names and consistent tooltips. Define top-level menu
order in one application-owned builder. Keep callbacks and their guards shared
with toolbars/context menus. Use an ellipsis for commands that open a dialog.

| Menu | Contents, in order; semicolons separate groups |
|---|---|
| File | Open Diagram…, Recent Diagrams, Close Diagram; Save, Save As…; Element Libraries → Load Element Library…, Save Diagram as Library Element…; Example Diagrams…; Exit |
| Edit | Undo, Redo; Cut, Copy, Paste, Delete, Select All; Element Properties…, Element Information… |
| View | Zoom In, Zoom Out, Actual Size; Fit Diagram to Window, Fit Page; Tool Windows, Hide All Tool Windows; Appearance → existing toolbar and tool-window-bar visibility actions |
| Diagram | Select/Move, Draw Wire; Insert Text Frame; Text Frame → existing text/font/size/color/fill/line formatting controls; Page → A5, A4, A3, Letter, then Portrait/Landscape |
| Run | Run Configuration…; Run, Pause, Resume, Stop; Reset; Keep Previous Scope Traces (checkable) |
| Nodes | Headless Nodes; Deploy to Nodes…, Open Deployed Diagram… |
| Settings | Scope Temporary Data Directory… |
| Help | S7 Studio Help; About S7 Studio…, About Qt… |

Save Diagram as Library Element describes the existing operation that packages
the root or active nested schema as an element definition. Loading a library
continues to add types to the catalog. Delete and Element Information expose
existing deletion and the shared information presentation specified below.
Formatting menu commands operate on the selected text frame; font and size
controls reuse existing values and apply immediately, as their toolbar does.

Page, drawing, selection, zoom and editing target the active diagram tab.
Local run controls and deployment target the root document. Keep Previous
Scope Traces and the Run Configuration checkbox use the same positive wording
and underlying value; the existing serialized clear policy can remain an
implementation detail. Its effect is on the next run, not an immediate clear.
Nodes → Headless Nodes shows the existing dock.

The Run toolbar keeps its six existing actions, in the existing order, using
icons only. Hovering enabled or disabled buttons shows their action description
and shortcut where assigned. Disabled icons remain distinguishable; action names
remain available to accessibility tools. Tooltips distinguish simulation pause,
display freeze, stop with retained results and reset of current presentation.

Zoom In accepts the standard platform bindings plus the main-keyboard
Ctrl+= alternative; verify actual Ctrl+Plus/Shift and keypad dispatch before
choosing explicit additional sequences. Ctrl+- remains Zoom Out. Shortcut
dispatch happens once per keypress. Cut/Copy/Paste/Select All/Delete/Undo/Redo
must operate on a focused text editor when appropriate and on the canvas when
the canvas owns the action. F4 retains the existing element operation; add
information access without changing observer/schema double-click gestures.

### Example chooser

Use the standard file chooser, as clarified by the maintainer during
implementation. Start in the distribution's examples directory and filter for
`.s7diagram` files. No separate example catalog, custom dialog or dialog header
is needed. The existing bundled examples guide and numerical validation matrix
provide descriptions and qualification limits through offline help.

Resolve examples from the installed package location relative to the executable:
Linux `usr/share/s7/examples` and Windows `share/s7/examples`; retain an explicit
development-tree path for development builds. The chooser must work after
relocation without depending on the working directory. Missing directories and
invalid files produce a visible error while preserving the current document.

Choosing Open enters the normal Save/Discard/Cancel replacement workflow.
An example opens as an unsaved working copy: Save asks for a destination and
does not overwrite the package. Cancel at either dialog leaves the document
and active run unchanged. Reuse the document-loading transaction; do not use
the demo loader's empty-schema fallback for a failed example open.

### Toolbar palette and text-frame defaults — 2026-10-24

The maintainer reported faint toolbar icons with a KDE palette. Preserve Qt/KDE
palette integration: draw the toolbar background with the Button role, render
neutral icons with ButtonText and disabled icons with Disabled/ButtonText, and
resolve colors at paint time so palette changes do not leave stale icon colors.
Keep Run/Stop accents legible against the same palette background.

Text frames and the toolbar's font-size control default to **10 pt**. This is
not a minimum: users may choose smaller sizes (the integer controls allow
1–72 pt), and existing smaller fonts in user documents remain valid. Update
bundled example text frames to at least 10 pt, preserving larger sizes. The
inventory contains 62 text frames; 60 already used 10 pt, and the two 9 pt
frames are raised to 10 pt. Diagram zoom continues to scale text normally.

### Integrator research scope correction — 2026-10-25

The maintainer clarified that integrator and feedback-settling research is a
separate project with no release assignment yet, outside both 0.1.1 and 0.2.0.
This supersedes the earlier planned oscillator accuracy investigation/example
increment (3b); it is removed from this release's implementation sequence and
acceptance gates. Research proceeds on an isolated branch, with findings reviewed
before any release integration is considered.

The research questions remain in [OPEN_PROBLEMS.md](OPEN_PROBLEMS.md).
[SIMULATION_MODEL.md](SIMULATION_MODEL.md) retains the analog-computer contract,
Siebert reference, reading status and attribution rules. Existing integrator
documentation and regression coverage remain relevant to Studio maintenance.

## 2. Element information and placement — ST-007/ST-008

The library keeps its searchable category tree, short selection summary and
double-click insertion. Right-click on an element offers Element Information…
for the clicked row. Categories and blank space offer no element action.
The same information is reachable by keyboard/context-menu key and from a
selected canvas element through Edit. Schema and observer double-click keep
opening their existing nested editor or full viewer.

Properties uses a vertical splitter: identity and a scrollable, selectable
documentation area above the existing parameter editor; dialog buttons stay
outside both scrolling areas. Initially allocate about one third of the content
height to documentation, with a compact minimum so parameter controls remain
usable. Information uses the same documentation component in a resizable
read-only dialog. The library's short summary is not the only access to long
third-party documentation. Display supplied text as plain text; do not execute
HTML or load external content from descriptions.

Documentation order: short purpose, inputs/outputs and timing/state notes,
then the complete JSON parameter reference and small examples where useful.
Every accepted key needs its exact spelling, type, default/required status,
units, bounds/choices, array/object structure and conditional interactions.
Include type-variable pins, variadic counts, schema parameters, plugin settings
and observer settings such as saved triggers. Distinguish a controller signal
input from a configuration parameter.

Use ElementCatalogService/ElementMetadata/ParameterSpec and ViewerRegistry as
the shared source. Audit metadata against the actual JSON consumers and schema
definitions; incomplete metadata must be corrected at its owner. Keep a coverage
inventory during implementation (type, accepted keys, documentation reviewed,
example checked), including built-ins, bundled schema library, s7io/s7net,
bundled noise and reference spectral plugin, and Studio observers. Preserve
third-party descriptions; label unavailable documentation explicitly. Avoid a
second hardcoded prose table in MainWindow.

Library dragging uses a Studio-owned MIME payload containing a version and
registered type identifier, with copy semantics. Only leaf entries can start
a drag. Validate payload size, version and type availability before insertion;
do not deserialize node objects or load plugins from drag data. The drop target
is the receiving diagram editor, including an already open nested editor.
Map viewport coordinates through current zoom/pan, snap using existing grid
rules, and invoke the same creation/default/schema-definition logic as
double-click placement. One drop creates one uniquely identified element in
one undo command. Cancel, unsupported payloads and outside drops create nothing.
No vendored QSchematic edits are required.

## 3. Observers panel — ST-006

Use the right tool-window area currently shared by Element Library and Headless
Nodes. Rename the dock and its menu/stripe action to Observers. Keep the existing
Probes dock keyboard slot for this replacement. The panel always refers to the
root document and its local run, even while a nested schema tab is active;
show the root diagram name in its header. Nested definitions do not acquire
runtime preview instances through this change.

Proposed layout (labels are illustrative):

```text
Observers                         [Choose Observers…]
Root: pendulum     Run 12 · Running · step 28000 · 28 s
----------------------------------------------------
v motion · Scope                       [Open] [Remove]
  [compact time plot, channel legend and units]
  > Channels / source outputs / latest sampled values
v phase · Phase Portrait                [Open] [Remove]
  [compact XY plot]
----------------------------------------------------
Recorded Signals…
```

Choose Observers opens a searchable checklist of root observer instances,
showing instance ID, type and connection state. Apply changes selection only;
Cancel changes nothing. Cards follow root diagram element order initially;
preserve the user's chosen order if the chooser provides move-up/down controls.
Removal hides a card and unchecks its selection; collapse keeps the selection.
Do not delete elements, disconnect channels, change recording, or open every
full viewer as a side effect. Selection can change during a run using only its
already prepared sources. New/rewired observers show Next run rather than
silently attaching a different signal to the active run.

The maintainer accepts this fixed-binding limit for 0.1.1 only. Live observer
rebinding is a required follow-up, with no release assigned yet; it is tracked
in [OPEN_PROBLEMS.md](OPEN_PROBLEMS.md#live-observer-rebinding--follow-up-to-011).

No initial selection: explain that no previews are selected and offer Choose
Observers. No observers in the document: explain how to place one from the
library and connect its inputs. An unconnected selected observer remains
visible with a useful status. Show channel source element/output, configured
name and unit (or unit unspecified); do not invent units.

### Preview content and ownership

Scope cards show a bounded recent time trace; Phase Portrait cards show an XY
trajectory; Phase Trajectory 3D cards reuse the existing 3D plot with a fixed
initial camera and full-viewer access. Live Oscilloscope cards show the existing
Time presentation, with rolling/trigger status. Spectrum, XY/correlation,
inspection/export and acquisition controls remain in its full viewer.
Previews state that they show a recent bounded interval. Full Scope recording
and comparison history remain available through Open.

Extract reusable plot presentation from current window classes where needed;
do not reparent the full viewer into the card. Full windows and cards have
independent zoom/freeze/clear state, while sharing the existing run sources and
channel bindings. Open raises the existing full viewer or creates it without
resetting the run or its data. Preview creation never configures retention,
trigger mode or rearm. One observer settings owner applies saved presets before
production, including short runs without an open full viewer; full-viewer edits
use that owner. This avoids conflicting configurations from two presentations.

Worker-published samples are the only source. Recording previews use const
ObservationSource access; live previews use read-only LiveAcquisition snapshot
operations. Keep GUI updates on the GUI thread and bounded preparation on the
existing analysis pool. Never read or step NodeRuntime from the GUI. Phase
coordinates and per-channel latest values retain their own source steps;
do not present independently sampled values as one coherent frame. Display
gaps, capture loss, nonfinite values and unavailable channels explicitly.

Proposed initial preview budgets, to validate during implementation:

- At most four expanded cards render concurrently in the visible viewport;
  further cards show an explicit resource-limit placeholder until scrolled
  into the active set. Selection itself is not limited to four observers.
- At most eight plotted channels per card; allow choosing the plotted subset
  for larger Scopes and label the subset. This never limits capture.
- At most 4,096 recent sample positions and 1,024 drawn vertices per channel
  in each card. Account for raw snapshots, reduced geometry and 3D buffers;
  live source reads remain bounded by existing acquisition limits.
- Refresh at most 10 times per second across the panel, with one active and
  one replaceable pending preparation request per active card. Requests carry
  run, binding and settings generations; stale completions are discarded.
- Collapsed, hidden, offscreen and minimized presentations stop polling,
  rendering and submitting analysis; their existing capture continues.

ObservationSource currently offers bounded sequential pages rather than a
per-channel tail index. Implement a Studio observation-layer bounded tail
projection/cache or indexed access as needed: a card opening late in a long run
must not scan the whole journal on the GUI thread or duplicate capture to disk.
Measure sparse channels separately; a global journal tail is not necessarily a
recent sample for every channel. This is a presentation support change inside
s7studio, not a change to the core observation threading contract.

### Lifecycle and layout

| Transition | Panel behavior |
|---|---|
| Run requested | After document/run preflight succeeds, activate Observers once and show Preparing. Do not move keyboard focus away from a deliberately focused editor. |
| Prepared → Running | Bind the immutable run configuration and its sources; clear old current-run values and show Waiting for first sample until data arrives. |
| Preparation fails | Keep the panel visible with the error and a route to Problems/Log. Previous results, if retained, are explicitly labelled as previous. |
| Pause / Resume | Keep cards and data; state changes on acknowledged worker transitions. |
| Stop / completion / error | Keep final data visible with Stopped, Completed or Error and completeness information. Do not restore the editing panel automatically. |
| User selects another dock during a run | Respect it; samples and status updates never reactivate Observers. |
| Reset | Clear current preview data, cancel pending work, invalidate generations and show Idle. Existing recording/source lifetime rules still apply. |
| Rerun | Activate Observers once for the new start, bind the new generation and reject late work from the previous run. |
| Edit/delete observer during a run | Keep active-run cards bound to their immutable snapshot, visibly mark document differences, and update next-run selection separately. |
| Replace/close document | Retire the run through existing lifecycle guards; release cards and pending work before loading another document. |

Recorded Signals… presents the existing standalone named taps with their
source and unit, rename/delete and Open Scope/Open Phase Portrait actions.
It preserves their existing capture and save behavior and current validation.
They do not become extra diagram observers or acquire implicit new recordings.
This dialog replaces the old flat dock's management access; the main panel
continues to list observer instances. Existing wire tap creation remains
available and existing documents retain their taps.

### Persistence

Add Studio-only `diagram.observers_panel` presentation metadata with an explicit
ordered selection of `{element_id, type, collapsed}` entries and optional
plotted-channel selections. An absent field means no selection; an empty list
means an explicitly empty selection. This is outside the runtime schema,
`run_configuration`, observer parameters and deployment payload.
Ensure the Studio deployment projection excludes this field; changing preview
selection must not change a deployed graph/configuration digest. Verify that
boundary with the existing deployment serialization path.

Selection/collapse edits mark the diagram modified and participate in the
existing save/replacement protection. Integrate the metadata in both scene
serialization and loading; do not rely on retaining an unmodified input JSON
object, since serializeScene rebuilds the document. Save/reopen and Save As
preserve it; clipboard/library export does not carry the root panel selection.
Undoing element deletion restores its applicable selection; confirmed deletion
prunes it from future saves. A reused ID with a different type is not selected
automatically. Preserve pending active-run cards until that run is retired.

Dock position/width and application geometry stay in QSettings; they do not
dirty the document. New observer viewports are created lazily. Do not create
one rendering surface for every chooser entry or selected offscreen card.

## 4. Instrument controls and visual rules — ST-003/ST-004

The maintainer requested this increment before the observer presentation boundary
and dock. Hardware and software references, selected-channel layout decisions
and value semantics are recorded in
[Studio instrument controls](STUDIO_INSTRUMENT_CONTROLS.md). The eight-channel
viewer shows one channel's knobs at a time; selecting a channel and showing its
trace are separate actions.

Keep the plot-first layout: tabs and plots take the expanding area; a hideable
right control column keeps Freeze Display, Resume Live/Fit and Auto Y visible
above independently collapsible Acquisition, Trigger, Channels and Inspection
sections. At narrow sizes, scroll the column vertically without clipping
numeric entry or dialog buttons. Full-viewer labels identify model time and
units; channel color supplements text rather than replacing it.

| Setting/workflow | Chosen presentation | Value/application rules |
|---|---|---|
| Channel display gain | Non-wrapping QDial plus existing numeric field | Dial position 0–1200 maps to `10^(-6 + position/100)` over the existing 1e-6…1e6 range. Exact entry retains the entered value; updating the dial must not round-trip and quantize it. |
| Channel display offset | Non-wrapping QDial plus exact numeric field | Dial controls an explicitly displayed local interval centered on a committed numeric value. Half-span starts at one raw signal unit and has a visible editable scale; it cannot auto-rescale under the pointer. Numeric entry retains the existing ±1e12 range. |
| Pre-trigger | Horizontal slider plus 0–100% spin box | Both update the same existing integer percentage. |
| Trigger edge and A/B selection | Exclusive labelled buttons | Preserve Rising/Falling and cursor identities, including when A passes B. |
| Trigger mode | Existing combo box | Rolling/Auto/Normal/Single remain visible and explicit; avoid a wide four-button strip. |
| Trigger level/hysteresis | Precise finite-number entry | Preserve current validation and previous effective value on error; arbitrary signal range makes a fixed slider unsuitable. |
| Retained duration/sample count | Existing exact entries with readable duration summary | Label retention separately from display zoom. State actual clamped capacity; shrinking can discard history, growing fills forward, model rate stays unchanged. |
| Pinned inspection interval | Two labelled sliders A and B plus exact indices | Reuse existing pinned-window/sample semantics, preserve crossing cursor identities, keyboard single-step and exact values. Avoid a new QML range-control dependency. |
| Export | Primary Export button with format menu; progress bar and Cancel | Show selected JSON/raw CSV/displayed CSV format explicitly. Percentage comes from the existing export worker. |
| Finite execution | Progress bar with completed/total steps | Continuous execution shows state and model step/time with no completion percentage. |
| Pending analysis | Indeterminate progress only while a request is pending | Distinguish processing, waiting for samples, frozen, complete and error. |
| Persistent toggles | Labelled checkboxes/checkable buttons | Use consistent checked/disabled states for visibility, Auto Y and peak hold. |

Gain and offset affect displayed values only. Knobs are a coarse adjustment
alongside exact fields. Clamp offsets at current supported limits; bound and
validate the offset dial half-span, and recenter only on committed numeric
entry or an explicit scale change. Use signal blockers/value ownership to avoid
feedback loops. Coalesce drag-triggered redraw requests through existing worker
scheduling; releasing a control or using keyboard steps must apply its final
value. Do not persist new viewer controls implicitly: preserve current saved
trigger presets, and keep other display controls session-local unless already
saved by their owning component.

Use project-owned Qt Widgets compositions and palette roles. No application
theme rewrite, LCD-number decoration or Qt Quick Controls dependency is needed
for this increment. QDial and QSlider provide integer positions, so floating
mappings are explicit. See the official [QDial](https://doc.qt.io/qt-6/qdial.html)
and [QSlider](https://doc.qt.io/qt-6/qslider.html) documentation. Existing plots
continue through [QQuickWidget](https://doc.qt.io/qt-6/qquickwidget.html);
additional rendering surfaces are budgeted and qualified rather than assumed
to be free.

## 5. Networking release boundary — ST-009 deferred

The maintainer clarified that remote node control, restart and other networking
work belong to 0.2.0. This supersedes the earlier inclusion of Trusted Networks
and runtime node configuration editing in the 0.1.1 proposal.

0.1.1 adds no Trusted Networks dialog, Studio connection-settings model,
credential/discovery settings changes, node configuration editor, remote
configuration/control/restart operation or new network qualification gate.
Those requirements are tracked in [the 0.2.0 plan](RELEASE_0.2.0.md).
Existing network commands may be regrouped by the menu cleanup; their behavior
and settings remain as currently implemented. Existing regression coverage
continues to run with the full suite.

## 6. Implementation boundaries and order

Work in small reviewable commits, updating relevant help with each delivered
workflow. The order follows the maintainer's accepted release sequence:

| Increment | Work and principal source areas | Completion evidence |
|---|---|---|
| 1 — Design | This plan, menu review and triage links | Review the menu, wireframes, persistence, release boundary and proposed budgets; settle remaining decisions before their implementation. |
| 2 — Isolated fixes | main_window actions, main_window_builder toolbar, Studio GUI workflows | Reproduce and fix actual keyboard dispatch; disabled-button tooltip and icon checks. |
| 3 — Shell/examples | main_window_builder, main_window document loading, packaged example lookup | Actual menu activation, focused editing and installed example loading/replacement protection. |
| 4a — Information | element metadata at its owning modules, element_picker_panel, shared information widget and Properties | Complete parameter inventory, representative semantic checks and long-text layout/accessibility. |
| 4b — Drag placement | element_picker_panel, diagram_editor/project-owned scene adapters, shared element creation | Real drag/drop, zoom/pan, root/nested placement, undo/redo and save/reopen. |
| 6 — Controls (moved forward by maintainer) | live_oscilloscope_window, spectrum/pair/inspection widgets, reusable control compositions | Exact numeric semantics, responsive dragging, export progress/cancel and unchanged raw measurements. |
| 5a — Observer presentation boundary | viewers registry/manager, reusable plot presentations, observation tail preparation and shared settings owner | Two independent presentations of one source; no capture/trigger changes on preview creation. |
| 5b — Observers dock | project-owned panel, tool_windows, main_window lifecycle, scene serializer/document metadata | Selection, run activation, state transitions, save/reopen, stale-work rejection and bounded-load evidence. |
| 7 — Release qualification | offline guide/example help, packaging scripts only where required, release evidence | Full regression and relocated packages, updated notices/checksums and recorded platform limits. |

Keep runtime/core execution and the headless build independent of Studio.
Documentation metadata changes stay at their definitions; only an actual
cross-module contract justifies expanding implementation scope. No edits in
`external/` are planned. Do not add protocol changes or a remote observation API.

## 7. Acceptance and packaging

The following are proposed gates, not results. Automated GUI work remains local
under [STUDIO_GUI_TESTING.md](STUDIO_GUI_TESTING.md); do not add hosted GUI jobs.
Every test process uses temporary state/settings directories rather than HOME.

| Area | Required checks |
|---|---|
| Actions/keyboard | Actual Ctrl+Plus/Equal/keypad and Ctrl+Minus dispatch below zoom limits; menu/wheel zoom; diagram versus text focus; nested editor targets; action enabled/check states through run transitions. |
| Menus/examples | All current operations reachable; Page scope and trace-retention policy correct; examples load from relocated package at unrelated working directories; chooser/replacement Cancel and failed Save/open preserve the document. |
| Information | Inventory every supported JSON key; verify semantic text for basic/stateful/integrator/schema/plugin/observer entries; context-menu target differs from selection; long third-party text remains readable, selectable and scrollable. |
| Placement | Mouse drag from filtered and unfiltered library to root/nested editors at different zoom/pan; unique IDs, built-in/schema/plugin/observer defaults; one undo/redo; save/reopen; invalid/cancelled/outside drops. |
| Observers | None/one/many selections; chooser Cancel; unconnected/invalid channels; current versus next-run bindings; root scope under nested tabs; full-viewer independence; named taps and ad-hoc actions preserved. |
| Lifecycle | Idle, Preparing, short run, Running, acknowledged Pause/Resume, Stop, completion, failure, Reset, rerun, observer edit/delete/undo, document replacement and running/paused shutdown; stale data never labelled current. |
| Layout | Start from Element Library, Headless Nodes and hidden docks; activate once; respect manual tab changes; keep stopped results; errors visible; restore selection and dock preferences without cross-document leakage. |
| Controls | Gain endpoints and non-grid exact values; offset center/span/clamping; keyboard/wheel/mouse; no feedback loops; retention/trigger/raw-data semantics; A/B crossing and single-step; export format/progress/cancel. |
| Visual/accessibility | Light/dark palettes at 100%, 150% and 200% scaling; 1440×900 and 1024×768 logical work areas; text/focus/disabled contrast; tooltip while disabled; keyboard traversal; no clipped values, buttons or long documentation. |

For performance acceptance, compare the same workloads with the panel hidden
and visible, with full viewers both closed and open:

- Two minutes with at least 12 selected observers (including 2D/3D/Live), four
  active preview cards and a 1 kHz model; exercise scrolling/collapse/selection.
- The existing 35-second, eight-channel 100 ksample/s live workload, measuring
  the added preview cost alongside its full viewer.
- A 30-minute continuous live-only soak with four active preview cards, so
  preview resource growth can be assessed without full-run recording growth.

Record achieved runtime throughput rather than assuming every graph sustains
the requested rate. After warm-up, retained preview allocations and pending
queue counts must stay within the stated budgets, with no new preview journal
files or monotonic preview-memory growth. Report existing explicit recording
growth separately in the mixed-observer check. Measure UI timer gaps; target a
maximum under 250 ms and compare against the hidden-panel baseline. Treat failure
as a design issue to resolve or an explicit scope decision, not a reason to
silently disable cards. These budgets need measured evidence before becoming
performance claims.

Build Studio and the headless configuration, build the appropriate test targets
and run the full Release suite after integration. Add targeted tests for new
lifecycle/data contracts and user gestures; do not add tests that merely mirror
widget construction. Run sanitizer coverage if changes affect source ownership
or worker lifetimes.

Both packages already include `share/s7/examples` (under Linux `usr`). Verify
example chooser lookup, descriptions/offline help, plugins, all four observer
preview types and full-viewer opening without source/build/sysroot paths.
Keep the Qt 6.9 API baseline and current QML imports. Windows packaging currently
removes QtQuick/Controls: the chosen Widgets controls require no new QML module.
If implementation introduces an import anyway, update deployment explicitly and
repeat relocated-package checks before accepting that change.

Cross-build Windows, run available Wine checks and record GUI blockers separately.
Native Windows interaction/rendering acceptance requires an actual native run;
Wine results do not establish it. Linux render/interaction tests and physical
display/high-DPI review are recorded with their environment and actual skips.

## 8. Design review completion

Before implementation, review the concrete recommendations in the decision
table and wireframes, especially initial observer selection, document-owned
preview persistence and resource budgets.
The maintainer settled the release boundary: remote control, restart and other
networking work, including ST-009 and runtime node configuration editing, belong
to 0.2.0. Section 5 records that boundary; the earlier proposed networking
implementation increment has been removed from this release.

Source inspection supports the implementation mapping above. The initial design
step was documentation-only; subsequent ST-001/ST-002 reproduction and regression
evidence is recorded above. The remaining design and broader release acceptance
are not implemented or qualified by that increment.
