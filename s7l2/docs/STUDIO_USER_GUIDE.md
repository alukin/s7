# S7 Studio user guide

S7 Studio lets you draw, run and observe causal graphs. Local simulations use
the bundled runtime; you do not need a separate service or certificates.
Press **F1** or choose **Help → S7 Studio Help** to open this offline guide.
Search matches topic titles and article text. Use Back, Forward and Home to
navigate. Help and About can stay open while you work.

## First simulation

1. Choose **File → Load Demo Diagram**, or open a saved `.s7diagram` with
   **File → Open**. Installed examples are in `usr/share/s7/examples` beneath
   the Linux package directory.
2. Choose **Run → Run Configuration** and set model duration and
   discretization frequency. Frequency sets model time (`dt = 1/f`), not a
   promised wall-clock speed.
   Duration accepts fractional seconds, for example `0.125`. A warning appears
   below 100 whole model steps; short runs remain allowed. Only complete steps
   at or before the duration boundary execute. Select **Run continuously until stopped** for continuous runs.
3. Start the local run from the Run menu or toolbar. Pause and resume as
   needed; Stop ends the run. Reset prepares a fresh run.
4. Double-click a wired Scope observer to view its channels. Phase Portrait
   uses X/Y inputs; 3D Phase Trajectory uses X/Y/Z inputs from the same steps.
5. Save the diagram with **File → Save As**. Save a recording separately with
   **Save Run…** in its Scope window.

## Editing a diagram

The [bundled examples catalogue](STUDIO_EXAMPLES.md) describes each family,
what to observe and the supported numerical envelopes.

Use the Element Library to add elements. Select an element and press **F4**
or **Enter** to edit its properties. Typed fields check values; the advanced
JSON editor is available for structured parameters.

Toggle wire drawing with **Ctrl+W** or the drawing toolbar, then connect output
and input connectors. Every functional input needs a driver; types must match.
Insert explicit conversion elements when needed. Feedback must contain causal
memory. Check the Problems tool window for errors before running.

**Ctrl+C / Ctrl+X / Ctrl+V** copy, cut and paste selected subgraphs.
**Ctrl+Z / Ctrl+Shift+Z** undo and redo edits. Save the diagram before closing
or replacing it; the Save/Discard/Cancel prompt protects unsaved changes.
**View → Fit Diagram to Window** fits the contents; **View → Fit Page** fits
the sheet. Page settings affect the diagram sheet.

## Running and observing

A run freezes its configuration. Edits made during execution apply to the
next run. Pause takes effect at a completed causal step. Stop and close wait
for the current element operation, so a slow plugin can delay them.

For bounded live inspection, add **Observers → Live oscilloscope (prototype)**,
connect its inputs and double-click it. Its settings change the acquisition window
in samples or model seconds during a run. Hover snaps to raw samples and shows
exact stored values, model time and step. Freeze holds the display while the model
continues. See [Live oscilloscope prototype](LIVE_OSCILLOSCOPE_PROTOTYPE.md) for
examples, retention behavior and current limits. Run Configuration lists live and
full-recording channel counts; live-only acquisition does not accumulate disk data.

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

See [Preview limits](qrc:/s7studio/help/docs/RELEASE_0.1.0.md) for the qualified
Linux platform and deferred work. **Help → About S7 Studio** lists the author,
contributors, software version, build details and licences. Include build
details, reproduction steps and relevant errors in an
[issue report](https://github.com/alukin/s7/issues).
