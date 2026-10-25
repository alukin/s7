# Studio manual testing — 0.1.1 candidates

Started: 2026-10-24.
Status: ST-001/ST-002/ST-005/ST-007/ST-010 implemented and locally verified; other interface work remains in planning.
Release 0.1.1 is tentative; this log does not commit its scope or date.

## Planning started — 2026-10-24

The maintainer accepted the proposed release work order and requested step 1,
interface design and acceptance checks. The concrete proposal is in
[RELEASE_0.1.1.md](RELEASE_0.1.1.md), including menus, Observers layout and
lifecycle, persistence, controls, packaging and verification. Trusted Networks
and runtime node configuration editing were considered in the initial draft
and subsequently assigned to 0.2.0, as clarified below. Earlier collection-only
statements below describe the findings phase; planning is now authorized.
Proposed choices remain for review before their implementation.

## Networking scope correction — 2026-10-24

The maintainer clarified that remote node control, restart and other networking
work belong to **0.2.0**, outside 0.1.1. This includes ST-009 Trusted Networks,
Studio connection-setting changes and runtime node configuration editing.
The earlier request to include runtime configuration editing establishes the
requirement, but does not keep it in 0.1.1. The 0.2.0 plan now tracks it.
In 0.1.1, menu cleanup may regroup existing network commands without extending
their behavior. Earlier networking proposals below are historical design inputs,
not active 0.1.1 implementation requirements.

## Release scope clarification — 2026-10-24

The user clarified that this work is about improving existing features, not
adding new ones. Treat the released 0.1.0 feature set as the baseline for the
tentative 0.1.1 maintenance release: fix defects, improve readability, clarify
terminology, reorganize existing actions and refine controls for existing
settings. A new widget is acceptable as a presentation improvement when it
operates an existing feature; it must not introduce a new capability implicitly.

The recommendations below are a collected backlog, not an approved feature
list. In particular:

- ST-001/ST-002: the maintainer requested implementation of the shortcut and
  toolbar readability fixes after the design step.
- ST-003/ST-004: restrict maintenance planning to better presentation of
  existing settings and workflows. New tools, measurement functions or
  acquisition behavior remain future work.
- ST-005: reorganize and consistently label existing commands. A new blank-
  document workflow or other missing operation is outside this maintenance
  scope.
- ST-006: the Observers panel, embedded live previews of existing observers,
  user selection of displayed observers and automatic activation on run start
  are UX improvements within maintenance planning. The user explicitly
  clarified that embedding existing observation views does not constitute a
  new S7 feature. Reuse existing observation capabilities; plan the layout and
  lifecycle behavior before implementation.
- ST-007/ST-008: understandable element descriptions, right-click information
  and dragging existing library elements onto the canvas are UX improvements
  to element discovery, configuration and placement.
- ST-009: deferred to 0.2.0 by the subsequent networking scope correction.
  Trusted Networks and runtime node configuration editing are not 0.1.1 work.
- Printing is explicitly assigned to 0.2.0 below.

## Interface planning decision — 2026-10-24

The user confirmed that the proposed interface changes will be planned before
implementation. Continue collecting and analyzing findings for now. Before
coding these changes, prepare a concrete design and implementation plan covering
control choices, layout, scope, Qt Widgets/Quick integration, packaging and
acceptance checks. ST-002 through ST-008 are inputs to 0.1.1 planning; ST-009
is assigned to 0.2.0. Individual recommendations remain subject to the
maintenance scope and subsequent networking correction above.

## Findings

### ST-001 — Diagram zoom-in keyboard shortcut does not work

- **Reported:** 2026-10-24. In the diagram, `Ctrl+-` works but `Ctrl++`
  does not. Expected: the shortcuts zoom out and in, respectively.
- **Status:** shortcut variants independently reproduced on Linux with Qt
  6.11.2/Xvfb: Ctrl+= does nothing; Ctrl+Shift+Plus falls through to the vendor
  view's different zoom increment. Ctrl+Plus and keypad Plus use the action's
  intended increment. This reproduces key-interpretation defects, not the
  reporter's unspecified physical keyboard session. Fix implemented; local
  verification is recorded in [the release plan](RELEASE_0.1.1.md).
- **Resolution:** register all standard zoom bindings plus explicit Ctrl+=,
  Ctrl+Plus and Ctrl+Shift+Plus alternatives. Associate the actions with root
  and nested editors using widget-and-children shortcut scope; focused library
  text fields keep text input, while menu zoom remains available. No vendor
  edits. GUI checks exercise dispatch and require exactly one intended zoom
  increment per keypress, plus zoom-out, menu/Ctrl+wheel and nested routing.
- **Provisional severity:** minor usability defect: keyboard zoom-in is
  unavailable through the reported shortcut. Menu and wheel behavior have not
  been confirmed in the reported session.
- **Reproduction from report:** with a diagram active, press `Ctrl+-`, then
  `Ctrl++`; zoom-out works and zoom-in does not. Exact platform, build, keyboard
  layout, physical key combination (including Shift or keypad), focus and
  starting zoom are not yet recorded.
- **Original source evidence (before the fix):**
  - `s7studio/src/main_window.cpp`: Zoom In/Out actions each use
    `setShortcut(QKeySequence::ZoomIn/ZoomOut)` and call the active editor.
  - `s7studio/src/diagram_editor.cpp`: the editor's key handler delegates
    unhandled keys to `QSchematic::View`; `zoomIn()` itself calls
    `zoomBy(1.15)`. Ctrl+wheel uses the editor's zoom functions.
  - `external/QSchematic/qschematic/view.cpp` (read only): its fallback handles
    Control with `Key_Plus` or `Key_Minus`, with no `Key_Equal` case.
- **Original working hypothesis:** shortcut/key interpretation, particularly the
  plus/equal key and Shift or keypad modifiers. The action installs a single
  standard shortcut rather than explicitly supplying alternative bindings;
  the fallback also requires an actual Plus key event. This suggests a path
  to investigate, not a confirmed root cause. Focus, shortcut dispatch and
  zoom limits still need to be checked against the failing session.
- **Future acceptance check:** below the maximum zoom, verify diagram scale
  increases through the intended main-keyboard and keypad zoom-in shortcuts
  on the supported platform/layout, including Shift where required. Verify
  zoom-out, menu zoom and Ctrl+wheel, and check behavior with canvas versus
  text-field focus. Cover the actual shortcut dispatch rather than calling
  `zoomIn()` directly.

### ST-002 — Run toolbar disabled labels are barely visible

- **Reported:** 2026-10-24. Text on inactive Run toolbar buttons is barely
  visible.
- **Requested presentation:** use icon-only buttons in the Run toolbar and
  show descriptive text in tooltips on mouse hover.
- **Status:** requested icon-only presentation implemented. The original
  disabled-text contrast in the user's theme remains unmeasured. Local
  verification is recorded in [the release plan](RELEASE_0.1.1.md).
- **Resolution:** the six existing Run toolbar buttons use icons only. Run
  Configuration, Pause and Resume now have explicit descriptive tooltips,
  alongside the existing Run/Stop/Reset descriptions. Keep the original menu
  action text and Qt accessible names. Use Qt's existing tooltip behavior;
  no custom disabled-button event handling is needed. GUI checks cover real
  hover, enabled/disabled states and pause/resume/stop/reset button activation.
- **Palette follow-up:** the maintainer reported faint toolbar icons with KDE.
  Automated captures exposed a mismatch between pale icons and the painted
  background. Toolbars now paint the Qt palette's Button background; icons
  resolve ButtonText/Disabled ButtonText while rendering, and palette changes
  refresh the stylesheet background. Run/Stop accents are adjusted for contrast.
  Light/dark palette pixel checks and rendered captures pass. This does not
  claim qualification of every physical desktop/KDE theme.
- **Provisional severity:** minor visual/usability defect, with an explicit
  preference for a more compact toolbar.
- **Reproduction from report:** inspect inactive buttons in the Run toolbar.
  Exact platform, build, theme, display scaling and affected buttons are not
  yet recorded. Treat "inactive" as disabled pending confirmation.
- **Original source evidence (before the fix):** `s7studio/src/main_window_builder.cpp` constructs
  `toolbar.run` with `Qt::ToolButtonTextBesideIcon`. All six actions (Run
  Configuration, Run, Pause, Resume, Stop, Reset) already have icons. The
  window stylesheet sets disabled toolbar button text to `palette(mid)`.
  Run, Stop and Reset have explicit tooltips; the other three have no
  explicit tooltip assignment in this builder. `MainWindow::refreshActions()`
  sets execution action availability from the controller state.
- **Analysis:** `palette(mid)` is a candidate cause of low disabled-label
  contrast in the user's theme; actual rendered colors remain unmeasured.
  The requested icon-only presentation can be scoped to the Run toolbar.
  Preserve action names for menus and accessibility, and provide meaningful
  tooltips for every toolbar button. Verify tooltip availability while
  buttons are disabled, since those buttons prompted the report.
- **Future acceptance check:** the six Run toolbar buttons display icons
  without inline labels; each exposes its action description on hover,
  including while disabled. Check readable icons, distinguishable enabled
  and disabled states, useful accessible names, and unchanged Run menu labels
  and action behavior across idle, running, paused and stopped states.

### ST-003 — Give the Live Oscilloscope richer instrument controls

- **Requested:** 2026-10-24. Use Qt controls such as knobs and sliders to make
  the Live Oscilloscope more attractive.
- **Classification:** design/usability enhancement, not a reported functional
  defect. Candidate for discussion; inclusion in 0.1.1 is undecided.
- **Status:** request recorded; current UI source inspected. No implementation
  or visual prototype produced.
- **Source evidence:** `s7studio/src/live_oscilloscope_window.cpp` builds a
  QWidget control column with grouped forms, spin boxes, line edits and combo
  boxes. Channel display gain/offset use double spin boxes; trigger level and
  hysteresis use line edits; pre-trigger percentage uses a spin box. The
  control column can already be hidden to enlarge the plot. Studio already
  links Qt Widgets (`s7studio/CMakeLists.txt`).
- **Possible direction (analysis, not an agreed design):**
  - Knobs for channel display gain and offset, with visible numeric readouts
    and precise editable values; use a logarithmic mapping for the wide gain
    range and avoid wrapping at limits.
  - A 0–100% slider for pre-trigger position. Consider a trigger-level control
    with an explicit adjustable range and numeric entry, since signal ranges
    vary.
  - A time-span control with readable units and useful presets; distinguish
    display zoom from retained acquisition duration, since shrinking retention
    discards history. Retain exact duration/sample entry.
  - Consistent channel colors, compact grouping and clear active states while
    preserving plot space. Leave infrequent advanced settings in an expandable
    area if a later layout review supports it.
- **Feasibility:** Qt Widgets supplies
  [QDial](https://doc.qt.io/qt-6/qdial.html) and
  [QSlider](https://doc.qt.io/qt-6/qslider.html). Their integer control positions
  require explicit mappings to the existing floating-point values; visual
  controls should complement precise entry. A QML rewrite is not required to
  explore this direction.
- **Future acceptance check:** review an actual layout before settling scope;
  verify readable values/units, precise entry, keyboard access, theme/scaling
  behavior and plot space. Dragging controls should remain responsive during
  live runs. Display gain/offset must continue to affect presentation only;
  acquisition settings must retain their existing bounded-storage semantics.

### ST-004 — Adopt richer Qt controls throughout Studio

- **Requested:** 2026-10-24. Look beyond knobs/sliders and use other suitable
  Qt controls across the interface. This broadens ST-003 beyond the Live
  Oscilloscope; the user wants adoption as a design direction.
- **Classification/status:** interface enhancement; documentation and source
  review only. Implementation remains deferred under the collection-only
  instruction. Specific designs and release inclusion remain undecided.
- **Review basis:** official Qt Widgets and Qt Quick Controls documentation,
  plus Studio's live controls, spectrum controls, window inspection, element
  picker, tool panels, docking and packaging sources. No visual comparison or
  interactive prototype has been performed.

Proposed control-to-workflow mapping (recommendations, not implemented):

| Control | Suggested Studio use | Design detail |
|---|---|---|
| [RangeSlider](https://doc.qt.io/qt-6/qml-qtquick-controls-rangeslider.html), Qt Quick | A/B inspection interval on a pinned Time/Spectrum frame | Two handles plus exact index fields; preserve cursor identities and existing sample semantics. Qt Widgets has no equivalent two-handle QSlider; integration needs explicit design. |
| [Switch](https://doc.qt.io/qt-6/qml-qtquick-controls-switch.html), Qt Quick | Auto Y, channel visibility, Spectrum peak hold | Persistent on/off options with visible state and label. In QWidget panels, existing checkable buttons are a simpler option; a switch requires QML or a project-owned widget. |
| Exclusive checkable buttons using [QButtonGroup](https://doc.qt.io/qt-6/qbuttongroup.html) | Trigger edge Rising/Falling, A/B cursor selection; consider trigger mode if space allows | Present short option sets side by side. A segmented appearance needs styling; QButtonGroup manages exclusivity, not appearance. |
| [QToolButton](https://doc.qt.io/qt-6/qtoolbutton.html) with a popup menu | Export using the selected format, with alternate formats under an arrow | Keep export format and raw/displayed distinction visible. Use a split button for a primary action with alternatives; keep icon-only Run controls linked to ST-002. |
| [QToolBox](https://doc.qt.io/qt-6/qtoolbox.html) or disclosure buttons with project-owned sections | Acquisition, Trigger, Channels and Inspection groups | Reduce the long control column. QToolBox shows a selected page; independently expandable sections need their own composition. Keep essential live controls visible. |
| [QProgressBar](https://doc.qt.io/qt-6/qprogressbar.html) | Frozen-window export, bounded acquisition filling and finite-run progress where measurable | Export already exposes numeric progress in window_inspection_panel.cpp. Display actual completion; continuous simulation has no completion percentage. |
| [BusyIndicator](https://doc.qt.io/qt-6/qml-qtquick-controls-busyindicator.html), Qt Quick | Outstanding FFT/correlation work or node discovery | Indicate pending work only; distinguish filling, processing, frozen and error states. QWidget panels can use an indeterminate progress bar. |
| Search with [QCompleter](https://doc.qt.io/qt-6/qcompleter.html) | Element library and suitable element/port selectors | Complement existing element filtering with suggestions, a clear button and keyboard selection; retain access to the full tree. |
| [QLCDNumber](https://doc.qt.io/qt-6/qlcdnumber.html) | Optional compact instrument-style summary value | Evaluate appearance in a mockup. Keep units explicit and retain selectable full-precision readouts; ordinary numeric labels may serve measurements better. |

- **Existing structure to build on:** Studio already has dock panels, icon
  toolbars, a splitter and analysis tabs. This proposal concerns stronger
  control choices and consistent presentation, rather than treating those
  containers as missing features. Maintenance candidates include ST-002,
  grouped controls for existing trigger-edge/A-B selection, clearer existing
  export progress and compact advanced sections. Instrument controls and a
  pinned-range selector require scope review against existing behavior before
  inclusion; no additional measurement or acquisition capability is implied.
- **Integration findings:** the main shell and control panels use Qt Widgets;
  plots use QQuickWidget with Qt Graphs. LinePlot.qml currently imports
  QtQuick/QtGraphs, not QtQuick.Controls. QML controls require explicit module
  deployment validation: tools/windows/package.sh currently removes
  QtQuick/Controls from the package. Preserve the supported Qt baseline and
  verify APIs before choosing newer controls or properties.
- **Visual direction:** a coherent desktop instrument style with readable
  states, compact spacing, channel accents, precise numeric entry and clear
  focus indication. Qt Quick offers
  [Fusion, Material and other styles](https://doc.qt.io/qt-6/qtquickcontrols-styles.html);
  selecting one for QML does not automatically restyle QWidget panels. Review
  both together, including disabled contrast from ST-002.
- **Future acceptance check:** compare layouts at normal and high display
  scaling, in light/dark themes, with keyboard and mouse. Verify units,
  precision, disabled states, retained plot area and live responsiveness.
  Check relocated Linux/Windows packages if new QML modules are introduced.
  UI changes must preserve run lifecycle, causal execution, bounded live
  acquisition and raw measurement semantics.

### ST-005 — Main menu organization and labels are inconsistent

- **Settings requirement, deferred to 0.2.0:** add **Trusted Networks…** to
  Settings, exposing network trust configuration (ST-009). Do not add this
  entry in 0.1.1.
- **Specific Page menu requirement:** move the top-level Page menu under
  **Diagram → Page**, as clarified on 2026-10-24. Retain its existing paper
  sizes and Portrait/Landscape options and their active-diagram scope. This
  supersedes the briefly proposed Settings → Page placement.
- **Specific File menu requirement:** replace Load Demo Diagram with
  **Example Diagrams**, as requested on 2026-10-24. Provide a way to choose
  among bundled example diagrams rather than loading a single fixed demo.
  Decide submenu/chooser layout during planning, use installed example paths,
  and preserve unsaved-document Save/Discard/Cancel behavior.
- **Reported/requested:** 2026-10-24. The main menu is inconsistent; analyze
  and update it. Applies to the planning-before-implementation decision above.
- **Status:** implemented and locally verified. Final menus and evidence are
  recorded in [the release plan](RELEASE_0.1.1.md). Existing network behavior
  is unchanged; ST-009 remains in 0.2.0.
- **Resolution:** explicit menu order, Page under Diagram, zoom/fit under View,
  existing remote actions under Nodes, grouped library commands, real editing
  shortcuts with text/canvas focus routing, and consistent Keep Previous Scope
  Traces wording. Example Diagrams uses the standard file chooser rooted in
  the distribution's examples directory; the maintainer explicitly simplified
  the earlier custom/searchable chooser proposal. Save/Discard/Cancel, staged
  layout validation and working-copy save behavior are covered by GUI checks.
- **Classification:** usability/organization enhancement; provisional minor
  severity. Shortcut behavior found during review needs verification.
- **Analysis:** duplicated Fit Diagram placement; an almost empty Diagram menu
  alongside a separate Page menu; drawing actions missing from menus; local
  execution mixed with remote deployment; Scope trace-retention policy under
  View; inconsistent library labels, punctuation and shortcut registration.
- **Planning artifact:** [main menu review](STUDIO_MENU_REVIEW.md) records the
  current inventory, source evidence, proposed organization and acceptance
  checks. Proposed menu order: File, Edit, View, Diagram, Run, Nodes, Settings,
  Help; whether Nodes deserves a top-level menu remains a planning choice.
- **Future acceptance:** review the final menu map before implementation,
  preserve existing command behavior and scope, verify keyboard/focus and
  lifecycle states, and update user/offline documentation.

### ST-006 — Replace the Probes panel with Observers and live run previews

- **Scope after clarification:** embedded previews of existing observers are
  UX improvements, as explicitly clarified by the user. Include the Observers
  panel, preview selection and automatic run-start activation in maintenance
  planning alongside naming/discoverability/empty-state fixes. This supersedes
  the earlier classification that deferred them as new capabilities. Plan
  before implementation; no new measurement or acquisition capability is
  required by embedding the existing views.
- **Reported/requested:** 2026-10-24. The Probes tab shows nothing. Use it for
  a live preview to monitor runs (the user explicitly clarified "runs").
- **Terminology clarification:** the user expected a probe to be the
  connection from a signal wire to an observer, rather than a separate named
  object that must be created in addition to that connection. This exposes an
  inconsistency between the diagram workflow and the panel's narrower named-
  probe list; treat it as a design input, not a user setup error.
- **Updated direction:** following that clarification, the user proposed that
  a separate Probes panel is unnecessary and should instead be Observers.
  Adopt an Observers panel as the planning direction, superseding the earlier
  proposal to expand/rename a flat probe list. List observer instances (Scope,
  Live Oscilloscope, Phase Portrait, etc.), with their connected channels and
  compact live run previews. Selecting/opening an entry should reach that
  observer's full viewer. Probes remain read-only bindings behind those
  channels, not a separate user-facing panel that must be managed.
  Implementation remains deferred.
- **Observer selection requirement:** the user must be able to choose which
  observer instances appear in the panel. Multiple observers must not all
  expand into previews automatically and clutter it. Panel visibility is a
  presentation preference, separate from diagram membership and acquisition.
- **Run-focused layout requirement:** when a diagram starts running,
  automatically show and activate the Observers panel in place of the element
  picker or whichever competing tab is active. The user wants to watch the
  run rather than manually switch away from editing tools. Present the chosen
  observer previews; if none are chosen, show the observer chooser/empty state
  rather than a blank panel. Surface preparation failures/errors clearly.
  This layout change does not prevent deliberate access to other tools.
- **Classification:** reported empty-panel usability issue plus a requested
  monitoring enhancement. Provisional minor severity; no evidence of missing
  runtime samples or recording loss from this report.
- **Status:** source inspected; the reported session was not independently
  reproduced. Implementation deferred pending interface planning. Tested
  diagram/build, presence of named probes and run state are not yet recorded.
- **Source evidence:**
  - `ProbesPanel` in `s7studio/src/tool_panels.cpp` is a metadata list with
    Probe, Taps and Unit columns. It provides Scope/Phase Portrait opening and
    named-probe rename/delete actions. There is no live value or trend column
    and no explanatory empty-state content.
  - `MainWindow::refreshProbesPanel()` populates the list only from the root
    diagram scene's explicit `probes()`. In contrast, `currentRunProbes()` also
    includes connections to observer elements, including LiveOscilloscope.
    A diagram with only observer connections can therefore have acquisition
    channels while this panel has no rows. This explains one possible cause;
    whether the user's diagram matches it is unconfirmed.
  - `MainWindow::onScopeSamplesAvailable()` presents worker-owned recorded and
    live sources to viewers; it does not update ProbesPanel with sampled
    values. LocalSchemaRunner exposes immutable active run configuration and
    read-only source access for UI consumers.
- **Conceptual model for planning:** a signal/output is the source; a probe
  taps it read-only; an observation connection binds that source to an observer
  input/channel; an observer consumes the samples (Scope, Live Oscilloscope,
  Phase Portrait, or the proposed monitor panel). In the user's diagram model,
  the wire-to-observer branch supplies that probe binding without a separate
  probe-creation step. ADR-012 requires probes to remain outside functional
  computation; it does not require the current standalone named-probe UI.
  Current Studio supports both standalone named taps (`DiagramProbe`) and
  observer links, then converts both into `RunProbe` sampling subscriptions.
  Plan consistent user-facing terminology around the Observers panel. Decide
  how existing standalone named taps and their ad-hoc Scope/Phase actions map
  to the observer workflow before removing their UI or changing saved data.
  No format change is decided here. Monitoring should not require a duplicate
  named tap for an already connected observer channel.
- **Proposed monitoring behavior for planning:**
  - Offer a searchable observer chooser with per-observer selection. Show
    previews only for the chosen observers, with compact/collapsible channel
    details and a way to remove a preview from the panel. Selecting an observer
    here does not create another observer instance.
  - Remember the chosen set for the diagram/workspace; settle persistence and
    the initial selection policy during planning. Explain an empty selection
    and offer Choose Observers. Handle deleted observers and document changes
    without retaining stale entries or selecting unrelated instances.
  - Switch to monitoring on run start, not on every sample update: respect
    deliberate panel changes made by the user during a run. Keep previews and
    their status available while paused and after completion/stop, so the user
    can inspect the outcome. Decide how and when to restore the editing layout
    during planning; do not immediately hide final results.
  - Group monitored channels under their observer instance, with source
    element/output, name, unit, latest value and model step/time. Keep the run
    identity and state visible. A short bounded sparkline or selected-observer
    preview could make trends visible without opening a full viewer.
  - Distinguish Running, Paused, Stopped, Waiting for first sample, unavailable
    and invalid/nonfinite values. Retain the final value on completion with a
    clear state; reset/replacement must not show old values as current.
  - Explain an empty panel and offer a clear path to select/add monitored
    observers and connecting their inputs. Distinguish unconnected observers,
    shared source outputs, and configured next-run connections from the
    immutable active-run bindings.
  - Provide full-viewer access and observer/channel management appropriate to
    the new model. Coordinate the Observers name and placement with ST-005's
    menu plan; do not carry over named-probe management blindly.
- **Architectural constraints:** reuse worker-published read-only observation
  data; never read or step NodeRuntime directly from the GUI thread. Use a
  bounded preview with capped refresh and run-generation checks. Do not turn
  monitoring on into implicit full-run disk recording: existing named probes
  record, while live-only channels use bounded acquisition. Monitoring new
  signals and changing that acquisition contract need an explicit plan.
  Deselecting/hiding a panel preview must not delete the observer, disconnect
  its channels, stop its acquisition or change its full viewer. Suspend hidden
  preview rendering/analysis while preserving observation semantics.
  The initial proposal is for bundled local runs; remote monitoring requires
  its own network observation contract and is not implied by this panel.
- **Future acceptance check:** diagrams with named probes, observer-only
  channels and no monitored signals; correct values/source identities and
  coherent step/time; idle/run/pause/resume/stop/reset/rerun and document edits;
  hidden/reopened panels; bounded memory and responsive continuous operation.
  Verify nested-editor versus root-run scope explicitly. Display thinning must
  not alter acquisition or authoritative recordings. Include many-observer
  diagrams, selecting/deselecting previews during runs, collapsed previews,
  selection restoration, observer deletion and full-viewer independence.
  Verify run-start activation from the element picker and other tabs, paused
  and completed result visibility, start failure, explicit user tab changes
  during a run, and rerun/reset layout transitions.

### ST-007 — Clear element descriptions in Properties and the library

- **Requested:** 2026-10-24. Write short, understandable descriptions for
  elements and display them in the available space in Element Properties.
  Make descriptions accessible by right-clicking an element in the Element
  Library panel as well.
- **Expanded requirement:** the description area must be scrollable to
  accommodate long documentation supplied by other element libraries. For our
  existing elements, documentation must describe every supported JSON
  parameter. A short summary alone is insufficient; retain it as an introduction
  followed by the complete parameter reference.
- **Classification/status:** content and discovery UX improvement; implemented
  and locally verified on 2026-10-25. The maintainer selected complete metadata
  in JSON instead of long C++ documentation strings.
- **Resolution:** module-owned embedded JSON supplies runtime and observer
  metadata, including parameter defaults/bounds/choices, port and type rules.
  Shared plain-text documentation is available in resizable Information and
  split Properties dialogs. Library context actions target the clicked row;
  keyboard access and Edit information cover root/nested canvas selections,
  schemas and observers. Existing double-click gestures are preserved.
  Long text supports scrolling, keyboard navigation and copying. A selected
  nested-editor shutdown crash found by the workflow is fixed by disconnecting
  scene callbacks before teardown. The coverage inventory is in
  [STUDIO_ELEMENT_INFORMATION.md](STUDIO_ELEMENT_INFORMATION.md); test results
  and platform limits are in [the release plan](RELEASE_0.1.1.md).
- **Original source evidence (before implementation):**
  - `MainWindow::onElementNodeDoubleClicked()` already adds a word-wrapped
    description label near the top of the 720×680 properties dialog.
    `elementDescriptionForType()` uses registry metadata first, then a small
    hardcoded fallback list and finally a generic missing-description message.
  - `ElementMetadata::description` is exposed through ElementCatalogService.
    Built-ins, schema-backed libraries and plugins already provide varying
    descriptions. The existence of the field does not establish clarity or
    complete coverage; review the actual text against each element's behavior.
  - ElementPickerPanel stores catalog descriptions on tree items and shows
    them in a selection-based label capped at 80 pixels. Observer descriptions
    come from ViewerRegistry. No element-information context-menu handler is
    present in this panel.
  - Schema-backed elements currently open their nested diagram, and observers
    open viewers, instead of the ordinary parameter dialog. Include them in
    the information-access design without silently changing those gestures.
  - `ParameterSpec` already carries a parameter name, type, description, default,
    bounds, enum choices and required flag. ParameterEditor displays these
    descriptions in its scrollable Parameters form and tooltips, alongside an
    Advanced JSON tab. This does not establish complete coverage: audit against
    actual accepted JSON parameters, including type-variable pin parameters,
    optional/conditional parameters and schema/plugin definitions.
- **Original planning direction:** author/review a concise introduction plus a complete
  parameter reference for our existing built-in, schema-backed and observer
  types, and preserve plugin-provided documentation. Reuse existing element and
  parameter metadata across Properties, library selection and right-click
  information rather than maintaining a separate UI prose table. Reconcile
  descriptions with implementation and JSON examples; do not document only
  fields currently exposed in the routine parameter form.
- **Writing guideline:** one or two plain sentences explaining what the element
  does, what its inputs/outputs mean, and any essential unit, timing or state
  behavior. Avoid merely restating the type name, unexplained abbreviations or
  mixing the full parameter reference into the introductory summary. Examples
  of the intended style: Gain — "Multiplies the input signal by a configurable factor." Delay —
  "Outputs the input value from the previous simulation step."
- **Parameter reference requirement:** for every supported JSON parameter,
  give the exact JSON key, its meaning/effect, value type, default or required
  status, units where applicable, valid range/choices and relevant interactions
  or constraints. Explain array/object structure and conditional parameters;
  use a small JSON example where it clarifies accepted values. Distinguish
  parameters from signal inputs, particularly schema-backed controller gains.
- **Presentation:** use a scrollable description/documentation area in
  Properties with readable wrapping and selectable text. Long third-party
  documentation must remain fully accessible without expanding the dialog
  beyond the screen or squeezing out parameter controls and dialog buttons.
  Apply the same access to full documentation in library information; a capped
  selection summary must not be its only presentation. Right-click should offer
  Element Information (or an equivalently clear action) for the clicked item,
  without adding an element.
  Keep information accessible by keyboard and retain selection-based help.
  Avoid clipping longer plugin descriptions or treating them as executable
  rich text.
- **Future acceptance:** inventory description coverage and review semantic
  accuracy; check representative basic/stateful/integration, schema/plugin and
  observer entries. Verify consistent text in Properties and library info,
  right-click targeting with a different row selected, category/blank-space
  behavior, keyboard access, long text and display scaling. Verify scrolling
  with long third-party documentation, sensible layout at small window sizes,
  and complete coverage of the existing elements' supported JSON parameters,
  including defaults, units, structures and conditional/type-pin settings.

### ST-008 — Drag library elements onto the diagram

- **Requested:** 2026-10-24. Drag an element from the Element Library panel
  directly onto the drawing area to place it.
- **Classification/status:** element-placement UX improvement; source-reviewed,
  not independently reproduced. No implementation changes made.
- **Source evidence:** ElementPickerPanel currently emits elementRequested on
  double-click. MainWindow::onElementRequested() creates the chosen element in
  the active editor at suggestedNewItemPosition(). The library tree has no
  setDragEnabled/startDrag or custom outgoing MIME setup. Its panel-level
  dragEnterEvent accepts text/plain, but dropEvent is empty; these inbound
  handlers do not implement dragging library entries out to the canvas.
  QSchematic's view accepts drops, but its vendor scene expects its own node
  MIME representation. No Studio library-to-canvas drag route was found.
- **Planning direction:** dragging a library leaf should create one instance
  of that type in the active diagram at the drop position, using the existing
  creation/default/port logic and grid placement. Include schema-backed types,
  loaded plugin elements and Studio observers. Retain double-click placement.
  Group categories are not draggable elements; cancelling or dropping outside
  the canvas must not create anything or remove the library entry.
- **Implementation constraints for later:** integrate through project-owned
  picker/editor/scene adapters, without modifying vendor code. Use a defined
  type payload, validate it against the available registry/viewer types, map
  viewport to scene coordinates under zoom/pan, allocate a valid unique ID,
  preserve schema-backed definitions and make insertion one undoable action.
  Follow the existing active-editor and run-snapshot rules.
- **Future acceptance:** actual mouse drag from filtered/unfiltered library
  entries to root and nested diagrams; correct drop position at several zoom
  levels and after panning; one insertion with undo/redo and save/reopen.
  Cover representative built-in/schema/plugin/observer types, duplicate types,
  category rows, invalid payloads, cancellation and drops outside the canvas.

### ST-009 — Settings entry for Trusted Networks — deferred to 0.2.0

- **Current release assignment:** the maintainer assigned remote node control,
  restart and other networking work to 0.2.0. This includes this finding and
  the requested runtime node configuration editing. The earlier 0.1.1 file
  editor/settings proposal is superseded; use [the 0.2.0 plan](RELEASE_0.2.0.md)
  for configuration ownership, administration-contract planning and acceptance.
  The source evidence and original requirements below remain design inputs.
- **Requested:** 2026-10-24. Add **Settings → Trusted Networks…**, since the
  underlying functionality exists but lacks a settings interface.
- **Classification/status:** settings/discoverability UX improvement for
  existing network functionality. Source/documentation reviewed; no settings
  dialog implemented or configuration changed.
- **Source evidence:**
  - `docs/SPLIT_TRANSPORT.md` and runtime DataServerConfig define encrypted
    `mtls` data transport and explicit `trusted_plaintext` for loopback/trusted
    LANs, plus bind/advertised addresses. Local node policy is configured under
    `websocket.data_server`; deployments declare matching data endpoints and
    security profiles in `runtime_nodes`.
  - Studio's HeadlessNodesPanel already provides discovery seeds and CA/client
    certificate/key fields, groups certificate-verified nodes separately, and
    reads advertised data endpoints/security from directly verified nodes.
    Deployment support carries those advertised settings into the diagram.
  - The main Settings menu currently contains only the Scope temporary data
    directory command. No Trusted Networks settings interface or editable
    subnet/CIDR policy was found in the reviewed sources.
- **Planning requirement:** retain the requested menu name. Specify which
  existing settings belong in this dialog and their ownership (Studio client,
  diagram deployment or runtime node), where they persist, and whether changes
  apply on the next connection/deployment/restart. The user did not specify
  individual controls; do not infer a new subnet allowlist feature from the
  menu label. Distinguish certificate-verified node trust from choosing
  plaintext data transport on a trusted network.
- **Contract to preserve:** expose current policies without bypassing verified
  control authorization or treating a UI/JSON trust flag as authorization.
  Split-listener control remains mTLS; plaintext data stays an explicit policy
  with matching node/deployment configuration. Changing a Studio preference
  alone cannot silently reconfigure a remote node's listener. Configuration
  application paths and their existing capabilities need review before coding.
- **Future acceptance:** Settings entry reaches the dialog, existing values
  load and round-trip correctly, Cancel leaves them unchanged, invalid values
  produce clear errors, and effective configuration/application timing is
  visible. Test relevant discovery/deployment behavior under both existing
  data security profiles, without changing unrelated local simulation.

### ST-010 — Text-frame defaults and bundled example readability

- **Requested:** 2026-10-24. Use 10 pt by default in text frames and the toolbar
  font-size control, and update examples with small text frames.
- **Clarification:** 10 pt is not a minimum for user documents. Users must be
  able to choose 6 pt or smaller. Controls now allow 1–72 pt; loaded valid small
  sizes remain unchanged. Desktop toolbar UI fonts still follow Qt/KDE settings.
- **Status:** implemented and locally verified. New frames/default control use
  10 pt. All 62 bundled text frames are at least 10 pt; 60 already were 10 pt,
  and the two 9 pt frames were updated in the 2D phase and 3D helix examples.
  No example integration behavior was changed.
- **Checks:** default/loaded values, choosing 4 pt through the frame-font dialog,
  and a scan of every bundled diagram's text-frame sizes. Diagram text continues
  to scale with diagram zoom.

## Explicitly assigned to 0.2.0

- **Networking and remote node administration:** ST-009 Trusted Networks,
  Studio connection settings, runtime node configuration editing, remote
  control/restart and related networking work. This supersedes their earlier
  proposed inclusion in 0.1.1. See [the 0.2.0 plan](RELEASE_0.2.0.md).

- **Certificate viewer and node certificate creation program:** requested
  2026-10-24 as part of the networking release. Track scope, provisioning and
  acceptance in [the 0.2.0 goals](RELEASE_0.2.0.md). These are additional tools,
  coordinated with Trusted Networks settings and node administration
  (ST-009), which are also assigned to 0.2.0.
- **Printing diagrams and observation graphs:** requested 2026-10-24 as a
  0.2.0 release goal. Studio currently has no print workflow and printing has
  not been tested. Scope, planning questions and acceptance requirements are
  tracked in [the 0.2.0 goals](RELEASE_0.2.0.md). Excluded from the tentative
  0.1.1 scope.

## Recording conventions

Assign each finding a stable ID (`ST-001`, `ST-002`, …). Record:

- User-reported behavior and expected behavior.
- Reproduction steps, example/document, platform and tested build, where known.
- Impact and provisional severity.
- Evidence: user report, source inspection and reproduction recorded separately.
- Analysis: relevant code paths, likely cause, uncertainty and related findings.
- Proposed acceptance check for a future fix.

Keep missing details explicit. Source inspection alone does not establish that
a symptom was reproduced or that a suspected cause is confirmed. Add findings
as they arrive; do not treat historical backlog or preview limitations as new
manual-test reports.

## Reference

- [0.1.0 release scope and known limits](RELEASE_0.1.0.md)
- [Existing backlog and open design decisions](OPEN_PROBLEMS.md)
- [Studio user guide](STUDIO_USER_GUIDE.md)
- [Automated GUI coverage](STUDIO_GUI_TESTING.md)
