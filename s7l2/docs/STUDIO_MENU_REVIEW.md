# Studio main menu review

Date: 2026-10-24. Related finding: ST-005 in
[manual-test triage](STUDIO_0.1.1_TRIAGE.md).

Status: ST-005 implemented and locally verified; this document retains the
0.1.0 inventory and original review. The final map and evidence are in the
[0.1.1 release plan](RELEASE_0.1.1.md). Example Diagrams uses the standard file
chooser in the distribution's examples directory, per the maintainer's later
clarification. Trusted Networks remains deferred to 0.2.0.
The subsequent [0.1.1 interface plan](RELEASE_0.1.1.md) selects a concrete menu
map and example chooser for review, with action scope and acceptance checks.
Its proposed map supersedes the alternatives below when reviewing that design;
this document retains the original source inventory and analysis.
Maintenance scope is improvement of existing features only. Reorganizing
existing commands and embedding existing observer views in the Observers panel
are UX improvements within maintenance planning. New S7 capabilities remain
outside this scope.
The initial report was general; subsequent specific menu requests are recorded
below. Source findings come from inspection, not a fresh GUI run.

The 2026-10-25 refinement promotes Tool Windows to a top-level **Tools** menu,
after Nodes and before Settings, preserving its actions and shortcuts. View
retains Hide All Tool Windows and Appearance. Diagram now also contains
Diagram Properties for root-document name/description. The original proposal
below predates these follow-up requirements.

## 0.1.0 structure before implementation

Source: `s7studio/src/main_window_builder.cpp`, `MainWindow::buildActions()`
and `MainWindow::buildUi()` in `s7studio/src/main_window.cpp`, and
`s7studio/src/tool_windows.cpp`. With the editor created, the order is:

| Menu | Current contents |
|---|---|
| File | Open, Recent Diagrams, Close Diagram; Save, Save As, Load Element Library, Save Schema Library; Load Demo Diagram; Exit |
| Edit | Undo, Redo; Cut, Copy, Paste, Select All; Element Properties |
| Diagram | Fit Diagram to Window |
| Run | Run, Run Configuration, Pause, Resume, Stop; Reset; Deploy to Nodes, Open Deployed Diagram |
| View | Zoom In/Out, Actual Size; Fit Diagram to Window, Fit Page; Overlay Scope Traces; Tool Windows, Hide All Tool Windows, Appearance |
| Page | A5, A4, A3, Letter; Portrait, Landscape |
| Settings | Scope temporary data directory |
| Help | S7 Studio Help; About S7 Studio, About Qt |

Appearance contains tool window bar visibility and File/Run/Diagram Drawing
toolbar visibility. Tool Windows lists the existing dock panels.

Subsequent planning direction (ST-006): replace the Probes panel with
**Observers**, listing observer instances, connected channels and live previews.
The future Tool Windows menu and related labels should follow that change;
the current-structure inventory above describes the existing implementation.
The requested run-focused workflow should automatically activate
Observers over the element picker or another competing tab, while retaining
manual tool-window navigation. The user clarified that the embedded previews,
observer selection and layout behavior are UX improvements for maintenance
planning; implementation still follows design/planning.

## Findings

1. **Duplicated ownership:** Diagram contains only the same Fit Diagram action
   that is also in View. Zoom/fit commands have a clear common home in View.
2. **Uneven grouping:** Page has a top-level menu for diagram sheet settings,
   while Wire Draw, Text Frame and text-frame formatting are available in the
   drawing toolbar but absent from the main menu. Group drawing commands in
   Diagram, including Page options as requested by the user.
3. **Local/remote mixing:** Run combines bundled local simulation with remote
   deployment and loading a deployed diagram. Distinct grouping should make
   the network boundary clear.
4. **Recording policy placed with viewing:** Overlay Scope Traces controls
   whether preceding Scope traces are retained when a new run starts. Its
   inverse is exposed as Clear scope traces before run in Run Configuration.
   Location and naming should express the same policy consistently.
5. **Terminology and punctuation:** Load Element Library versus Save Schema
   Library makes related library actions sound unrelated; confirm their exact
   operations before renaming. Three dots and the ellipsis character are mixed.
   Dialog-opening actions such as Text Color lack an ellipsis. Settings uses
   sentence case where most command labels use title case.
6. **Shortcut display differs from registration:** Cut/Copy/Paste/Select All
   and Element Properties put tab-separated key names directly in action text.
   Their action-building helper does not call setShortcut(). The diagram view
   separately handles these keys. Undo/Redo and other actions instead register
   shortcuts. Menu presentation alone therefore does not establish uniform
   dispatch, especially with text inputs focused.
7. **Fragmented construction:** the builder creates File/Diagram/Run, then
   MainWindow appends other menus and inserts Edit by position. This spreads
   ordering and ownership across construction stages. A future implementation
   should define menu order explicitly and reuse shared actions.
8. **Document workflow needs a deliberate decision:** there is no New command
   in the inspected main menu. Determine whether creating a blank document is
   required; this review does not establish an existing New operation or
   authorize implementing one as part of menu cleanup.

## Proposed organization for planning

This is a concrete starting point, not an approved final menu design.
The user specifically requested replacing **Load Demo Diagram** in File with
**Example Diagrams**, giving access to the bundled examples rather than one
fixed demo. The current-structure inventory above retains the existing label
for accuracy. Choose a submenu versus an example chooser during layout
planning; preserve document-replacement protection when opening an example.
The user settled on moving the top-level **Page** menu into **Diagram**.
Use **Diagram → Page** for the existing paper sizes and orientation options;
this supersedes the briefly proposed Settings placement.
These remain settings of the active diagram, not global application defaults.
The requested **Settings → Trusted Networks…** entry and ST-009 are deferred
to 0.2.0 under the maintainer's subsequent networking scope correction.
0.1.1 menu cleanup may regroup existing network actions, but adds no network
settings or administration workflow.

| Menu | Proposed responsibility and existing actions |
|---|---|
| File | Open Diagram, Recent Diagrams, Close Diagram; Save, Save As; library loading/saving in a clearly named group or submenu; Example Diagrams; Exit |
| Edit | Undo/Redo; Cut/Copy/Paste, Select All; Element Properties. Evaluate adding the existing diagram deletion operation. |
| View | Zoom In/Out, Actual Size, Fit Diagram to Window, Fit Page; Tool Windows, Hide All Tool Windows; Toolbars and tool window bars |
| Diagram | Drawing mode, Text Frame insertion, Text Frame formatting; Page submenu for paper size and orientation |
| Run | Run Configuration; Run/Pause/Resume/Stop; Reset; consistently named policy for keeping previous Scope traces |
| Nodes | Deploy to Nodes, Open Deployed Diagram; a link to the existing Headless Nodes panel if useful |
| Settings | Scope Temporary Data Directory…; Trusted Networks is deferred to 0.2.0 |
| Help | S7 Studio Help; About S7 Studio, About Qt |

An alternative to a new Nodes top-level menu is a clearly separated Nodes
submenu under Run. Resolve this during planning based on the local simulation
workflow and expected remote-node usage. Do not introduce nonfunctional entries
for unimplemented features.

Use one label for each operation across menus, tooltips and dialogs. Keep full
menu labels even when toolbar buttons become icon-only (ST-002). Make action
scope explicit: page settings and document save, active nested diagram edits,
local run controls, viewer presentation and remote-node operations are distinct.
Do not change an operation's target merely by moving its menu entry.

## Checks for a future implementation

- Verify every existing command remains reachable, with no accidental duplicate
  placement, and check the menu against the toolbar and contextual actions.
- Define action shortcuts separately from displayed labels. Verify shortcuts
  and menu commands with the diagram, nested schema, search box and editable
  text focused; text editing must retain its expected scope.
- Check unique top-level mnemonics and suitable labels/ellipsis, menu icons,
  check states, disabled contrast and shortcut rendering on Linux and Windows.
- Verify diagram selection and drawing-mode availability, active page size and
  orientation, and action enablement in idle/running/paused/stopped states.
- Preserve save-failure/cancellation protection, run lifecycle and remote
  control authorization. Moving actions must not bypass their existing guards.
- Update the user guide/offline help and relevant GUI workflows after the menu
  design is settled. Include actual menu activation in verification; action
  callbacks alone do not prove discoverability or keyboard dispatch.
