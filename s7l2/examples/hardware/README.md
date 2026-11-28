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

## Mono and stereo audio blocks

`AudioBlockSource` captures channel `"0"` (mono) or channels `"0"` and `"1"`
(stereo) in one stream and publishes one planar `F32[channels, block_frames]`
block per model step; both rows cover the same sample positions. The model
step rate is the audio rate divided by `block_frames` (48 kHz / 2048 =
23.4375 Hz). `AudioBlockPSD` in the spectral plugin computes a one-sided
power spectral density per channel, with the capture rate as its frequency
scale.

When a diagram contains an `AudioBlockSource`, Run Configuration lists the
inputs of the available audio backends, including PipeWire monitors, with
channel counts when the backend reports them, and offers **Mono** or
**Stereo**. For a live view of a microphone without blocks, see
[Microphone spectrum](../audio/microphone_spectrum.md).

`s7studio_array_persistence` checks mono and stereo blocks with a scripted
provider at 48 kHz: 1 kHz on the left and 3 kHz on the right, each PSD peak in
its own channel, and the shared starting sample of each block.
