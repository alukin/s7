# Stabilize periodic Time traces with an edge trigger

The Live Oscilloscope's **Time synchronization** controls align successive captures
to a selected signal event. They operate on complete model-step samples on the
acquisition producer; GUI refresh timing cannot miss an edge. Triggering observes
the model and never changes its causal execution or sample rate.

## Quick start

1. Open `examples/signal_measurement/iq_correlation_live.s7diagram` and open
   `iq_scope` **before starting the run**. Set retention to **128 samples**.
2. In Time, choose **Normal**, Source **I**, **Rising**, Level **0.25**,
   Hysteresis **0.02**, and Pre-trigger **25%**. Level/hysteresis accept scientific
   notation; press Enter to apply edited numbers. Thresholds use supplied raw
   source units, independently of display gain, offset and visibility.
3. Run at **1024 Hz**, continuously or for 8 model seconds. The 16 Hz trace stays
   at a stable phase: the sampled rising event is at **T = 0**. Orange T marks
   that position; negative times are pre-trigger samples.
4. Select **Single** and start a new run to hold the first complete capture.
   **Rearm** waits for a new capture while bounded raw acquisition continues.
   A finished run cannot provide new edges; start a new run to capture again.

Settings made in an existing window apply before the first step of a new run.
Changing trigger settings on a running model starts fresh trigger history.
Enabling triggering after a run has finished does not retrospectively search its
retained data. Use Rolling to inspect that data, or configure triggering and rerun.

## Modes

| Mode | Behavior |
|---|---|
| **Rolling** | Existing latest-window display with absolute model time; trigger detection is disabled |
| **Auto** | Use qualifying edges; after the configured model-step timeout, publish a full, explicitly **UNTRIGGERED** window if no edge qualified |
| **Normal** | Wait for a qualifying edge and complete post-trigger coverage; keep the preceding completed capture while waiting, or show an empty plot before the first one |
| **Single** | Hold one complete triggered capture; Rearm releases the single-shot hold without stopping simulation |

Auto needs enough samples for a full retained window. Its timeout counts model
steps from the beginning of the new acquisition bank; holdoff still suppresses
acquisition events. An untriggered timeout frame uses absolute time and has no
T marker. It does not claim synchronized phase.

## Edge, hysteresis and coverage

For positive hysteresis H, a rising edge arms at/below `level-H/2` and fires
at/above `level+H/2`; falling is symmetric. With zero hysteresis, a genuine change
across the level is required. A constant equal-to-level plateau does not repeatedly
trigger. Invalid or unrepresentable settings leave the previous settings active
and display an error.

For C retained positions and pre-trigger percentage p, the capture contains
`floor(p*(C-1)/100)` positions before the firing sample, the firing sample itself,
and the remaining positions after it. Arming requires enough contiguous prehistory.
**Holdoff** ignores triggers for the requested number of model steps after a
capture ends. Events during post-trigger collection are also ignored.

Gaps or a nonfinite trigger-source value abort a pending capture and restart edge
qualification/prehistory; interrupted captures are counted. No missing samples
are interpolated or zero-filled. Nonfinite values on other channels retain the
existing trace/measurement coverage rules.

Stopping during post-trigger collection publishes a clearly labeled **PARTIAL**
capture with its actual samples and expected window size. The uncollected portion
stays blank. Normal/Single do not fabricate a trigger when the run ends while
waiting. Rearm preserves the preceding completed capture until a new one is ready;
changing trigger settings or retention discards pending trigger history. A separately
frozen display retains its immutable old frame through later retention changes.

## Precision, other views and export

The trigger is the **first qualifying stored sample**, not an interpolated crossing.
The Time axis uses `(step-trigger_step)/Fs`. Absolute timestamps and step IDs stay
unchanged in raw data, hover and export. A/B cursors report both absolute model
time and trigger-relative coordinates. Sampling quantization can leave up to a
sample of apparent phase variation; this is not sub-sample trigger interpolation.

FFT, XY and correlation continue to use rolling raw input while following. Freezing
a triggered Time capture lets the other tabs analyze that same pinned input.
Window JSON/CSV metadata records trigger mode/source/edge, thresholds, pre-trigger,
holdoff, timeout, sampled firing step/time, capture ID and partial/triggered flags.
Raw CSV keeps absolute times; JSON display coordinates include relative time.

The producer uses two fixed capture banks in addition to the existing raw ring,
one for collection and one for the latest completed capture. Publication swaps
banks, with no full-window copy or allocation on a model step. GUI reads copy
bounded chunks and abandon a snapshot if a newer capture replaces it. The worst
reserved acquisition storage is now **48 MiB** under the aggregate eight-channel
limit (eight single-channel workspaces), excluding immutable display/export
snapshots, schema provenance and Qt/GPU storage. There is no recording journal or
unbounded trigger history. Capture continues when a window is hidden; inactive
FFT/XY/correlation panels still submit no analysis work.
