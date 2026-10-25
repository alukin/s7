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

The gain dial maps positions 0–1200 to `10^(-6+p/100)`, with exact numeric entry
from 1e-6 to 1e6. Position has ±1e12 numeric entry and a wrapping relative dial.
A full turn changes the offset by half the visible Y span; there is no half-span
control or movable interval. Numeric edits never pass through the dial. Editing
either gain or position disables Auto Y, preserving the shared scale and other
channels' positions. Auto Y can be enabled again explicitly.

Project-owned painted QDial subclasses retain Qt keyboard, wheel, mouse and
accessibility behavior. Channel accents, dark graticule and compact readouts
form a consistent instrument surface. Numeric faces show five significant
digits while inactive; focus and tooltips expose the complete round-tripping
value. The compact face never changes the stored value. Gain and position sit
side by side with their numeric fields below the knobs.

Channels and Time window start expanded; Trigger and Window inspection start
collapsed. All sections collapse independently. The control column scrolls
vertically and can be hidden. Hover readouts and status use fixed-height
scrollable viewports, and worker progress reserves its space while hidden.
Pointer movement therefore cannot resize the plot or shift the settings panel.
The plot header and dock preview status explicitly identify the retained model
time interval, distinguishing it from the full run duration.

Spectrum and paired-analysis settings retain their wrapping, bounded-height
settings layouts.

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

## Verification

The integrated Release suite passed 82/82 with 56 GUI workflow cases. After
visual refinements, the affected observer workflows passed 9/9 and exact
value/trigger/cursor tests passed 15 cases. Final light/dark layout checks passed
at 100%, 150% and 200%, using 1024×768 and 1440×900 logical sizes. Windows
cross-build and Wine Studio smoke passed. Detailed scope and platform limits are
recorded in [the release plan](RELEASE_0.1.1.md). Captures remain outside Git in
`/tmp/s7-controls-final-{100,150,200}`.
