# Studio instrument controls

The 0.1.1 controls increment presents existing observer settings with knobs,
sliders, explicit choices and progress. It precedes the observer presentation
boundary and Observers dock work. It adds no acquisition or measurement model.

## Visual references

Reviewed on 2026-10-25, using the projects' own documentation and screenshots:

| Reference | Relevant pattern | Application to Studio |
|---|---|---|
| [Rigol DHO1000](https://www.rigol.com/intl/products/oscilloscope/DHO1000.html), [four-channel DHO1204 front panel](https://www.rigol.com/dam/jcr:b91aa3a2-12d8-40a7-b4a8-f55370aa29fa/DHO1000-front.png) | Plot dominates the front panel; channel selection, vertical, horizontal and trigger controls form distinct groups. | Keep plots large and channel identity visible; separate retention, display adjustment and trigger settings. |
| [Tektronix 4 Series B quick start](https://www.tek.com/en/manual/oscilloscope/4-series-b-mso-quick-start-4-series-mso) | Separate acquisition, vertical, horizontal and trigger control groups. | Group related controls and label their scope. |
| [Scopy oscilloscope](https://analogdevicesinc.github.io/scopy/plugins/m2k/oscilloscope.html), [annotated screenshot](https://raw.githubusercontent.com/analogdevicesinc/scopy/doc_resources/resources/m2k/oscilloscope/osc-main.png) | Selected-channel settings in a hideable side panel, knobs alongside numeric fields, channel accents. The pictured M2k instrument has two channels. | Show one selected channel's gain/offset controls at a time, preserving independent settings for all eight S7 channels. |
| [OpenHantek6022](https://github.com/OpenHantek/OpenHantek6022), [Qt desktop screenshot](https://raw.githubusercontent.com/OpenHantek/OpenHantek6022/main/docs/images/screenshot_mainwindow.png) | Compact labelled channel, horizontal and trigger groups beside the waveform. | Use ordinary desktop widgets and retain exact input. |
| [ngscopeclient main window](https://www.ngscopeclient.org/manual/MainWindow.html), [four-waveform tutorial screenshot](https://www.ngscopeclient.org/manual/ng-images/tutorials/basics/first-waveform.png) | Large waveform workspace with configurable controls. Its [tutorial](https://www.ngscopeclient.org/manual/Tutorials.html) separates viewport navigation from timebase settings. | Preserve plot space and distinguish display navigation from retained samples. |
| [PicoScope 7 scope view](https://www.picotech.com/library/knowledge-bases/oscilloscopes/scope-view) | Controls arranged around a dominant waveform view, with distinct channel and scope controls. | Keep frequent actions accessible while detailed settings can collapse. |

These are design references, not bundled assets or dependencies. Studio keeps
the supplied signal units, existing channel colors and Qt palette. Freeze
Display affects this viewer; simulation Pause remains a separate run action.
Hardware voltage ranges, sample-rate controls and Run/Stop semantics are not
implied for an S7 observer.

## Live controls

The channel buttons select a control page, with independent gain, position and
visibility for all eight channels. Colored checkboxes beside the buttons and in
the plot legend share the same visibility state. Full names and supplied units
appear above the selected controls and in button tooltips.

The larger vertical dial now controls units/division directly. Both scale and
timebase dials default to a 1–2–5 ladder, with reachable numeric bounds even
when those bounds are not ladder values. Fine buttons independently select 100
logarithmic steps per decade. Changing mode never changes the exact value.
Clockwise, Right/Up and a positive wheel notch reduce scale (zoom in); Left/Down
zoom out. One wheel detent applies one step regardless of desktop scroll-line
settings; partial angle deltas accumulate. Page Up/Down apply three steps.
Numeric spin arrows increase/decrease the number using the same adjustment
mode. Exact off-ladder values move to the next stop in the chosen direction.
Mouse dragging selects the dial's coarse or fine position.

The exact Gain × field retains the 1e-6 to 1e6 display multiplier range. Scale
bounds follow the current Y span and that range. Position has ±1e12 exact entry
and a wrapping relative dial. Scale, position and timebase knobs all use the
same 72×72 logical-pixel size. A full turn changes the offset by half
the visible Y span. Zero and double-click reset only that channel's position;
there is no half-span control or movable interval. Numeric edits never pass
through the dial. Editing scale, gain or position disables Auto Y, preserving
the shared scale and other channels' positions. Auto Y can be enabled again
explicitly. Fine/Zero sit beside the vertical knob headings to preserve room
for the horizontal controls.

Project-owned painted QDial subclasses retain Qt keyboard, wheel, mouse and
accessibility behavior. Channel accents, dark graticule and compact readouts
form a consistent instrument surface. Numeric faces show five significant
digits while inactive; focus and tooltips expose the complete round-tripping
value. The compact face never changes the stored value. Scale and position sit
side by side with their numeric fields below the knobs; exact gain is below them.

Vertical and Horizontal start expanded; Analysis retention, Trigger and Window
inspection start collapsed. All sections collapse independently. The control column scrolls
vertically and can be hidden. Time input fields use slimmer vertical padding.
The Time hover readout defaults to approximately three lines below an adjustable
vertical divider; drag the divider to enlarge it or down to hide it and give the
controls more room. The settings remain scrollable when sections expand.
Wheel events follow the pointer across the full column width, including blank
areas and disabled controls; events at a scroll limit stay inside the controls.
Enabled knobs and editors retain their own wheel adjustments.
Status keeps a compact two-line scrollable area at the bottom. Analysis tabs
retain their eight-line hover areas. Hover values and times show five significant
digits; longer messages remain scrollable. Worker progress reserves its space
while hidden.
Pointer movement therefore cannot resize the plot or shift the settings panel.
The plot header and dock preview status explicitly identify the retained model
time interval, distinguishing it from the full run duration.

Spectrum, Constellation / XY and Correlation use the same right column as Time.
Their settings scroll vertically in collapsible groups; no settings sit above
the graph. All plots use the same dark graticule, brighter traces, five-digit
hover readouts and compact bottom status. Analysis retention and Window inspection
move with the active tab while retaining their settings. Hide controls and
Resume / Fit apply to the active plot. Technical analysis status remains in
its tooltip and pinned-window metadata.

The Time-base knob and adjacent exact `s/div` editor use model seconds/division
and a ten-division grid. Clockwise reduces seconds/division. Mouse navigation
updates both the numeric value and dial, including its compact readout. Time
defaults to 2 s/div independently of model dt, for rolling and triggered views;
reruns preserve the selected time scale rather than deriving it from retention. A separate 4096-position
history selects original endpoints and channel extrema in bounded causal buckets;
changing this display span does not change full-rate analysis samples or the
simulation. Existing retained history is merged when widening. Missing history
fills forward. Zoomed intervals use retained original raw samples when available,
while preserving the wider overview separately. Expired raw detail is reported
rather than reconstructed. Sampled triggers keep their full-rate capture and can
be zoomed with the same knob; partial display history is labelled. Pinned Time-envelope statistics and exports explicitly
identify the selection instead of claiming full-rate measurements.

Pre-trigger slider and percentage field share the same integer value. Rising
and Falling are exclusive labelled buttons. Trigger mode, raw input, level,
hysteresis, holdoff and timeout retain their existing semantics and validation.
Saved trigger presets still apply without emitting a user edit. Other new
display controls remain session-local.

## Inspection and progress

Pinned A and B cursors each have a slider and exact sample index. They retain
their identities when crossing. The click-target buttons select A or B;
Alt+Left/Right moves that cursor one position. Measurements and exports continue
to use the immutable pinned input and existing inclusive interval semantics.

The Export button shows the selected Window JSON, raw acquisition CSV or
displayed data CSV format. Its arrow opens the format menu. Export progress
comes from the existing worker; Cancel requests cancellation. Success shows
100%; failure/cancellation clears the progress indicator and reports the result.

Finite run progress shows completed/total steps and model time. Continuous runs
show step count and model time without a completion percentage. Time, Spectrum
and paired analysis show indeterminate progress only while their worker has
pending work; status text distinguishes waiting, processing and frozen results.

All widgets are project-owned Qt Widgets compositions. Numerical analysis,
runtime scheduling and the headless build remain independent. No Qt Quick
Controls module or external UI code is added.

## Next session: Rigol-inspired live oscilloscope UI

Recorded on 2026-10-25 at the maintainer's request. The five time/preview
findings and the first item-6 increment are implemented below. The remaining
control-design proposals are still a backlog.
The maintainer originally asked
to finish and commit the model-time scale and automatic-preview work before
starting another live oscilloscope UI pass.

**Engineering-tool priority (maintainer, 2026-10-25):** Live Oscilloscope is
the primary engineering tool; Scope is the next priority. Both must receive
thorough correctness and usability refinement. Treat reliable traces, accurate
time/value presentation and predictable controls as essential acceptance
criteria. Apply the interface-quality review to Scope as well as Live
Oscilloscope; a polished live viewer alone does not complete this work.

The maintainer's instrument is a **Rigol DS1054Z**. Its
[DS1000Z user guide](https://www.rigol.com/dam/global/downloads/brochures/en/user-manual/oscillosopes/DS1000Z_UserGuide_EN.pdf)
documents shared selected-channel controls, separate scale and position,
coarse/fine adjustment, and delayed sweep. The hardware photo and downloaded
manuals are in the local-only `pic/` directory; keep that directory out of Git.
The accompanying UNI-T UTG932E inspires the separate
[0.2.0 software signal-generator plan](RELEASE_0.2.0.md#software-signal-generator-element).

### Proposed priorities

1. **Compact Vertical, Horizontal and Trigger groups.** Keep common adjustments
   visible and put acquisition details in expandable settings. Preserve the
   dominant graph area and consistent layouts across Time, Spectrum, XY and
   Correlation. These group names primarily describe the Time view.
2. **One shared pair of channel knobs.** Build on the existing selected-channel
   controls: select through the colored legend/channel control and adjust that
   channel's scale and position. Keep visibility and selection distinguishable,
   and preserve independent settings for every channel. Reuse channel colors in
   the controls, trace, scale readout and zero marker.
3. **Vertical scale in units/division.** Prefer a meaningful graticule readout
   such as `200 mV/div` or `0.5 units/div` for a signal without declared units.
   Define how independent channel scales map onto the shared plot and how the
   selected channel's axis is presented, including channels with different units.
   Display transformations must preserve original values for hover, cursors,
   measurements, exports and trigger thresholds.
4. **Familiar knob behavior.** Offer 1–2–5 scale steps with optional fine
   adjustment and exact numeric entry. Clockwise should zoom in, reducing
   seconds/division or signal units/division. Review the current time knob's
   direction. Use larger scale knobs, smaller position knobs, and a clear
   reset-to-zero action for position, proposed as double-click. Retain keyboard,
   wheel and accessibility support and five-significant-digit inactive readouts.
5. **Draggable graph markers.** Add colored channel zero-level arrows, a
   trigger-level marker and a trigger-time marker. Make their relationship to
   display position and raw trigger values explicit. Changing one channel's
   position must leave the other channels fixed.
6. **Stable instrument readouts.** Show time/division, channel scales, trigger
   state and live/frozen state in compact fixed positions. Keep hover values to
   five significant digits, status near the bottom, and detailed diagnostics
   expandable. Pointer movement must not resize the plot or move other text.
   Keep viewer freeze distinct from simulation pause/stop.
7. **Optional overview and zoom region.** Consider an overview of retained
   history with a movable highlighted interval for the main plot. Preserve
   follow-latest and explicit manual navigation. Show only history actually
   retained; a 600-second run does not imply 600 seconds remain available.

Start with grouping, selected-channel controls and units/division. Treat the
overview as a later increment. Review the current implementation before replacing
controls: selected-channel gain/position, colored visibility checkboxes, a
model-time seconds/division knob and collapsible sections already exist.

### Item 6, increment 6a — grouping, selection and scale readouts

Implemented 2026-10-25. Time controls now use Vertical and Horizontal groups,
with up to eight compact channel selectors in two rows. A legend name selects
that channel's existing gain/position controls; its checkbox still independently
toggles visibility. Enter selects a focused legend row and Space toggles its
visibility. The selected label is bold and uses the trace color. Long live
legend names elide within a bounded width; full names remain in channel controls
and accessibility text.

The full Time plot uses a vertical grid spacing of one eighth of the visible
height, anchored at display zero. Each channel's readout
shows original signal units per division, computed from the shared visible Y
span divided by eight and that channel's display gain. Position does not change
this scale. Readouts refresh after Auto Y or manual Y-range/gain changes and keep
supplied units independently (V, A, etc.); unspecified units use `units/div`.
The shared numeric axis is explicitly a display level, since independently
scaled mixed-unit traces have no common physical unit. Hover values, trigger
thresholds, measurements and exports continue using original samples.

This increment retained the existing gain/position controls and added a readout.
Increment 6b below extends it with direct entry and selected-channel axes.

Validation: Studio rebuild, Live module and Qt Quick line-plot checks pass.
Seven focused GUI cases pass; five affected cases pass again after final tick
label/legend layout review. Local helpers and captures remain outside Git under
`build-release/rigol-6a-*`. Details are in the release plan.

### Item 6, increment 6b — direct scale entry and selected-channel axes

Implemented 2026-10-25. The Vertical **Scale** editor accepts units/division
directly. It adjusts only that channel's gain, turns off Auto Y, and holds the
plot range and other channels fixed. Positive finite values within the gain
control's representable range are accepted, including scientific notation.
The full entered value survives channel selection and position edits; an
independent gain or Y-span change recalculates it. Live range changes preserve
partially typed text, and committing applies it against the current range.

The Y axis now identifies the selected channel and its declared units, using
the trace color. Grid labels invert that channel's gain and position to show
original signal values. Selecting a different channel, including one with
different or unspecified units, changes labels without moving any trace.
The graticule remains evenly spaced in display coordinates. Hover, trigger,
measurement and export values still use original samples. Generic line plots
retain their existing axes.

Item 6 remains in progress. Resume with 6c: coarse/fine 1–2–5 stepping and
position reset, followed by channel/trigger position markers. The optional
history overview remains later work. Gain/position knobs remain available
alongside the direct Scale editor during these increments.

Validation: five focused GUI cases cover exact entry, invalid values and range
endpoints, focused edits during range changes, mixed/unspecified units,
selection, position, raw-data preservation, light/dark layouts and long runs.
The Live Oscilloscope, generic Qt Quick line-plot and bundled-demo regressions
pass. Local helper snapshots and reviewed captures remain outside Git under
`build-release/rigol-6b-*`.

### Item 6, increment 6c — coarse/fine scale and position reset

Implemented 2026-10-26. The scale knob operates on the existing units/division
editor, keeping its exact-entry and selected-channel axis contracts. Separate
Fine buttons for each channel and the timebase change only the adjustment mode.
Coarse 1–2–5 steps, fine logarithmic adjustments, keyboard, wheel and mouse
behavior are described above. The gain field remains available for exact edits.
The position knob is smaller; its adjacent Zero button and double-click reset
operate on that channel alone. These controls remain session-local.

Next: channel-zero and trigger-level/time markers. The optional history overview
and broader Scope refinement remain later work.

Validation: Release `build-tests` and 83/83 CTest entries pass (80.09 s) with
recording scratch files on the project disk. After the final compact-heading
adjustment, the Live module and seven focused rendered cases pass; four cases
pass at each of 150% and 200%. Raw samples, independent channels and exact
values are preserved. Captures/helpers are outside Git under
`build-release/rigol-6c-*`; ST-016 records the light-palette label contrast found
during review. See the release plan for the environment retry and limits.

### Item 6, increment 6d — channel zero and trigger plot handles

Implemented 2026-10-26. Visible channels have colored zero badges in a reserved
left margin. Dragging selects and moves only that channel; double-click/Home
resets its position. Coincident badges fan out without changing their true zero:
leaders and arrow tips stay at the transformed raw zero. Hidden traces have no
badge. Off-screen positions are indicated at an edge and can be brought back
by dragging. Channel moves are immediate and do not freeze acquisition.

The right-hand orange L handle edits the raw threshold through the trigger
input's own gain/position, even when another channel is selected. The upper P
handle edits pre-trigger percentage. Trigger handles preview during drag and
commit once on release through the existing settings owner, with its existing
validation, saved-setting feedback, clear/freeze and rearm behavior. Escape
cancels pending trigger edits; changed source/preset revisions reject stale
gestures. Owner rejection is visible in compact status. A marker edit does not
commit drafts in other fields. Numeric controls remain available.

P is a next-acquisition coverage guide; the captured T reference remains tied
to the actual sampled event. P does not move the captured timeline when zooming
or panning. Keyboard and accessibility actions edit markers; graph-navigation
shortcuts yield while a marker has focus. Hover help shows channel identity and
raw trigger units. Marker gestures do not pan/zoom or place inspection cursors.
Ordinary plots and dock previews keep their existing marker-free layout.
Narrow Time plots number every second/fifth division as needed; subticks keep
the ten-division graticule and numeric precision is unchanged.

The planned channel/trigger markers are implemented. Broader Scope refinement
and the optional retained-history overview remain later work.

Validation: final native Release suite passes 83/83 at two jobs (117.60 s).
Three rendered marker cases pass at 100%, 150% and 200%, including direct tip
hits, owner/capture lifecycle and grid division checks, without QML warnings.
The full workflow covers XY hover shutdown after disconnecting all observation
views before Time-plot deletion. Final live-load UI gap is 87 ms; the release
plan also records an eight-job 370 ms outlier and an isolated 133 ms pass.
Reviewed captures and local-only helpers are under `build-release/rigol-6d-*`.

### Reported time/preview defects — implemented refinement, 2026-10-25

The maintainer authorized items 1–5 together, subsequently identifying
`2int_osc_live` as the reported example. The exact original old/new dt pair was
not supplied. The original reports and acceptance requirements follow below;
the implementation resolves the confirmed paths and verifies the example at
0.01, 0.001 and 0.0001 s.

- Source attachment no longer resets a mouse-selected time scale. Triggered
  displays no longer derive the viewport width from sample count times dt;
  full viewers and previews keep explicit model-time spans. Missing portions
  of a requested interval are labelled as partial display history.
- A magnified sine previously used just six line intervals despite 50 raw
  samples still being retained. Time preparation now uses those originals
  whenever they cover the visible data, and keeps the wider display history
  available for zooming out. Genuine gaps/nonfinite data remain explicit;
  expired fine detail is reported instead of invented.
- The existing numeric editor is now beside the Time knob with `s/div` units.
  Silent range updates refresh dial and compact readout together. Clockwise
  zooms in; exact entry is not quantized through the dial. Invalid signed or
  out-of-range input retains the previous committed value.
- Live preview wheel zoom and drag pan preserve Y; phase plots retain two-axis
  navigation. Full Live Time navigation follows the same horizontal rule.
- Recorded Signals is explicitly named and documents its named-tap scope.
  Empty diagrams get instructions, and configured/captured named taps remain
  visible through run completion and reopening.

Verification: 10 affected module entries and all four GUI suites pass, with 66
workflow cases and no skips. Local rendered checks show a smooth zoomed sine
and a fully visible numeric field beside the knob. The complete recorded
increment and limits are in [the release plan](RELEASE_0.1.1.md).

The broader proposed Rigol control redesign above remains a separate backlog.
The following numbered observations are retained as the original requirements.

1. **Visible time span changes unexpectedly after changing model `dt`.** On
   initially running a diagram, the sine trace shows about three periods; after
   changing `dt`, only about one period is visible. Reproduce the initial run
   and the run after the change, and inspect time-axis mapping, initial scale,
   retained history and acquisition/display sampling. For an unchanged signal
   frequency and seconds/division, the same model-time span must show the same
   number of periods once sufficient history is available. Make initial and
   rerun behavior consistent and any genuinely unavailable history explicit.
2. **Time-scale adjustment breaks the sine trace into line fragments.** Turning
   the time-scaling knob produces broken lines instead of the expected sine
   waveform. Reproduce while running and with frozen/finished data; investigate
   rendering, display-envelope selection and continuity handling across scale
   changes. A continuous source with sufficient samples must remain continuous
   at supported scales. Preserve explicit gaps when source data is truly missing.
3. **Add exact numeric entry beside the time-scaling knob.** Provide an editable
   numeric field like the other knobs, with explicit time units and
   seconds/division semantics. Keep knob, field and plot synchronized in both
   directions; validate positive finite values within the supported range and
   retain keyboard and wheel operation.

4. **Preview mouse scaling changes both time and amplitude.** Additional
   maintainer report (2026-10-25): mouse scaling in the full Live Oscilloscope
   correctly changes the time scale, but the embedded preview changes both
   time and amplitude scales. For the same mouse gesture, the preview must
   match the full viewer's horizontal-only scaling and preserve channel
   amplitude scales and vertical positions. Reproduce in both presentations
   using the same signal and gesture; check that any existing explicit vertical
   adjustment remains independent. The precise gesture/modifiers and underlying
   handler are not yet confirmed. This is a distinct preview interaction defect,
   not evidence that the full viewer's mouse time scaling is broken, and does
   not close the separately reported broken-trace or `dt` issues.

5. **Preview Signals button opens an empty table.** Additional maintainer
   report (2026-10-25): clicking **Signals** in the preview panel opens a window
   whose signals table is empty. Reproduce with a bound observer showing signal
   data. The dock's documented **Signals…** action opens the recorded named-tap
   list; inspect that data path and clarify its relationship to observer input
   channels before changing behavior. Available recorded signals must appear;
   when none exist, explain the empty state and how to record them. Verify
   whether the report concerns this dock action or another preview control.
   The affected observer type, run state and root cause have not yet been
   confirmed. Check before/during/after a run and after reopening the preview;
   keep this finding open until reproduced and resolved.

Read the local `pic/Rigol_DS1000Z_UserGuide_EN.pdf` before implementing these
controls, particularly its horizontal time-base scale/position and coarse/fine
adjustment descriptions. Use it to guide the interaction contract. Verify with
a known-frequency sine at several model `dt` values, the initial run and reruns,
and repeated zoom-in/zoom-out through both knob and numeric entry. Record the
reproduction settings and add focused regressions for confirmed causes. These
are presentation fixes; model-time display controls must not alter simulation
`dt` or runtime execution semantics.

### Constraints and acceptance for the next pass

- Keep scale and position changes local to observation presentation. Changing
  time/division must not change model `dt`, causal execution, source sampling,
  raw analysis retention or trigger-capture semantics. Preserve the distinction
  between full-rate samples and selected display-envelope samples.
- Keep Qt work inside Studio and project-owned controls; do not modify vendored
  UI sources. Hardware-specific probe controls and hardware Run/Stop semantics
  are not implied by this design.
- Check multiple channels and units, independent scaling/position, coarse/fine
  and exact entry, reset behavior, live/frozen/finished runs, raw-value readouts
  and trigger markers. Use the symplectic oscillator example for visual tests;
  integrator research remains separate.
- Inspect narrow windows and multiple display scales for plot space, clipped
  controls, stable hover/status geometry and readable five-digit values. Verify
  several visible periods at different model `dt` values. Explain any additional
  problems found to the maintainer before expanding the proposed fixes.

## Verification

The integrated Release suite passed 82/82 with 56 GUI workflow cases. After
visual refinements, the affected observer workflows passed 9/9 and exact
value/trigger/cursor tests passed 15 cases. Final light/dark layout checks passed
at 100%, 150% and 200%, using 1024×768 and 1440×900 logical sizes. Windows
cross-build and Wine Studio smoke passed. Detailed scope and platform limits are
recorded in [the release plan](RELEASE_0.1.1.md). Captures remain outside Git in
`/tmp/s7-controls-final-{100,150,200}`.
