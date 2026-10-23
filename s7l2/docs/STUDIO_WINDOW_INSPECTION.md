# Live cursors, measurements and window export

The controls beneath the Live Oscilloscope tabs inspect and export **one bounded
displayed window**. They use stored raw values or computed analysis arrays, never
values read back from plot pixels. They do not start full-run recording.

## Place and move A/B cursors

1. Choose Time, Spectrum, Constellation / XY or Correlation and wait for data.
2. Click **Pin window / A–B**, or click inside the plot. This freezes the displayed
   result and its raw input. A initially selects index zero; B selects the last
   position. Indices are zero-based.
3. Choose **Click sets A** or **Click sets B**, then click a position. Time snaps
   to a raw sample, Spectrum to a bin, and Correlation to an integer lag. Ties
   choose the earlier position. XY chooses a raw pair within 12 screen pixels,
   breaking exact distance ties toward the newest step—even in density mode.
   A density bin itself is an aggregate, not a cursor's raw point.
4. Edit **A index** / **B index**, use their Up/Down keys, or press **Alt+Left** /
   **Alt+Right** to move the selected cursor one position. Indices also let you
   inspect coincident XY points from different model steps.
5. Read A, B and signed B−A differences, with supplied units. Time and XY also
   show model steps, times, Δstep, Δt and `1/Δt`. The reciprocal is a cursor
   interval calculation, not an automatic signal-frequency estimate. Zero Δt
   reports unavailable. Spectrum shows baseband and offset-axis frequency;
   Correlation shows lag in samples and model seconds.

**Copy readout** copies unscaled, 17-significant-digit values. This round-trips
stored f64 values; it cannot restore precision lost from a wider source type.
Nonfinite input and overflowing deltas are explicit. A/B markers remain separate
from the transient hover cursor. Panning and zooming preserve the selected indices.

**Resume live / Fit** or releasing Freeze clears the selection and resumes updates.
Changing tabs, analysis bindings/settings or starting/resetting a run also clears
the selection. Time display gain/offset does not change the selected raw values.
Re-pinning an already-frozen tab cancels pending analysis so a queued newer result
cannot replace the cursor's displayed owner.

## Measure an inclusive A–B region

Click **Measure A–B**. The readout puts measurements first; scroll for all channels
and cursor details. It updates when either endpoint moves. Reversed endpoints are
accepted: statistics use the inclusive interval between them, while deltas remain
signed B−A.

- **Time:** every acquired channel's raw samples between the two selected steps,
  including channels hidden in the plot. Display gain/offset is ignored.
- **XY:** raw X and Y pairs between the two selected steps, including retained
  persistence points. Invalid pairs omitted from the XY plot remain visible as
  missing-step coverage; they are not reconstructed or interpolated.
- **Spectrum/Correlation:** explicitly labeled statistics over selected **linear
  bins/lags**, followed by raw-channel statistics for the latest transform input
  interval. RMS of bin/lag values is not time-domain signal RMS. The full raw
  history of an average or peak hold is not promised by the latest input frame.

Each column reports count, min/max with positions, mean, RMS, peak-to-peak and
population standard deviation (divisor N). Compensated, scaled summation and
shifted variance handle large values and small variation. Overflowing metrics
report unavailable. Nonfinite coverage makes that column's aggregates unavailable;
values are not silently discarded. Missing steps are counted, and statistics
cover observed positions only. No interpolation, zero filling, crossing-based
frequency estimate or rise/fall-time estimate is applied.

## Export the frozen window

Choose a format, then **Export frozen window…**. Export pins the displayed input
before opening the destination dialog. A/B annotate the exported measurements;
**the file contains the entire pinned window**, rather than cropping it to A–B.

| Format | Contents |
|---|---|
| **Window JSON (.s7window)** | Raw acquisition samples, displayed samples/pairs or computed bins/lags, cursor selection, measurements, channel units/source and stored types, rational model rate, retention/loss status, provenance and analysis/display settings captured at pinning |
| **Raw acquisition CSV** | Every raw row and channel in the result's retained input frame; older acquisition history can extend beyond the latest transform's input interval |
| **Displayed data CSV** | Every displayed raw sample/XY pair or full computed bin/lag array, including linear peak hold when present; plot reduction and dB display flooring do not replace these values |

The JSON format has root fields `metadata`, `raw_samples` and `displayed_rows`.
`metadata.format` is `s7.window`, version 1. Step/run/generation identities use
decimal strings, preserving integers above 2^53. Floating-point values use
round-trip decimal strings; `NaN`, `Infinity`, `-Infinity` and signed zero are
explicit. Raw metadata distinguishes source type from stored f64. Display rows
include an offset/time axis coordinate where applicable. XY density is
reconstructible from exported raw pairs, grid size and bounds; no density count
is mislabeled as a sample value.

CSV starts with one `# ` line containing compact JSON metadata, followed by a
quoted column header and data rows. Skip the metadata line in a conventional
CSV reader. Decimal points use the C locale. Columns are `index`, `step`,
`model_time_s`, `axis`, then named values with units. Step/time fields are empty
for derived bins/lags; their input interval is in metadata. The axis column uses
the baseband frequency or integer lag; display offsets/units are in metadata.
Raw and displayed CSV identify their own column sets separately.

Export owns an immutable snapshot, so ring wrapping/resizing and tab changes
cannot change its values. Progress and **Cancel export** are available; one export
per window is admitted, with a shared serial I/O worker. Output is streamed with
cancellation checks every 128 rows. An existing destination is replaced atomically
only after successful writing. Cancellation or write failure before commit leaves
it intact. The final atomic commit cannot be interrupted. Closing/resetting the
window requests cancellation; export never pauses causal execution.

These files are bounded-window exports, not complete-run `.s7scope` recordings.
Studio does not yet reopen `.s7window` files. Use the recording Scope's Save Run /
Open Run workflow for that purpose. Plot-image export and saved workspaces remain
future work. [Edge triggering](STUDIO_TRIGGERING.md) stabilizes periodic Time
traces. In triggered captures, raw timestamps stay absolute, display coordinates
are trigger-relative, and export metadata records the sampled edge and coverage.

## Try it

Open `examples/signal_measurement/iq_correlation_live.s7diagram`, run **8 seconds
at 1024 Hz**, and open `iq_scope`. Pin Time, set A=0 and B=4095, then measure: I/Q
have RMS near `1/sqrt(2)` V. Changing display gain/offset leaves those raw statistics
unchanged. Export the JSON, then shrink acquisition retention; the exported window
still contains the original 4096 rows. Try the same controls in Spectrum, XY and
Correlation using their [analysis instructions](STUDIO_PAIR_ANALYSIS.md).
