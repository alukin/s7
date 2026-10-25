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
Gain also updates Scale.
The colored checkboxes beside channel buttons and in the plot legend toggle
visibility. Each channel keeps independent settings. Numeric faces show five
significant digits; focusing an editor or hovering its tooltip exposes the full
value, including scientific notation. Numeric entry retains full precision.
Position is a continuous relative knob: one turn moves half the visible plot
height. There is no half-span setting. Changing scale, gain or position turns off
**Auto Y**, holding the shared scale so other channels remain stationary; enable
Auto Y again to fit all visible traces. Raw values and trigger thresholds stay
unchanged.

**Horizontal → Time / div** sets model seconds per division across ten horizontal divisions.
Turn clockwise to zoom in, or edit the adjacent **s/div** field for an exact
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
have fixed-height scrollable areas so pointer motion does not move the layout.
Hover values and times use five significant digits. The compact status area
sits at the bottom, leaving more room for channel readouts.

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
an eight-line readout, with compact status at the bottom. Hide controls expands
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
