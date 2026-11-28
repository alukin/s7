# Hardware examples

These graphs are never activated by normal tests. `s7hw --check <file>` checks
the graph and initial execution profile without device I/O; `--run <file>
--frames <n>` explicitly activates the selected device. See the
[hardware guide](../../docs/HARDWARE_IO.md).

- `audio_capture.json`: stereo native PipeWire capture, discarded after graph
  evaluation; verifies transfer counts. Add downstream DSP through channel ports.
- `audio_playback.json`: stereo 440 Hz tone, normalized amplitude 0.02.

See the [SDR examples](../sdr/README.md) for radio capture.

For PortAudio, change `backend` to `portaudio`; `device` may be `default` or an
index reported by `s7hw --devices portaudio`. For PipeWire it may be `default`,
a node name or object.serial selector. Audio channels initially use contiguous
zero-based positions. The runner uses no wall-clock throttle in addition to
device pacing. Playback completion aborts pending output; the final accepted
tail is not guaranteed to be presented.

## Planned mono and stereo live spectrum

Choose a mono or stereo input in Studio before starting the run. The chooser
lists available inputs reported by the selected audio backend, including
PipeWire monitor sources where available, and shows their channel counts.

Capture all selected channels in one stream. Each graph step publishes an
`F32[N]` vector for each channel, covering the same sample positions. Run a
one-sided PSD per channel and plot the results in one viewer on shared frequency
and power axes. Use the capture sample rate to label frequency in hertz.

Mono displays one blue **Mono** trace. Stereo displays a blue **Left** trace
and an orange **Right** trace. Label the traces in the legend.

At 48 kHz with 2048 samples per block, verify that a mono 1 kHz input yields
exactly one trace. For stereo, use 1 kHz on the left and 3 kHz on the right.
Check the trace colors and labels, the peak for each channel, and the shared
starting sample of both blocks.
