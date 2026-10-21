# Saving and comparing Scope results

In a Scope window, **Save Run…** saves the current recording to a `.s7scope`
file. **Open Run…** opens one or more saved files as comparison traces (thinner and translucent, with a dashed legend swatch),
with the filename, source Scope and a unique comparison number in the legend.
The current run remains a solid trace. All traces use their original model
timestamps; loading does not interpolate, shift time or run the model again.
Use names such as `pendulum_trapezoid_10kHz.s7scope` to identify configurations.

**Clear display** clears only this presentation. Recorded source data remains available for export and for replay after viewer recreation.
Starting a run clears current samples; with **Clear scope traces before run**
unchecked, it first moves the preceding recording into a labelled `Run <ID>`
comparison, preserving its storage and capture-loss warnings. Reset clears live
samples. Phase views always restart their history and step alignment. Imported comparisons remain until **Remove comparisons** is clicked
or the Scope is destroyed (for example, when closing its diagram/application).
Closing and reopening the Scope window keeps both its current recording and
comparisons. Reopen saved files after restarting Studio.

A saved file contains only the current recording, not imported comparisons.
**Export Full CSV** includes current and comparison traces; the `scope` column
distinguishes their labels. CSV is an interchange export; **Open Run…** reads
`.s7scope` files. Run settings, the model diagram, integrator parameters and
runtime checkpoints are not stored in a Scope result. Run identity, source signal types and per-channel step ranges are included when saving an attached source. Save the diagram
separately to reproduce the computation.

## Snapshots and completeness

Saving/exporting during execution snapshots the authoritative source prefix,
independently of UI delivery or plot replay progress. Capture runs on the worker
before the preview mailbox. Its memory is bounded and disk writes apply
backpressure to wall-clock execution; they do not change model time. A storage
write/final-flush failure stops the run and marks retained results incomplete.
Preview drops are reported separately and do not make recording incomplete.
Explicit save/export/import currently perform synchronous paged work and can
delay the UI; making bulk operations asynchronous remains responsiveness work.
Temporary capture is not crash-durable. Explicit saved files are the persistence
boundary.

The saved result includes the Scope name, UTC save time, channel names, recorded
timestamps/values, dropped-sample count, incomplete flag and storage warning.
Imported incomplete recordings show **INCOMPLETE** in the legend and retain a
status warning. Saving a partial recording preserves only the retained data;
it cannot restore lost samples. CSV does not carry this completeness metadata.

Saving through the UI uses `QSaveFile`: a failed save does not replace an
existing destination. Opening validates the entire file into independent
buffers before adding comparisons; invalid/truncated files leave displayed
runs intact. Save/open require finite values and strictly increasing timestamps
within each channel on import. Empty current recordings cannot be saved.

## File format, version 1

All numeric fields are little endian. No Qt object serialization is used.

| Field | Encoding |
|---|---|
| Magic | Eight bytes `S7SCOPE\n` |
| Version | Unsigned 32-bit integer, `1` |
| Header length | Unsigned 32-bit integer, at most 65,536 bytes |
| Header | UTF-8 JSON object |
| Samples | Consecutive `(time_s, value)` IEEE binary64 pairs, channel by channel in header order |

Header fields: `scope` (string), `saved_utc` (ISO UTC string),
`dropped_samples` (unsigned decimal string), `incomplete` (boolean),
`storage_warning` (string), and `channels` (array). An `observation` object carries source run identity, active/finished/stopped state, run error, channel types and step ranges when available. Each channel has `name`
(string) and `samples` (positive unsigned decimal string). Counts use strings
to avoid JSON floating-point rounding. Channel names must be unique; the loader
allows at most 1,024 channels. Trailing bytes, unsupported versions, bad counts,
non-finite samples and unordered timestamps are errors.

Both loaded and live series use the same bounded preview and temporary-disk
history implementation. Comparison count and disk use grow with imported data;
**Remove comparisons** releases displayed projection buffers. Session-owned raw histories remain until diagram replacement/shutdown; imported-only buffers are released when removed. A preview is decimated for
plotting; saved files retain the available full-resolution recording.

## Verification

```bash
cmake --build build --target build-tests --parallel 2
ctest --test-dir build -R '^s7studio_(scope_lifecycle|observation_source|observation_capture|viewers)$' --output-on-failure
```

The offscreen regression uses the nonlinear Symplectic pendulum and compares
all delivered lifecycle-run values with an independent headless execution. It
covers pause/resume, stop while paused, reset/rerun, hide/reopen, save/export
while running, disk-backed save/open/zoom followed by append, persistent
comparisons, repeated imports, malformed-file rollback and loss metadata.
It intentionally overflows the actual worker mailbox with 220,000 values,
checking that 200,000 remain and 20,000 drops are reported, and injects an
unusable temporary directory to verify storage-failure reporting/persistence.
Native file-picker interaction and actual disk exhaustion are not automated.

With an attached observation source, save/export reads the retained source rather
than the displayed prefix or cleared display. ScopeRecording inside a window is
a presentation/import projection. Source lifetime is independent of windows;
closing and recreating a viewer can replay its retained history. Authoritative capture precedes the preview mailbox, so preview loss does not truncate the source. `s7studio_observation_capture` verifies full saved results under forced mailbox overflow and incomplete metadata under injected write/flush failures. See STUDIO_OBSERVATION_VIEWERS.md.
