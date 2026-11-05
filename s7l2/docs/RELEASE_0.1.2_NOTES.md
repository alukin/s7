# S7 0.1.2 preview

S7 0.1.2 is a maintenance release of the 0.1 preview line. It fixes defects
found in Studio's plots, observation windows and diagram editor since 0.1.1,
checks every bundled example in Studio, and includes the runtime work merged
into `main` since 0.1.1. Studio still runs a schema with its bundled causal
graph runtime; no separate service is needed for local runs.

## Studio fixes

- **Plot axes.** Scope, Phase Portrait, Spectrum and the other ordinary plots
  label their axes in steps of 1, 2, 2.5 or 5 times a power of ten, with the
  same number of decimals on every label of an axis. Previously Qt Graphs chose
  steps such as 0.3 for a ±1 signal, and rounding residue at zero could print
  as `2.2045e-16` over the axis title.
- **Live Oscilloscope Auto Y** chooses a round scale (1, 2, 2.5 or 5 times a
  power of ten per division) with the divisions on round values, instead of
  stretching the data extremes over eight divisions. Units/div readouts and
  channel axis labels show values such as `0.5` rather than `0.27085`.
- **XY Trajectory and Constellation / XY dots** are drawn as small dots in the
  series color. Qt Graphs' default 16-pixel bordered squares merged into a
  thick black band that hid the trajectory. At most 16,384 dots are drawn, as a
  uniform subset that keeps the newest pair, and the status says so; hovering
  still inspects every raw pair. Newest-pair, peak-lag and inspected-pair
  markers are round 9-pixel markers.
- **Phase Portrait** marks the newest pair, as the 3D trajectory does. A
  trajectory resting at an equilibrium was previously a blank plot.
- **Triggered Live Oscilloscope time axis** spans exactly the pre-trigger
  percentage of the window, so its labels are round (−5 … 15 s at 25 % and
  2 s/div rather than −4.99634 … 15.0037 s), and the trigger line sits exactly
  under the pre-trigger handle.
- **Live Oscilloscope time base** starts at the smallest 1–2–5 scale that
  shows a configured run shorter than 20 s whole (0.2 s/div for the 2-second
  AM/FM examples) instead of squeezing it into the right edge of a 2 s/div
  screen. Longer and continuous runs keep 2 s/div. **Horizontal → Fit run**
  shows the whole run at any length, and a time base the user sets is kept for
  later runs.
- **Live Oscilloscope Spectrum** starts with fitted peaks, like the Scope's
  Spectrum tab; **Full frequency band** remains one click away.
- The translucent value box beside the pointer is back in Scope and Live
  Oscilloscope. Plot zoom uses Ctrl++, Ctrl+=, Ctrl+Shift++ and Ctrl+-, and
  Ctrl+] / Ctrl+[ zoom the time axis only.
- The main window no longer blinks when a run starts.
- Diagram editor: elements can be resized on the grid, with undo; text-only
  bodies, schema-backed elements and stateless elements are distinguished.
  Moving or resizing elements no longer freezes Studio, leaves wires behind
  their ports, or loses wires on undo.
- Probes can be renamed from a probed wire's context menu; empty and duplicate
  probe names are refused with a message.
- Toolbars draw flat in every Qt style, and icon-only toolbar buttons have
  accessible names without an ellipsis.

## Examples

Every bundled diagram is now opened, run with the duration its article
documents at the diagram's own rate, and inspected in every observer window
and analysis tab by an automated Studio test (`bundledExamplesRunAndRender`).
The test fails on a run that does not start or complete, a message box, or a
plot whose axes hide or squeeze its data. Three Live Oscilloscope
demonstrations (`2int_osc_live2`, `symplectic_osc_live`, `demo_diagram2`) and
the spectrum reference family are now described in the
[examples guide](STUDIO_EXAMPLES.md).

## Runtime changes included

These were developed for 0.2.0 and merged into `main` after 0.1.1. They are
part of 0.1.2 and change how some diagrams are checked:

- **Rate domains** ([ADR-022](../../doc/S7_Architectural_Decisions.md)).
  Integrators, generators, oscillators, PWM and the frequency meter follow the
  run rate; a stored `dt` or `sample_rate_hz` in a document is a legacy field.
  Durations are seconds, and a duration that is not a whole number of samples
  at the run rate is a validation error. Older documents load unchanged. FIR and
  IIR filters record their design rate and refuse another rate. The
  **Resampler** is the only rate-changing element; feedback across rate
  domains is rejected.
- **Nested schemas** behave exactly like their inlined contents
  ([ADR-024](../../doc/S7_Architectural_Decisions.md)).
- **Coupled Gauss–Legendre integration** for F64 diagrams, with editable
  linear and oscillator examples.
- A fault in any step phase rolls back the whole step
  ([ADR-021](../../doc/S7_Architectural_Decisions.md)).
- Floating-point parameters never silently become infinite or zero; schema
  library elements that contain themselves are rejected.
- Transport: TCP_NODELAY on control and data connections, and TLS session
  resumption is accepted from peers that resume.

## Qualification and limits

Pre-release verification on the development host (Ubuntu 26.04, GCC 16
development snapshot, Qt 6.10.2): the native Release suite passes 101/101
tests, including the Qt GUI workflow, plot and 3D suites under Xvfb with
software OpenGL (llvmpipe) and the four example shards, which open, run and
inspect all 73 bundled diagrams. These are headless rendering checks; the
final release binaries are built and qualified separately, and their platform
results belong here before publication.

Windows remains experimental. Physical GPU, high-DPI, accessibility, other
Linux distributions, larger graphs and multi-hour soaks remain outside this
release qualification, as in 0.1.1.

This is pre-MVP software: APIs, file formats, and protocols can change.
Observation storage is temporary rather than crash-durable. Save or export
results explicitly. Display previews can skip updates under load; authoritative
capture is separate. Live observer bindings are fixed for a run.

The [0.1.1 notes](RELEASE_0.1.1_NOTES.md), the
[numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md) and the
[observer latency profile](OBSERVER_UI_LATENCY_PROFILING.md) describe the
earlier tested envelopes.
