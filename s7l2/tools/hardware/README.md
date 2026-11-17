# Opt-in hardware experiments

`s7hardware_probe` exercises the production provider API directly. It is excluded
from normal builds, CTest and installation. These experiments supplement the
scalar graph tests; they do not relax `hardware::Run` clock/profile validation.

```sh
cmake -S . -B build-hardware -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release \
  -DS7_PORTAUDIO=ON -DS7_PIPEWIRE=ON -DS7_SOAPYSDR=ON
cmake --build build-hardware --target s7hardware_probe --parallel 2
```

A request file describes exactly one endpoint:

```json
{
  "backend": "pipewire", "device": "s7_test_loop", "direction": "capture",
  "type": "F32", "channels": ["0", "1"], "rate": [48000, 1],
  "block_frames": 256, "queue_frames": 8192,
  "options": {"capture_sink": "true"}
}
```

`capture_sink` explicitly selects a sink monitor. Ordinary microphone/line-input
capture omits it. Native PipeWire explicit targets disable default fallback,
movement and reconnection; a missing/removed endpoint faults the stream.

```sh
build-hardware/s7hardware_probe --request capture.json --frames 48000 \
  --output /tmp/capture.f32 --summary /tmp/capture.json
```

Playback reads `--input <raw-file>` or generates an F32/C32 tone with
`--tone-hz <offset>` and `--amplitude <normalized-value>`. `--output -` and
`--input -` provide raw interleaved native PCM on Linux stdin/stdout. Summary
and progress use stderr. Capture/playback counts are complete simultaneous
frames; playback counts mean acceptance, with abort-on-completion semantics.
Trailing guards keep marker verification independent of unpresented output tails.

Other useful options:

| Option | Experiment |
| --- | --- |
| `--provider-library <path>` | Explicit external provider, including a separately built private adapter |
| `--cancel-after-ms <n>` | Concurrent stream cancellation, reporting return latency |
| `--cancel-token-only` | Use the supplied stop token without an extra direct cancel call |
| `--stall-after-frames <n> --stall-ms <n>` | Owner stalls to expose bounded-queue overflow/underflow |
| `--ready-file <path> --start-gate <path>` | Prepare endpoints before a coordinated application start; gate wait is bounded to 15 seconds |

Gate synchronization is application scheduling evidence, not hardware clock or
RF phase synchronization. `first_transfer_steady_ns` is host read completion;
it is never a hardware acquisition timestamp. Missing driver loss counters stay
unknown even when the API returns success.

## Two-host digital audio loopback

This experiment routes generated stereo markers through local capture, an SSH
PCM pipe, remote playback/capture, another SSH PCM pipe and local playback/
capture. It uses the production sound providers, not the S7 network wire
protocol. The first profile's production graph restrictions remain unchanged.

1. Build/deploy a matching `s7hardware_probe` and public shared libraries on a
   second Linux host. A temporary directory plus `LD_LIBRARY_PATH` suffices;
   system installation is unnecessary. Keep an SSH session open if that host's
   user audio services stop at logout.
2. Create named null sinks on both hosts with `pactl load-module module-null-sink`.
   On the local host create `s7_test_loop` and `s7_return_loop`; on the remote host
   create `s7_test_loop`. Use 48 kHz stereo and retain returned module IDs for cleanup.
3. Run the script, requiring local NumPy and SciPy for marker generation/analysis:

```sh
python3 tools/hardware/audio_loopback.py \
  --probe /path/to/s7hardware_probe --host labhost \
  --remote-probe /tmp/s7-lab/bin/s7hardware_probe --remote-lib /tmp/s7-lab/lib \
  --work /tmp/s7-loopback-local --remote-work /tmp/s7-loopback-remote --seconds 28
```

The local work directory must be empty. The experiment writes raw reference and
return PCM, per-process logs/results, and `analysis.json`. It checks both channel
values bit-for-bit at two separated markers. Round-trip estimates use local host
read-completion anchors and nominal rate; callback/queue scheduling uncertainty
remains. A short successful test does not establish long-term independent-clock
stability. Remove the created null-sink modules with `pactl unload-module <id>`
when finished; system default endpoints need not be changed.

On Linux, PortAudio's ALSA PipeWire device can be routed per process using the
PipeWire ALSA plugin's `PIPEWIRE_NODE` and `PIPEWIRE_ALSA` properties. Device IDs
must come from that host's current enumeration. Tests of that route qualify the
PortAudio/ALSA/PipeWire path, not other host APIs or an analog cable loopback.

Radio-specific fixtures and measurements stay with the separately maintained
provider. RF experiments require explicit device, band, gain and path selection;
no RF transmission is part of normal tests or this audio script.
