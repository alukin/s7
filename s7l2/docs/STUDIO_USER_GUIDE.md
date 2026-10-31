# S7 Studio user guide

S7 Studio lets you draw, run and observe causal graphs. Local simulations use
the bundled runtime; you do not need a separate service or certificates.
Press **F1** or choose **Help → S7 Studio Help** to open this offline guide.
Search matches topic titles and article text. Use Back, Forward and Home to
navigate. Help and About can stay open while you work.

## First simulation

1. Choose **File → Example Diagrams…**. The standard file chooser starts in
   the distribution's examples directory. Open a saved diagram with
   **File → Open Diagram…**. Installed examples are in `usr/share/s7/examples`
   beneath the Linux package directory, or `share/s7/examples` on Windows.
   Examples open as working copies; Save asks for your own destination.
2. Choose **Run → Run Configuration** and set model duration and
   discretization frequency. Frequency sets model time (`dt = 1/f`), not a
   promised wall-clock speed.
   Duration accepts fractional seconds, for example `0.125`. A warning appears
   below 100 whole model steps; short runs remain allowed. Only complete steps
   at or before the duration boundary execute. Select **Run continuously until stopped** for continuous runs.
3. Start the local run from the Run menu or toolbar. Pause and resume as
   needed; Stop ends the run. Reset prepares a fresh run.
   The Run toolbar uses icons; hover over any button, including a disabled
   button, to see its description. Full action names remain in the Run menu.
4. Double-click a wired Scope observer to view its channels. Phase Portrait
   uses X/Y inputs; 3D Phase Trajectory uses X/Y/Z inputs from the same steps.
5. Save the diagram with **File → Save As**. Save a recording separately with
   **Save Run…** in its Scope window.

## Editing a diagram

The [bundled examples catalogue](STUDIO_EXAMPLES.md) describes each family,
what to observe and the supported numerical envelopes.

Drag a leaf entry from the Element Library onto the canvas to place it at the
nearest grid point. This works with a filtered library, at any zoom/pan, and in
an open nested schema tab. A drop creates a new selected instance; the library
entry stays available. Built-ins, schema elements, loaded plugins and observers
use the same defaults as double-click placement. Double-click still chooses a
free spot automatically. Categories cannot be dragged. Press **Esc** to cancel;
dropping outside the canvas changes nothing. Each insertion is one undo step.

Select a canvas element and press **F4**
or **Enter** to edit its properties. Typed fields check values; the advanced
JSON editor is available for structured parameters. **Name** edits the diagram
label while preserving the internal ID, wiring and observation connections.
**Type**, **Library** and **Internal ID** are displayed separately. Library
identifies the registered module, loaded plugin or schema library; unavailable
provenance is stated explicitly.

Right-click an element in the library and choose **Element Information…** for
its full description, ports, timing notes and JSON parameter reference. Keyboard
users can focus a library row and press the context-menu key (or Shift+F10).
For a selected canvas element, use **Edit → Element Information…**. Information
is read-only; text can be selected and copied. Properties shows the same
documentation above the parameter editor, with an adjustable splitter and
independent scrolling. Double-clicking a schema still opens its nested editor;
double-clicking an observer still opens its viewer.

Toggle wire drawing with **Ctrl+W** or the drawing toolbar, then connect output
and input connectors. Every functional input needs a driver; types must match.
Insert explicit conversion elements when needed. Feedback must contain causal
memory. Check the Problems tool window for errors before running.

**Ctrl+C / Ctrl+X / Ctrl+V** copy, cut and paste selected subgraphs.
**Ctrl+Z / Ctrl+Shift+Z** undo and redo edits. Save the diagram before closing
or replacing it; the Save/Discard/Cancel prompt protects unsaved changes.
**View → Fit Diagram to Window** fits the contents; **View → Fit Page** fits
the sheet. **Diagram → Page** sets the active diagram's paper size and
orientation. **Diagram → Diagram Properties…** edits the root diagram name and
multiline description. The editing tab shows the name, falling back to the file
basename for an empty or `untitled` name, or **Untitled** for an unnamed document.
Name and description edits support undo/redo and save/reopen.
Drawing and text-frame commands are also under **Diagram**.
Text frames default to **10 pt**; the toolbar and **Diagram → Text Frame →
Font and Size…** allow **1–72 pt**, including sizes below 10, and preserve larger
imported sizes. Bundled examples
use at least 10 pt. Selecting a text frame reveals the drawing toolbar and its
font controls. Double-click a frame (or select it and press F4) to edit its text
with font and size controls in the same dialog. Font changes preserve the text
selection and apply to the whole frame. Text and formatting changes support
undo/redo. Text scales normally with diagram zoom.
With the root or nested diagram focused, **Ctrl++** or **Ctrl+=** zooms in
and **Ctrl+-** zooms out. Shifted Plus and keypad Plus are supported.
**Ctrl+mouse wheel** also zooms. These shortcuts leave focused text fields
alone; View menu zoom commands still operate on the active diagram.

Toolbar colors follow the Qt application palette supplied by the desktop
(including KDE's Qt integration). Icons update with palette changes; disabled
icons use the palette's disabled text color.

## Running and observing

**Run → Keep Previous Scope Traces** and the identically labelled setting in
Run Configuration control whether the next run keeps the preceding Scope
recording as a comparison. This does not clear or modify an active run.
The **Nodes** menu groups the existing remote deployment commands and opens
the Headless Nodes panel. Local simulation commands stay under **Run**.

A run freezes its configuration. Edits made during execution apply to the
next run. Pause takes effect at a completed causal step. Stop and close wait
for the current element operation, so a slow plugin can delay them.

### Run rate, element rates and resampling

The frequency in **Run Configuration** is the run's rate (`dt = 1/f`), and it
is the only source of model time
([sample rate model, section 8](../../doc/S7_Sample_Rate_Model.md#8-model-time-and-element-rate-classification-2026-10-31)).
Changing it never edits the diagram:

- Integrators, generators, oscillators, PWM and the frequency meter follow the
  run's rate. Their durations are times in seconds (`start_time`,
  `sweep_time`, `bit_time`, PWM `period`), so events stay at the same model
  time; a duration that is not a whole number of samples at the new rate is
  reported in the problems list. Older diagrams that store `dt`,
  `sample_rate_hz` or sample counts still open; those legacy fields are kept
  in the Advanced JSON tab but no longer shown for editing.
- FIR and IIR filters record the rate their coefficients were designed for
  (the diagram's rate when the filter was added, or the saved `dt` of an older
  diagram). A run at another rate stops with an error naming the filter,
  because raw coefficients cannot be recalculated; redesign them for the new
  rate.
- Files, recordings and network streams keep their own rate. If it differs
  from the run's, the run does not start and the error names the element and
  both rates.
- Nested schemas run at the run's rate and behave exactly like their contents
  placed in the parent diagram. A feedback loop may close through an
  integrator or delay inside a nested schema; a loop through it with no such
  element on the path is reported as a cycle without causal memory. Problems
  inside a nested schema are reported with the element's path, for example
  `plant/integrator`.

To connect parts that run at different rates, place a **Resampler**
(Conversion) where they meet. At equal rates it passes values through
unchanged. Its `order` selects 0 (hold the latest sample, the default),
1 (linear) or 2 (quadratic) interpolation; orders 1 and 2 delay the signal by
one input sample so that they interpolate between received samples. When it
lowers the rate it first filters against aliasing, which adds a further delay
of about half the filter length; `stopband_attenuation_db` (default 60) trades
filter length for rejection. Feedback loops that cross rates are not supported
yet. Observers still record once per run step.

For bounded live inspection, add **Observers → Live oscilloscope (prototype)**,
connect its inputs and double-click it. Its settings change the acquisition window
in samples or model seconds during a run. Hover snaps to raw samples and shows
stored values, model time and step, formatted to five significant digits. Freeze holds the display while the model
continues. See [Live oscilloscope prototype](LIVE_OSCILLOSCOPE_PROTOTYPE.md) for
examples, retention behavior and current limits. Run Configuration lists live and
full-recording channel counts; live-only acquisition does not accumulate disk data.

**Tools → Observers** (**Alt+3**) opens compact previews for root
observer instances. The first three observer IDs are selected automatically
until you choose your own selection. Use **Observers…** to change it. An explicit
empty selection is preserved when saving and reopening. Each card can collapse, freeze independently or **Open** its
full viewer. Selection and collapse state are saved with the diagram. Starting
a run activates this dock once; choosing another dock during execution is
respected. **Recorded Signals…** manages explicitly named wire taps and their ad-hoc
viewers. It lists those taps, rather than every observer input. An empty list
explains how to use **Add Probe Here** on a connected wire and run the diagram;
observer channel bindings remain available in each preview's details.
See [Observers dock](STUDIO_OBSERVERS_DOCK.md) for source bindings, lifecycle and
bounded-work behavior.

The controls column scrolls under the pointer, including its blank areas and
right edge. Reaching a scroll limit keeps the wheel within the controls.
Knobs and numeric editors retain their wheel adjustments.

In **Vertical**, select a **CH n** button or click its name in the plot legend
to adjust that channel's **Scale**, **Gain ×** and **Position**. Selection leaves visibility
unchanged; use the separate checkbox to show/hide a trace. For a focused legend
row, Enter selects the channel and Space toggles visibility.
**Scale** accepts raw signal units per one of eight vertical divisions,
such as `0.25 V/div`. Changing Scale adjusts that channel's gain while holding
the plot range and other traces fixed. Channels with different units keep
independent settings; an unspecified unit appears as `units/div`. The colored
Y axis shows the selected channel's original units, accounting for its gain
and position. Selecting another channel changes the axis labels without moving
traces. Hover values and measurements retain original signal values. Changing
Gain also updates Scale. The larger **Scale** knob now operates directly in
units/division. Coarse adjustment follows **1–2–5** values; its **Fine** button
selects 100 logarithmic steps per decade without changing the current value.
Clockwise rotation, Right/Up and a forward wheel notch reduce units/division
(zoom in); Left/Down zoom out. Each wheel notch applies one step. Page Up/Down
apply three steps. Numeric spin arrows increase/decrease the displayed number,
and exact entry remains available in either mode. An exact value between coarse
steps moves to the next value in the chosen direction. Range endpoints remain
reachable even when they fall between 1–2–5 values.
The colored checkboxes beside channel buttons and in the plot legend toggle
visibility. Each channel keeps independent settings. Numeric faces show five
significant digits; focusing an editor or hovering its tooltip exposes the full
value, including scientific notation. Numeric entry retains full precision.
Position is a continuous relative knob: one turn moves half the visible plot
height. Its knob has a **Zero** button; double-clicking the knob also
resets only that channel's display position to zero. There is no half-span
setting. Changing scale, gain or position turns off
**Auto Y**, holding the shared scale so other channels remain stationary; enable
Auto Y again to fit all visible traces. Raw values and trigger thresholds stay
unchanged.

Colored numbered badges at the left of the Time plot mark each visible channel's
raw zero. Drag a badge up/down to select and move only that trace. Coincident
badges spread out in the margin; their leaders and arrow tips still indicate the
true zero levels. Double-click a badge or press Home while it has focus to reset
that channel's position to zero. Up/Down adjusts position; Shift uses smaller
steps. Off-screen levels have an arrow at the plot edge and can be dragged back
into view. Hiding a trace hides its zero badge.

In Auto, Normal or Single trigger mode, orange **L** controls the trigger level
and **P…%** controls the pre-trigger percentage. Hover L to see the threshold in
the trigger input's original units, including while dragging. The trigger input
can differ from the channel selected for the Y axis. Trigger drags preview first;
release applies the setting and starts fresh trigger history, just like the
fields. Escape cancels a pending trigger drag. Rejection preserves the accepted
settings and appears in status. These handles disappear in Rolling mode.

The **P** handle sets coverage for the next acquisition; it does not reposition
the recorded event. The existing **T** line stays at the captured sampled event,
including when you pan or zoom a frozen capture. P and T can therefore appear at
different positions. Left/Right adjusts P by one percent; Home/End selects
0/100%. L uses Up/Down (Shift for smaller steps). Marker gestures do not pan,
zoom or place inspection cursors. Narrow Time plots number fewer ticks while
retaining the ten-division grid and the full tick-value precision. A changed run/source or trigger preset cancels
pending edits from the previous binding.

**Horizontal → Time / div** sets model seconds per division across ten horizontal divisions.
Turn clockwise to zoom in, or edit the adjacent **s/div** field for an exact
value. The timebase has the same coarse **1–2–5** steps and independent **Fine**
button as the vertical scale. Switching adjustment modes preserves the exact
value. The knob, numeric readout and mouse-selected time range stay synchronized;
the selected scale survives reruns and changes of model `dt`. Live preview wheel
zoom and drag pan move only the time axis, preserving amplitude and position.
The default **2 s/div** shows a **20 s** rolling window at every model dt. A
bounded display history preserves endpoints and channel extrema using at most
4096 original sample positions. Changing dt therefore changes detail rather than
the visible model-time span. Enlarging the scale merges the retained history;
older discarded data cannot be recovered, and new coverage fills forward.

**Analysis retention** separately controls full-rate samples for FFT/correlation
and sampled trigger captures. Its default remains 4096 samples. Triggered Time
views use the same seconds/division setting, independently of capture sample
count or model `dt`. Partial display history is labelled when available samples
cover only part of the requested interval. Use Time base to inspect a capture
more closely.

Zoomed Time views use original raw samples while the visible interval is still
retained raw. Wider overviews use bounded display samples. If fine raw detail
has expired, the viewer says so; zooming does not reconstruct missing detail.

On the Time tab, **Vertical**, **Horizontal**, **Analysis retention**, **Trigger** and **Window
inspection** collapse independently. The column scrolls vertically; **Hide
controls** gives the plot the full width. Hover readouts and acquisition status
remain stable while the pointer moves. Scale, position and timebase knobs are
the same size, and input fields use less vertical space. The Time sample readout
starts at about three lines. Drag the divider above it to resize the readout, or
drag down to hide it and give the controls more room. Expanded settings remain
scrollable. Hover values and times use five significant digits. Compact status
stays at the bottom.

The live oscilloscope has four tabs sharing the same acquisition:

| Tab | Use it for |
|---|---|
| **Time** | Signals over model time, display gain/offset, and retained-sample hover |
| **Spectrum** | Real or I/Q FFT, window/size selection, amplitude/power/PSD, explicit dB references, averaging and peak hold |
| **Constellation / XY** | Two same-step channels as dots, segmented lines or density; equal numeric aspect and persistence in model steps |
| **Correlation** | Auto- or cross-correlation, normalization, maximum lag and a peak-delay marker; positive lag means B occurs later than A |

See [Live FFT spectrum](STUDIO_SPECTRUM.md) and
[Live constellation and correlation](STUDIO_PAIR_ANALYSIS.md) for controls and
worked examples. FFT and correlation need at least N retained consecutive samples;
increase **Raw samples / channel** or reduce N if the view reports **Filling**.
The model sample rate determines frequency and lag in seconds.
All four tabs use the same layout: full-height graph on the left and grouped,
scrollable settings on the right. Hover values use five significant digits in
a scrollable readout (resizable on Time, eight lines on analysis tabs), with
compact status at the bottom. Hide controls expands
the active graph. Analysis retention and Window inspection remain available on every
tab.

Only the visible tab of a visible, non-minimized window analyzes data. Switching
tabs, hiding/closing or minimizing suspends analysis while bounded acquisition
continues. Returning analyzes the retained window and restarts FFT averages/hold
and XY persistence. Visible windows keep updating when desktop focus moves away.
**Freeze** pins the displayed result and its raw input; changing analysis
settings reanalyzes that input. **Resume / Fit** returns to current data.
Use **Pin window / A–B** or click the plot to place persistent cursors and freeze
their input. **Measure A–B** computes inclusive region statistics from raw values
or explicitly labeled derived bins/lags. A and B each have a slider and an exact
index; their identities remain unchanged when crossing. **Export: …** saves bounded
window JSON, raw CSV or displayed-data CSV with metadata; it does not start a
recording. Use its arrow to select the format. The export progress bar reports
worker completion; **Cancel export** cancels that export. See [Live cursors, measurements and export](STUDIO_WINDOW_INSPECTION.md)
for keyboard movement, units, coverage and file formats. To stabilize periodic
Time traces, expand **Trigger — sampled edge** before starting a run: select
Auto/Normal/Single, source, edge, raw threshold, hysteresis, pre-trigger and holdoff.
The sampled trigger stays at time zero; absolute timestamps remain intact. See
[Live edge-trigger synchronization](STUDIO_TRIGGERING.md) for a worked example,
timeout/partial-capture behavior and sampling limits. Saved workspaces remain pending.

For the recording Scope and phase viewers, the plot preview can skip updates under load. Authoritative recording is
separate; skipped preview updates do not imply lost recorded samples.
Temporary recording uses disk after a bounded memory buffer. If storage fills,
the run stops with an explicit error and the retained recording is incomplete.
Choose the temporary directory under Settings and keep free disk space.

Use Scope **Save Run…** to persist `.s7scope` recordings, **Open Run…** to add
saved comparisons and **Export Full CSV** for interchange. CSV does not carry
all recording metadata. **Run details…** shows the frozen model and provenance.
These Scope actions use compact icon buttons with tooltips and accessible names.
Scope also has **Time**, **Spectrum**, **Correlation**, and **XY Trajectory** tabs.
The header keeps its controls on one bar. Hovering over a Time trace places the
exact sample value below the plot, leaving the trace visible. All four tabs use
a light plot background. Spectrum overlays real spectra for all inputs by default;
clear **All inputs** to select one source, or choose I/Q mode for a two-sided FFT.
Use the arrow at the right of the analysis tabs to hide or show their settings
column and expand the plot.
Choose a current or comparison run, enter a model-time interval, then press
**Analyze**. **Visible time** copies the Time plot's zoom range; **Full run**
selects the chosen recording's span. The initial interval covers the first
quarter of the run; changing it or analyzing holds that choice. Spectrum starts
with a fitted frequency range, and **Display → Frequency range** offers the
full band through Nyquist. Analysis reads exact retained samples,
never the reduced drawing points. The Spectrum and Correlation controls include
an exact full-interval sample count when it fits, including counts that are not
powers of two. Spectral bin spacing is the model sample rate divided by FFT N.
Correlation offers cross and auto modes; XY pairs channels at the same logical
step. Missing steps and nonfinite samples remain visible as analysis errors,
without interpolation. The analysis readout below the tabs reports the sample count, `dt`, an
incomplete recording, and any sample-cap truncation. For very long intervals,
**Settings → Scope Analysis Limit…** chooses the maximum retained positions
per analysis request (16,384 by default; up to 65,536). The newest positions
in the selected interval are used at the cap. Re-run **Analyze** after changing
the interval or while a continuous run advances.
Closing a viewer and reopening it retains its source while the diagram stays
open; explicitly save results before closing the diagram or application.

Read [Saving and comparing results](qrc:/s7studio/help/docs/SCOPE_RESULTS.md)
for completeness and replay behavior, or
[Observation viewers](qrc:/s7studio/help/docs/STUDIO_OBSERVATION_VIEWERS.md)
for plot and trajectory controls.

## Troubleshooting

- **The model cannot run:** check Problems for missing inputs, incompatible
  types, feedback without causal memory, or missing element plugins.
- **A curve drifts or diverges:** inspect the integration method, parameters
  and step size. A plausible plot does not establish numerical accuracy. See
  the [validated numerical envelopes](qrc:/s7studio/help/docs/NUMERICAL_VALIDATION_MATRIX.md).
- **Recording is incomplete:** read the storage warning, free disk space or
  select another temporary directory. Export retained data if needed, then
  release old recordings and start a fresh run.
- **Save/open/export appears slow:** large recording operations currently run
  synchronously. Let the operation finish before closing Studio.
- **A 3D view is blank:** check that the graphics driver supports Qt Quick 3D.
  Native GPU and broader platform acceptance remain preview limitations.
- **FFT or correlation shows Filling:** retain at least N samples per channel,
  let the buffer fill, or choose a smaller N. Retention growth cannot restore
  evicted history. A frozen view keeps its original input; resume live to refill.
- **Correlation is unavailable:** inspect its status for missing steps, nonfinite
  values or zero-energy coefficient inputs. A constant signal becomes zero-energy
  after mean removal. Check channel bindings and normalization.
- **XY points or FFT averages disappear after changing tabs:** analysis history
  restarts after inactivity. Acquisition retains its bounded raw window independently.

See [Preview limits](qrc:/s7studio/help/docs/RELEASE_0.1.0.md) for the qualified
Linux platform and deferred work. **Help → About S7 Studio** lists the author,
contributors, software version, build details and licences. Include build
details, reproduction steps and relevant errors in an
[issue report](https://github.com/alukin/s7/issues).
