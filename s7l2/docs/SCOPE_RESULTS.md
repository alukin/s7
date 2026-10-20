# Saving and comparing Scope results

In a Scope window, **Save Run…** saves the current recording to a `.s7scope`
file. **Open Run…** opens one or more saved files as comparison traces (thinner and translucent, with a dashed legend swatch),
with the filename, source Scope and a unique comparison number in the legend.
The current run remains a solid trace. All traces use their original model
timestamps; loading does not interpolate, shift time or run the model again.
Use names such as `pendulum_trapezoid_10kHz.s7scope` to identify configurations.

**Clear current** discards the current recording and its capture warnings.
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
runtime checkpoints are not stored in a Scope result. Save the diagram
separately to reproduce the computation.

## Snapshots and completeness

Saving/exporting during execution snapshots the samples already delivered to
Scope on the UI thread. The worker continues; subsequent/queued samples are
outside that snapshot. File I/O runs synchronously on the UI thread, so large
snapshots/imports can delay delivery. Pause before large file operations when
capture completeness matters; this feature does not remove the bounded mailbox
or promise lossless capture under arbitrary load.

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
`storage_warning` (string), and `channels` (array). Each channel has `name`
(string) and `samples` (positive unsigned decimal string). Counts use strings
to avoid JSON floating-point rounding. Channel names must be unique; the loader
allows at most 1,024 channels. Trailing bytes, unsupported versions, bad counts,
non-finite samples and unordered timestamps are errors.

Both loaded and live series use the same bounded preview and temporary-disk
history implementation. Comparison count and disk use grow with imported data;
**Remove comparisons** releases their buffers. A preview is decimated for
plotting; saved files retain the available full-resolution recording.

## Verification

```bash
cmake --build build --target s7studio_scope_lifecycle_test
ctest --test-dir build -R '^s7studio_scope_lifecycle$' --output-on-failure
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
