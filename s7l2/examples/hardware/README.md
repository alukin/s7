# Hardware examples

These graphs are never activated by normal tests. `s7hw --check <file>` checks
the graph and initial execution profile without device I/O; `--run <file>
--frames <n>` explicitly activates the selected device. See the
[hardware guide](../../docs/HARDWARE_IO.md).

- `audio_capture.json`: stereo native PipeWire capture, discarded after graph
  evaluation; verifies transfer counts. Add downstream DSP through channel ports.
- `audio_playback.json`: stereo 440 Hz tone, normalized amplitude 0.02.
- `sdr_receive.json`: receive through an installed SoapySDR driver; replace
  the device selector before activation. C32 explicitly chooses driver conversion.

For PortAudio, change `backend` to `portaudio`; `device` may be `default` or an
index reported by `s7hw --devices portaudio`. For PipeWire it may be `default`,
a node name or object.serial selector. Audio channels initially use contiguous
zero-based positions. The runner uses no wall-clock throttle in addition to
device pacing. Playback completion aborts pending output; the final accepted
tail is not guaranteed to be presented.
