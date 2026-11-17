# Hardware I/O live tests — 2026-11-16

These opt-in experiments exercised the production providers after the initial
mock-backed implementation. Both lab hosts ran Fedora 45 Linux with PipeWire
1.6.11 and PortAudio 19.7; the public build used GCC 16.2.1 and SoapySDR 0.8.1.
The second host's name does not imply Windows qualification. Device addresses,
radio-specific fixtures and private adapter details are retained outside this
public repository.

## Audio results

All digital loops used 48 kHz interleaved F32 stereo with explicit named virtual
endpoints. Marker length was 96,000 simultaneous frames. Test routes were created
temporarily, without changing system default endpoints, and removed afterward.

| Experiment | Observed result |
| --- | --- |
| Local native PipeWire playback → sink monitor capture | Stereo marker bit exact; maximum absolute error 0 |
| Local PortAudio playback/capture through ALSA's PipeWire device | Stereo marker bit exact; maximum absolute error 0 |
| Remote PortAudio playback/capture through ALSA's PipeWire device | Stereo marker bit exact; maximum absolute error 0 |
| Native PipeWire two-host return loop | Both stereo markers, 23 seconds apart, bit exact; all seven processes exited successfully |
| Local USB audio input, native PipeWire and PortAudio separately | 96,000 stereo frames per backend, finite samples, no reported fault; only statistics retained |

The two-host harness ran its source for 28 seconds and return capture for
38 seconds. The reference contained 1,728,000 frames and the return 1,824,000.
Both markers gave a **394.29 ms software round-trip estimate**, using local
host read-completion anchors and nominal sample rate. This includes application
queues and scheduling uncertainty; it is not an acquisition/presentation
timestamp or calibrated physical latency.

The network leg was **SSH raw PCM**, not the S7 wire protocol or a production
multi-boundary graph. Virtual endpoints do not qualify independent physical
clocks, long-term drift, a physical DAC/cable loopback, or remote physical audio
hardware. The remote host exposed virtual audio endpoints only.

Failure injection also exercised real callbacks and routing:

| Injection | Observed result |
| --- | --- |
| Capture owner stalls 100 ms, 1,024-frame application queue | Overflow after a valid prefix of 5,120 frames |
| Playback owner stalls 500 ms, 8,192-frame queue | Underflow; 8,192 accepted frames and 15,360 separately counted emergency output frames |
| Remove the explicitly selected virtual endpoint | Disconnected after 4,608 valid frames; process returned in about 24 ms without falling back to another source |

## Findings fixed

1. Native PipeWire sink-monitor capture needed explicit `capture_sink=true`.
   Explicit targets now disable policy fallback, movement and reconnection.
   Regression tests check the properties sent to the real adapter's fake C API.
2. PipeWire `GAP`/`EMPTY` buffers denote neutral media. Capture now produces
   zero-filled frames without reading absent backing storage; corrupted data and
   later discontinuities still fault. A fake-buffer regression covers silence.
3. An IIO TCP disconnect initially terminated the process with SIGPIPE. Linux
   backend calls now block it on the calling thread, consume only a newly pending
   signal when appropriate, and restore the caller's mask and errno. Process-wide
   disposition and caller-blocked/pending signals are preserved. A socketpair/fork
   regression includes an unprotected negative control. Providers must guard
   backend operations outside the base stream hooks as documented in the SDK guide.
4. The separately maintained radio adapter shortened every transfer deadline to
   100 ms. It now respects the caller's remaining deadline and registers token
   cancellation to interrupt the native transfer. Its own contract regression
   covers deadline conversion.

## Radio evidence and limits

The separate private provider completed RX graph runs on two AD936x-family
radios, exercised native and converted types, reversed a two-channel request,
and tested cancellation. The public Soapy provider separately completed C32 and
CI12 capture on one radio; a CI16 request correctly failed because its reported
native full scale was 2,048 rather than 32,768.

With explicitly authorized 2.4 GHz transmission, changing a generated tone's
offset moved the detected peak by the corresponding amount. On/off comparisons
separated that response from a stationary unrelated spur. This is RF signal
evidence, not a measurement of radiated power, channel phase coherence or gain
calibration. Detailed settings and spectra remain with the private provider.

A scoped TCP proxy injected a 200 ms backend delay, then disconnected only its
own IIO session. After the fixes the process survived the delay, preserved a
98,304-frame prefix, reported continuity loss with native EPIPE, and exited
about 2.6 ms after disconnect. A fresh direct RX session then passed. Token-only
native cancellation returned in about 0.1 ms. No physical unplug was tested.

Observed host delivery in the nominal 2.5/4 MSPS experiments was roughly
1.2–2.2 million frames/s. The bottleneck was not isolated, and hardware loss
counters were unavailable. Successful API calls therefore **do not certify
lossless full nominal-rate capture**. Exact sample-rate readback remains required;
a requested 2,400,000 Hz that read back as 2,399,999 Hz was rejected.

Both radios' original controls were saved before testing. Driver PHY
reinitialization was needed to remove loaded FIR state and restore the original
internal DAC clock chain. After reapplying saved settings, 95 selected checks
matched on each radio, including FIR state and RX/TX path rates. AGC-controlled
gain may naturally change after AGC is reenabled.

## Reproduction and automated checks

Build the opt-in `s7hardware_probe` and follow
[tools/hardware/README.md](../tools/hardware/README.md). It exercises direct
provider calls; scalar graph invariants remain covered separately. The public
two-host harness is `tools/hardware/audio_loopback.py`. Normal CTest never
discovers devices or transmits RF.

Final regression commands:

```sh
cmake --build /tmp/s7-hardware-build --parallel 3
cmake --build /tmp/s7-hardware-build --target \
  s7hardware_smoke s7hardware_backend_signal_smoke s7hardware_plugin_smoke \
  s7audio_smoke s7audio_portaudio_smoke s7audio_pipewire_smoke \
  s7sdr_smoke s7sdr_soapy_smoke s7core_step_rollback_smoke \
  s7core_rate_inference_smoke --parallel 3
ctest --test-dir /tmp/s7-hardware-build \
  -R 's7(hardware|audio|sdr)|s7core_(step_rollback|rate_inference)_smoke' \
  --output-on-failure
ctest --test-dir /tmp/s7-hardware-sanitizer --output-on-failure
ctest --test-dir /tmp/s7-hardware-nodeps -R s7hardware --output-on-failure
```

Final results: 14/14 focused Debug checks, 7/7 public module sanitizer checks,
7/7 backend-disabled profile/SDK checks, and 1/1 private adapter check in each
optimized and sanitizer build passed. The sanitizer harness instruments the new hardware/audio/SDR modules and tests;
it links an installed, uninstrumented `s7core`. Separate private adapter checks
do not depend on publishing its source. See the
[implementation guide](HARDWARE_IO.md) for the initial execution profile and
remaining features. Results are observations of this bench, not universal
backend/device qualification.
