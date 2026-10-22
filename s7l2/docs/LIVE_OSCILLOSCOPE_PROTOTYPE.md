# Live oscilloscope prototype

This prototype provides bounded **Time**, **Spectrum**, **Constellation / XY**
and **Correlation** tabs from the [live oscilloscope design](LIVE_OSCILLOSCOPE_DESIGN.md).
Shared [cursors, measurements and JSON/CSV export](STUDIO_WINDOW_INSPECTION.md) are
available. Triggers, saved workspaces and operational qualification remain incomplete;
the presence of four tabs does not establish the ADI OSC parity milestone.

## Try it

1. Build Studio and open `examples/2int_osc_live.s7diagram`. For multichannel
   work, use `examples/signal_measurement/am_coherent_detection_live.s7diagram`
   or `fm_frequency_detection_live.s7diagram` at 10 kHz. Both use seven live
   channels and zero full-recording channels.
2. Double-click the `live_1` observer. Alternatively, add **Observers → Live
   oscilloscope (prototype)** to a diagram and wire signals to its `+` input.
3. Start a run. Run Configuration offers **Run continuously until stopped** and
   reports the live and full-recording channel counts.
4. Change **Retain samples / channel**, or enter a positive decimal duration in
   seconds and press Enter. Scientific notation is accepted. Duration rounds up
   to complete model-step intervals; the effective span is displayed.
5. Hover over the plot. The displayed frame is held while the pointer is over
   the plot, and a vertical cursor snaps to the nearest retained sample. Readout
   shows run, step, model time, raw value, supplied unit and source/storage type.
   Select the readout text to copy it. Moving out resumes live updates.
6. **Freeze display** holds that frame while acquisition continues. Wheel zoom
   and drag pan also freeze it. **Resume live / Fit** returns to the latest window.
   Display gain, offset and visibility do not modify captured values.

For analysis, choose **Spectrum** for a real channel or I/Q pair, or
**Constellation / XY** for same-step X/Y dots, lines or density. **Correlation**
compares A and B; a positive lag means B occurs later. These tabs consume retained
raw values, independently of Time-tab gain/offset. FFT and correlation need N
consecutive retained samples; **Filling** means more samples or a smaller N are
needed. See [FFT controls](STUDIO_SPECTRUM.md) and
[XY/correlation controls](STUDIO_PAIR_ANALYSIS.md).

Only the visible tab of a visible, non-minimized window runs analysis. Hidden
tabs stop polling and cancel queued work. Acquisition continues while the window
is hidden or minimized; returning analyzes the retained window, restarting FFT
averages/hold and XY persistence. Desktop focus changes alone do not suspend a
visible window. Try `examples/signal_measurement/iq_correlation_live.s7diagram`
at **1024 Hz for 8 seconds**: I/Q forms a unit circle, and correlation of I against
I_delayed_6 peaks at **+6 samples** with N=4096 and maximum |lag|=16.

The example uses explicit Euler integrator networks, like the original
`2int_osc.s7diagram`. Its amplitude growth is the known numerical behavior of that
example, not an oscilloscope fault. Unthrottled simulation can advance model time
much faster than wall time. Use a finite run or Studio's simulation pause to inspect
later stages without implying real-time pacing.

## Retention and precision

There are at most eight live channels **across all live workspaces in a run**.
Each workspace retains 2–65,536 positions per channel, default 4,096. Capacity can
change during a run or while paused. Shrinking discards old history; growing fills
forward and never resurrects discarded history. Settings survive reruns while the
same viewer instance exists, but are not saved in the diagram yet. A duration
setting requires an attached run so its rational model rate is known.

Each ring reserves twice the maximum logical positions before stepping starts.
The spare physical history lets a snapshot copy 256-frame chunks while the producer
continues. No worker allocation or whole-window copy occurs during resize. The
worst ring allocation under the aggregate eight-channel limit is 24 MiB (eight
single-channel workspaces); this excludes bounded display snapshots, Qt/GPU data
and schema provenance. Logical retention is independent of the fixed reservation.
Snapshots whose physical data is overwritten during copying are discarded and
retried on a later refresh, with an explicit status message.

Live-only probes never create the runner's full-run journal or preview mailbox
traffic. Existing Scope, phase and named-probe recording remains enabled as before;
its disk usage can grow in continuous operation. A shared output is read once per
step and routed to the requested sinks. Closing a live viewer leaves its bounded
acquisition running; reopening shows the current retained window. Reset detaches
old sources and clears the display. A frozen frame survives ring wrapping/resizing.

Hover uses retained raw binary64 values, including samples omitted from the reduced
plot. Values are shown with 17 significant digits. This preserves stored double
precision; it does not recover precision lost when a wider source type was converted.
Time reduction retains bucket endpoints and extrema in chronological order, so
single-step spikes remain visible. The current renderer suppresses a channel's
trace if the frame contains gaps or nonfinite values, reports why, and still allows
raw inspection; segmented traces are follow-up work.
If display gain or offset overflows, the affected trace is also hidden with an
explicit message; reducing the display settings restores it without changing raw
samples.

## Current limits

- The [Spectrum tab](STUDIO_SPECTRUM.md) now supplies real/IQ FFT analysis,
  windows, amplitude/power/PSD, referenced dB, power averaging and peak hold.
  [Constellation / XY and Correlation](STUDIO_PAIR_ANALYSIS.md) supply same-step
  pairs, model-step persistence/density and normalized linear correlation.
  [Paired cursors, inclusive region statistics and frozen-window JSON/CSV export](STUDIO_WINDOW_INSPECTION.md)
  share the displayed input. Triggers, saved workspaces, window import and image
  export are not implemented here. The Time view currently rolls with acquisition;
  it has no edge trigger to stabilize periodic waveforms.
- One shared numeric Y axis; supplied units remain explicit. No implicit conversion
  or separate unit lanes. Colors are assigned automatically.
- Plot refresh is capped near 30 Hz. Rendering and snapshot processing run on the
  UI thread with fixed data caps; FFT, XY and correlation use a bounded shared
  worker pool. Only the visible tab of a visible, non-minimized window submits
  work; inactive analysis cancels queued jobs and stops polling.
  Throughput, latency targets, long-duration resource
  behavior and native Windows rendering still require qualification.
- The prototype does not claim full design completion or ADI OSC parity.

## Verification

`s7studio_live_oscilloscope` covers multiple physical ring wraps, coherent concurrent
snapshots/resizing, exact rational duration boundaries, extrema preservation, raw
hover precision, journal-free live capture, mixed recording, pause/resume/stop/rerun,
freeze, resize while frozen and viewer destruction/reopen. The native GUI workflow
suite opens the example through Studio, changes acquisition settings, drives actual
mouse hover through QML, and exercises freeze/resume/reset.

Use `cmake --build build-release --target build-tests` then
`ctest --test-dir build-release -R 's7studio_(live_oscilloscope|gui_workflows)' --output-on-failure`.
Rendered GUI tests require Xvfb/OpenGL on Linux. Offscreen data/QML tests alone do
not establish correct on-screen rendering.
