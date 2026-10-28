# Saving and comparing Scope results

In a Scope window, **Save Run…** saves the current recording to a `.s7scope`
file. **Open Run…** opens one or more saved files as comparison traces (thinner and translucent, with a dashed legend swatch),
with the filename, source Scope and a unique comparison number in the legend.
The current run remains a solid trace. All traces use their original model
timestamps; loading does not interpolate, shift time or run the model again.
Use names such as `pendulum_trapezoid_10kHz.s7scope` to identify configurations.

Hover near a Scope trace to see its stored model time and channel value below
the plot. A ring marks the sample and a vertical cursor marks its time without
covering the plotted values. The readout uses the
retained full-resolution recording when it is available, including samples
omitted from the bounded plot preview; imported comparison traces show their
run label. Move outside the trace to clear the readout.

The **Spectrum**, **Correlation**, and **XY Trajectory** tabs use the same light
plot background as Time and analyze a selected run and model-time interval.
The Spectrum tab overlays separate real spectra for all inputs by default;
clear **All inputs** to select one input, or choose I/Q mode for a two-sided
spectrum. Its initial frequency range fits significant peaks; the full
DC-to-Nyquist range is available in **Display → Frequency range**. The initial
analysis interval covers the first quarter of the selected run and follows an
active run until you edit the interval or press **Analyze**.
**Visible time** copies the current Time zoom;
**Full run** selects the run span. Press **Analyze** to take a fresh snapshot.
The Scope header puts display, file, and interval controls on one bar, using
compact icon buttons; hover
or focus them for their names and descriptions. The arrow at the right of the
analysis tabs hides or shows the settings column, giving the plot the full width.
Current runs are read from the authoritative observation source; saved and
archived runs are read from their full-resolution temporary recordings. The
plot preview is never analysis input. The analysis readout below the tabs states whether the configured
analysis sample limit shortened the selected interval. The limit is set under
**Settings → Scope Analysis Limit…**; it defaults to 16,384 positions and is
bounded at 65,536. The analysis panels offer the full selected count as an FFT
or correlation size when at least 64 positions are present. Spectrum uses the
run's rational model rate and displays bin spacing; correlation and trajectory
use same-step channel pairs. Gaps are not filled or interpolated.

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
`.s7scope` files. Results from Studio's embedded runner include immutable run
provenance: requested and effective model configurations, validated parameter
defaults, embedded library definitions, initialization parameters, resolved noise
seeds, rational rate, requested steps/probes, model SHA-256, run UUID/generation,
and software/element versions. This records the functional model, not the diagram
layout or a runtime checkpoint. Keep the diagram for further visual editing.

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

## Run details and reproduction

**Run details…** opens a read-only snapshot for the current run or an imported/
retained comparison. Summary shows identity, model fingerprint, rate, initialization,
seeds, software and capture status. Effective model and Requested model tabs
preserve the exact JSON number text. Later edits, clearing a display, reset or
rerun cannot change the previous source's provenance. Standalone data without
provenance is explicitly identified as such.

The runner freezes defaults and embedded schema-library definitions before
compilation. S7 White/PinkNoiseGenerator requests with `seed=0` get a recorded,
nonzero seed before factories run; the original request remains available.
Reproduction uses the effective configuration and recorded probes/dt/step count,
not the current editor. `configurationFromProvenance` validates the model fingerprint and rate and
returns configuration for an explicit run; opening a result never executes it.
The helper returns the original requested step count, which can be -1 for a
continuous run: reproducing a stopped/partial prefix still requires choosing its
finite boundary. A saved Scope may contain a subset of the original probes.

Tests verify exact same-platform sample reproduction for linear/nonlinear models
and seeded S7 noise. Matching implementations and external input histories are
required: file/network/hardware input data is not captured by this metadata.
Arbitrary plugins and cross-platform RNG/bitwise equivalence are not guaranteed.
The build identity includes project revision and whether tracked sources were
modified at build time; a dirty build requires retaining those source changes.
Hashes identify configuration content, not file authenticity.

## File format, version 2

All numeric fields are little endian. No Qt object serialization is used.

| Field | Encoding |
|---|---|
| Magic | Eight bytes `S7SCOPE\n` |
| Version | Unsigned 32-bit integer, `2` |
| Header length | Unsigned 32-bit integer, at most 16 MiB |
| Header | UTF-8 JSON object |
| Samples | Consecutive `(time_s, value)` IEEE binary64 pairs, channel by channel in header order |

Header fields: `scope` (string), `saved_utc` (ISO UTC string),
`dropped_samples` (unsigned decimal string), `incomplete` (boolean),
`storage_warning` (string), and `channels` (array). An `observation` object carries source run identity, active/finished/stopped state, run error, channel types and step ranges when available. Each channel has `name`
(string), `samples` (positive unsigned decimal string), and an optional `unit`
(string; absent means unspecified). Counts use strings
to avoid JSON floating-point rounding. Channel names must be unique; the loader
allows at most 1,024 channels. Trailing bytes, unsupported versions, bad counts,
non-finite samples and unordered timestamps are errors. Version 1 files are
rejected explicitly; this pre-MVP format increment has no compatibility reader.
The `observation.provenance` object has its own `format_version=1` contract.
Requested/effective model JSON is stored as strings to preserve large integers;
SHA-256 covers the canonical effective JSON. Header length is checked before
allocation; malformed provenance or a model fingerprint mismatch rejects the
entire import without changing existing traces.

Both loaded and live series use the same bounded preview and temporary-disk
history implementation. Comparison count and disk use grow with imported data;
**Remove comparisons** releases displayed projection buffers. Session-owned raw histories remain until diagram replacement/shutdown; imported-only buffers are released when removed. A preview is decimated for
plotting; saved files retain the available full-resolution recording.

## Verification

```bash
cmake --build build --target build-tests --parallel 2
ctest --test-dir build -R '^s7studio_(scope_lifecycle|observation_source|observation_capture|run_provenance|viewers)$' --output-on-failure
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


N1b adds `s7studio_run_provenance` for saved-config replay, exact uint64/default/
nested-library/seed preservation, edit/reset/rerun isolation, large/oversized
headers and corrupt-provenance rollback. `s7studio_viewers` checks read-only Run
details on live, imported and missing-provenance recordings.


Supplied probe units are frozen in run provenance and saved in Scope channel
metadata as `unit`. Imported comparison legends retain those annotations.
Units affect labels only; samples are not converted. A shared axis containing
different or missing units is explicitly identified. See the
[visual usability audit](usability/2026-10-21/AUDIT.md) for measured GUI evidence.
