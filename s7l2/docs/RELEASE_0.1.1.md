# S7 0.1.1 — interface design and implementation plan

Date: 2026-10-24. Status: ST-001/ST-002 implemented and locally verified; remaining
interface work is a proposed design for review.

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
| Examples | File → Example Diagrams… opens a searchable chooser with descriptions and qualification labels. |
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

Use a modal dialog with a search field, category/list pane, description pane,
Open and Cancel. Entries show title, relative bundled path and the existing
supported/experimental qualification. Use the bundled example documentation as
the content source. Show available `.s7diagram` files, including their variants;
do not treat all runtime fixture JSON files as user examples.

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
| 5a — Observer presentation boundary | viewers registry/manager, reusable plot presentations, observation tail preparation and shared settings owner | Two independent presentations of one source; no capture/trigger changes on preview creation. |
| 5b — Observers dock | project-owned panel, tool_windows, main_window lifecycle, scene serializer/document metadata | Selection, run activation, state transitions, save/reopen, stale-work rejection and bounded-load evidence. |
| 6 — Controls | live_oscilloscope_window, spectrum/pair/inspection widgets, reusable control compositions | Exact numeric semantics, responsive dragging, export progress/cancel and unchanged raw measurements. |
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
