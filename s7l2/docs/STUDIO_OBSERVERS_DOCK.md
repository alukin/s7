# Observers dock

**Observers** replaces the Probes dock in the right tool-window area. Open it
through **View → Tool Windows → Observers** or **Alt+3**. It always describes the
root diagram and its local run, including when a nested schema tab is active.

## Choose previews

Choose **Choose Observers…**, filter the root observer list and check the desired
instances. **Apply** changes the selection; **Cancel** leaves it untouched.
Initially no observers are selected. Selection does not create an observer,
alter its connections, start recording or open full viewer windows.

Each selected card has a collapse arrow, **Open** and **Remove**. Collapse keeps
the selection; Remove removes only the preview. **Open** raises the observer's
independent full viewer. **Freeze preview** holds that card's displayed data;
it is independent of full-viewer Freeze and simulation Pause. Navigating a 2D
preview freezes it. Uncheck Freeze preview to follow the current data again.

Scope cards show recent time traces, Phase Portrait cards show same-step XY,
and Phase Trajectory 3D cards show a bounded 3D trajectory. Live Oscilloscope
cards show the Time presentation and rolling/trigger status. Detailed analysis,
recording/comparisons, cursors, export and acquisition controls stay in the full
viewers. Scope and Live cards offer **Plotted channels…** for up to eight display
channels. This never limits capture. Expand **Channels / latest sampled values**
to see source outputs, supplied units and each input's own latest sample step
and model time. A set of latest values is not presented as a coherent frame.

An unconnected observer remains visible with a connection message. Units that
are not declared are labelled **unit unspecified**. Status reports gaps, invalid
or unmatched values, capture loss, trigger state, completion and source errors.
Phase previews show the latest continuous segment rather than joining across
missing or nonfinite samples. Scope Time traces likewise break at gaps.

## Run lifecycle

After run preflight succeeds, Studio activates Observers once and preserves
editor keyboard focus. Further sample/status updates do not switch back to it
after you select a different dock. Acknowledged Pause/Resume update its status;
Stop, completion and errors keep final results. Reset clears the preview data
and cancels pending preparation. Replacing or closing a document retires its run
and releases its preview surfaces.

In 0.1.1, observation bindings belong to the immutable run configuration. Adding
or rewiring an observer during a run applies to the next run. Existing cards keep
their active source and label the document difference. Deleting an active
observer keeps its current card until that run is retired; undo restores its
document selection. Saved trigger presets are applied before production even
if no full window is open. Opening a card or full window does not rearm a trigger
or reconfigure retention. Explicit full-viewer trigger edits use the shared
observer settings owner.

The maintainer requested live observer rebinding as a follow-up. The fixed-run
limit is accepted for 0.1.1; no later release has been assigned to the rebinding
work. Its causal handover and continuity requirements are in OPEN_PROBLEMS.md.

## Recorded Signals

**Recorded Signals…** opens the existing named-tap list with source/units,
rename/delete and Open Scope/Open Phase Portrait actions. Wire tap creation,
recording and saved probe annotations retain their existing behavior. Named
taps do not become extra diagram observer elements.

## Persistence and bounded work

Selection order, collapse state and optional plotted-channel indices are saved
in `diagram.observers_panel`. They are undoable document edits and participate
in Save/Save As and replacement protection. Each entry has `element_id`, `type`,
`collapsed`, and optionally `plotted_channels` (at most eight distinct nonnegative
indices). Reusing an element ID with another type does not select it. Deleting
an observer prunes its selection in the same undo command; undo restores it.
Clipboard and schema-library export do not carry the root panel selection.
The Studio deployment projection removes this presentation field, leaving the
deployed graph/configuration unchanged. Dock geometry remains in QSettings.

The panel refreshes at most 10 times per second. Only expanded cards intersecting
its visible scroll viewport can prepare/render data, with at most four active
plots. Further visible cards show a resource-limit placeholder. Hidden, collapsed,
offscreen and minimized previews stop submitting work and release their plot
surfaces. Capture continues. Frozen cards may retain their last reduced geometry.

Each preview prepares at most 4,096 recent positions and draws at most 1,024
vertices per selected channel. Live reads remain bounded by the existing live
acquisition limits. Recording sources maintain an independent bounded tail of
4,096 records per channel, including sparse channels, in RAM. This adds bounded
per-channel presentation cache storage, not a second journal or a new recording.
Tail reads do no disk I/O and do not scan a long recording. Full-resolution
recordings and exports remain unchanged. Preparation runs on the existing
analysis pool, with one active and one replaceable pending request per card.
Run, binding and settings revisions reject obsolete completions.
