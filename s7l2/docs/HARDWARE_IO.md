# Sound and SDR I/O

The public implementation consists of `s7hardware` (contract, scalar boundary
and device-paced run owner), `s7audio` (independent PortAudio and native
PipeWire implementations), and `s7sdr` (common SDR elements and optional
SoapySDR provider). The private AD936x provider is maintained and built outside
this repository against the installed SDK. Public builds never fetch or bundle
that provider or its dependency. No GNU Radio or private audio implementation
was copied into these modules.

The [hardware contract](../../doc/S7_Hardware_IO_Contract.md) and ADR-026 describe
the direction. This guide identifies the implemented first profile and its
qualification limits. The [live test report](HARDWARE_IO_LIVE_TESTS.md) records
the first opt-in audio and radio experiments and their measured limits.

## Build and installation

```sh
tools/install_deps_fedora.sh --no-studio
# Ubuntu: tools/install_deps_ubuntu.sh --no-studio
cmake -S . -B build-hardware -DS7_BUILD_STUDIO=OFF \
  -DS7_PORTAUDIO=ON -DS7_PIPEWIRE=ON -DS7_SOAPYSDR=ON
cmake --build build-hardware --parallel 2
cmake --build build-hardware --target build-tests --parallel 2
ctest --test-dir build-hardware -R 's7(hardware|audio|sdr)' --output-on-failure
cmake --install build-hardware --prefix /path/to/install
```

Each backend accepts `AUTO` (build when its development package is found),
`ON` (fail configuration if unavailable), or `OFF` (do not discover/link it).
PipeWire is Linux-only. A simulation-only build sets all three to `OFF`; its
SDK, elements and offline graph checks remain available. `build_for_linux`
requires all three unless `--no-hardware` is given. Linux dependency scripts
offer the same opt-out. These scripts install public dependencies only.

`S7_BUILD_SDR=OFF` excludes the complete SDR module and its element plugin while
keeping sound and the shared hardware contract. It defaults to OFF on Windows
and ON elsewhere. The Windows wrapper requires PortAudio and disables both
PipeWire and SDR; see [Windows builds](WINDOWS_BUILD.md). The examples
`portaudio_capture.json` and `portaudio_playback.json` use this portable backend.

The SDK component is `s7sdk`; CMake consumers use
`find_package(s7hardware 0.1 CONFIG REQUIRED)` and link `s7::s7hardware`,
`s7::s7audio` and/or `s7::s7sdr`. Headers live under `include/s7/`.
Element plugins `libs7audio-elements.so` and `libs7sdr-elements.so` install under
`${libdir}/s7/plugins`. Their metadata/factories perform no hardware I/O.
Load them through the usual runtime/Studio plugin discovery for editing and
validation. Their `step()` refuses execution without an explicit hardware owner.

Studio discovers these plugins at startup in a development build or an
installation. The Element Library places audio boundaries under **Sound and
Video** and SDR boundaries under **Radio**. The Radio library also provides
complex baseband AM and FM modulators and demodulators. Match the AM depth
(greater than zero) or the FM sample rate and deviation between the modulator
and demodulator to recover the normalized message. The FM discriminator
reports the raw phase change in radians per sample. Filters and resamplers
are under **Signal Processing**.

The [stereo FM receiver diagram](../examples/sdr/fm_receiver.s7diagram)
plays live from Studio's **Run** action or from `s7hw`. See its
[tuning and playback guide](../examples/sdr/FM_RECEIVER.md).

## Explicit activation and pacing

```sh
build-hardware/s7hw --check examples/hardware/audio_capture.json
build-hardware/s7hw --check examples/hardware/audio_playback.json
build-hardware/s7hw --check examples/sdr/sdr_receive.json

# Opt-in hardware operations:
build-hardware/s7hw --devices portaudio
build-hardware/s7hw --run examples/hardware/audio_capture.json --frames 480000
build-hardware/s7hw --run examples/hardware/audio_playback.json --frames 480000
```

`--check` compiles and validates the initial execution profile without resolving
providers or opening hardware. It cannot qualify device-specific capabilities.
`--devices` explicitly invokes provider discovery. PipeWire currently reports
only its policy-selected `default` target; obtain other node names/object.serial
selectors with `pw-dump`. PortAudio discovery reports host API, device name and
unstable enumeration index. Soapy requires an explicit driver/device selector;
there is no default-first-radio selection.

`--run` explicitly prepares and activates capture, playback or RF transmission,
according to the element direction. Preparation can open devices and change
stopped RF controls; changes are not transactional. SIGINT/SIGTERM request
cancellation. Transfers have a one-second deadline in this runner; driver
shutdown is not a hard real-time guarantee. The final report separates graph
frames, accepted/delivered frames, terminal status and emergency output frames.

Studio's **Run** action uses the same device-paced runner for capture and audio
playback when the S7 hardware plugins are installed. Use the hardware's nominal
rate in **Run Configuration**. Use **Stop** to release the devices and **Run**
to start a new stream. SDR transmission requires the explicit `s7hw` command.

A capture run steps once per received frame and can feed slower graph domains
through explicit resamplers. It may also feed one `AudioSink` at an integer
divisor of the capture rate. The audio provider buffers complete playback frames.
Capture and playback remain independent hardware clocks.
The runner does not correct for clock drift. Its report includes the separate
number of frames accepted by the audio provider.

A playback-only run uses a single rate domain. Playback stages scalar graph
frames into bounded blocks and waits for device queue capacity. The graph
runs on its owning thread, paced by device input or playback capacity.

Audio playback prefills its configured `queue_frames` before activation; SDR
providers prefill one block. A queue bound not divisible by `block_frames` uses
a shortened final prefill block. A finite run shorter than prefill activates
with its shorter available prefix. **Completion aborts** pending output; it
does not drain the DAC or guarantee presentation of the final accepted tail.
With very short playback runs this may also expose an underflow before stop.
There is no silent zero padding counted as graph output. Future graceful drain
needs separate, bounded backend qualification.

Runs and streams are single-use. A fault, cancellation or completed run requires
a new graph/run/stream segment before resuming. A graph failure cannot roll back
already captured or emitted hardware samples. The boundary latches the existing
`on_step_failure()` notification so ordinary graph retry cannot consume/emit
replacement samples. Copying/moving a live boundary is disabled.

## Elements and formats

`AudioSource`, `AudioSink`, `SdrSource`, and `SdrSink` use the common parameters:

| Parameter | Meaning |
| --- | --- |
| `type` | Explicit application S7 type (or normal graph type inference) |
| `backend`, `device` | Explicit provider and backend-scoped selector; no fallback |
| `channels` | Ordered unique channel IDs; one scalar port per channel |
| `frame_rate` | Reduced exact nominal rate `[numerator, denominator]` |
| `block_frames` | Maximum transfer block; default 256, limit 1,048,576 |
| `queue_frames` | Application handoff/prefill bound; default 4096, limit 16,777,216 |
| `options` | JSON object encoded as a string, with string-valued backend options |

Frames contain one complete real/complex value per selected channel. I/Q are
components of a complex channel, not separately scheduled ports. Channel count
is limited to 64 and a single admitted application queue to 256 MiB; backend/OS
queues are separate and may be unobservable. Option and selector sizes are also
bounded. The provider API transfers into caller-owned storage. Graph-side IQ capture
can use a bounded array lease as that storage.

Audio supports `F32` normalized application PCM (nominal [-1,1]), signed `I16`
and signed `I32` full-scale PCM. Channel IDs must initially be contiguous
zero-based positions (`["0"]`, `["0","1"]`, ...); arbitrary audio channel
remapping is rejected. Rates are integer nominal rates up to 768 kHz, subject
to backend support. Packed PCM24 is unsupported. Physical converter resolution,
OS conversion and server resampling are unknown unless independently qualified;
the adapters make no strict-native PCM guarantee. PortAudio uses its callback
status flags; PipeWire checks negotiated application parameters, buffer/chunk
bounds, discontinuity/corruption flags and stream disconnection.

PipeWire and PortAudio playback accept string-valued
`options.underrun="rebuffer"`. When audio runs short, the callback emits
silence and resumes once the queue holds at least half its capacity and enough
frames for the next callback, within the configured capacity. The runner counts
the silence separately as emergency output. The default `"fault"` policy ends
the run on underrun.

Native PipeWire capture accepts string-valued `options.capture_sink="true"`
to explicitly capture a sink monitor (`"false"` or omission selects ordinary
capture). It is rejected on playback. Explicit targets disable policy fallback;
streams disable movement and reconnection. Neutral `GAP`/`EMPTY` buffers become
zero-filled capture frames, while corrupted data and later discontinuities fault.

Soapy accepts `C32` as explicit `CF32` driver conversion. `CI12` and `CI16`
require driver-reported native `CS16` plus respectively full-scale 2048 or
32768 for every selected channel; a container-width name alone is insufficient.
CI12 values are range-checked. Driver channels use canonical zero-based integer
IDs; they do not establish that RF connectors exist. Supported stopped options
are `frequency_hz`, `bandwidth_hz`, `gain_db` and `antenna`, applied to all selected
channels. On receivers with selectable gain control, set `gain_mode` to `auto`
or `manual`. Automatic gain and `gain_db` are mutually exclusive.
Continuous settings (sample rate, frequency, bandwidth, gain) are realized at
the closest value the device supports and accepted
([ADR-028](../../doc/S7_Architectural_Decisions.md#adr-028)). For example, an
AD9361 tunes 100.1 MHz to 100,099,998 Hz and 2.4 MS/s to 2,399,998 S/s. The
descriptor reports `requested_<setting>` and the realized `<setting>` per
channel, plus `sample_rate_deviation_ppm`; `s7hw` prints them and Studio logs
the realized frequency and rate. The graph keeps its nominal rate as model
time. Discrete settings (`antenna`, `gain_mode`) must read back exactly, or
preparation fails. Receivers can still opt into a strict sample-rate check with
`rate_tolerance_ppm`. A transfer respects the driver's reported stream MTU when
available.

`Descriptor` records the accepted request, randomized session ID, segment and
configuration/timing revisions, startup prefill, amplitude interpretation and
available native/readback details. Conversion, resampling and timestamp support
use supported/unsupported/unknown values. Physical clock identity stays unknown.
Transfer frame positions are software-delivered/accepted positions, never hardware
counters. Soapy `HAS_TIME` carries driver-local time with unspecified epoch;
audio sample timestamps are not exposed by this first implementation. No arrival
timestamp is labeled as acquisition time or UTC.

### IQ vector capture

`SdrBlockSource` in the SDR element plugin publishes one `C32[block_frames]`
vector per graph step on `out`. Select one receive channel and set
`frame_rate` to the nominal RF sampling rate. `block_frames` defaults to 1024
and must match the consumer's vector length. For example, 2.4 MS/s and 1024
samples per block give a Studio model step rate of 2343.75 Hz. Connect the
output directly to `VectorPSD` with `components=1024` and then to Waterfall.
The physical sample rate can come from the provider readback.

The hardware runner fills a bounded writable lease across short device reads,
then seals and publishes the complete block. The sample window carries
software-delivered sample position and the physical rate when reported.
A capture fault or incomplete block ends the run and preserves the last
committed vector. Start a new run after changing the input device or tuning.

Use a single hardware boundary and a single block-rate domain for this profile.
Studio uses its existing device-paced hardware runner. With `s7hw --run`,
`--frames` counts RF samples and must be a multiple of `block_frames`.
In the result, `graph_frames` counts committed vector steps and
`transferred_frames` counts received RF samples, including any incomplete tail.

## C++ ownership and private extensions

Build a schema with `s7::audio::Source/Sink`, `s7::sdr::Source/Sink`, or registered
element factories. Compile normally, then construct
`s7::hardware::Run(graph, provider_resolver)` and explicitly call
`execute(frame_count, transfer_timeout, stop_token)`. The resolver returns a
provider instance; `Run` owns it and its prepared stream. `Run::cancel()` may
be called concurrently; lifecycle and transfer operations otherwise have one
owner. Stop/join any controller before destroying the run. Graph observation
still follows `NodeRuntime`'s stepping-thread rules.

An external provider links only the installed public SDK and its own dependency.
It exports `extern "C" s7::hardware::Provider*
s7_hardware_provider_v1(const char* backend)`, returning null for an unknown
backend. This is a C-named factory with the matching C++ SDK/compiler ABI,
not a compiler-independent C ABI. `ProviderModule` retains library code until
the last returned provider/stream is destroyed, and forwards cancellation.
Use `s7hw --provider-library /absolute/path/to/provider.so ...` to load one.
Loading calls no device factory until the explicitly selected operation needs it.

On Linux, base stream start/read/write/stop hooks run inside `BackendCallGuard`
from `<s7/hardware_backend.hpp>`. It handles socket SIGPIPE without replacing
the process signal disposition, preserves the caller's mask/pending signal and
errno, and lets normal native errors reach the stream contract. Backend-owned
threads created within the scope inherit the blocked mask. Provider authors
must also guard socket-using preparation, discovery, cancellation and destruction
outside these hooks. The guard is a no-op elsewhere; other platforms need
separate qualification.

Public CI builds an external fake provider against an installed SDK. The private
AD936x implementation, its build instructions and device profiles remain outside
this repository. It uses generic public `SdrSource/SdrSink` elements, so private
element metadata is unnecessary in public packages.

## Verification and current limits

Automated tests exercise partial prefixes/writes, variable block sizes, scalar
Delay equivalence, graph failures, cancellation, overflow, underflow, emergency
silence, SPSC wrap/concurrency, offline/plugin loading and installed-SDK extension
lifetime. A registered fake Soapy driver exercises the actual Soapy adapter's
planar/interleaved mapping, timestamp validity, native-width evidence, error
latching and cleanup. No normal test discovers/opens physical devices.
Separate fake PortAudio and PipeWire C APIs exercise the production audio
adapters' negotiation, callbacks, status mapping, native buffer bounds and
cleanup without linking the real audio libraries. The terminal-prefix race
is covered by concurrent capture/drain stress. Native backend error codes are
retained when supplied; zero means unavailable or not applicable.

The initial modules have no duplex/shared-session execution, multiple hardware
boundaries, independent-clock matching, adaptive resampling, hardware timestamp
mapping, direct/borrowed buffer API, hot reconfiguration, drain/presentation
confirmation, queue telemetry UI, or Studio/network-service activation integration.
Those remain explicit extensions of the contract. Initial PortAudio/native
PipeWire and radio experiments are recorded separately; untested device/channel
combinations, full-rate continuity and physical coherence remain unqualified.

## Implementation verification, 2026-11-16

Observed host: Fedora 45, GCC 16.2.1, PortAudio v19 (Fedora 19.7.0-4),
PipeWire 1.6.11, SoapySDR 0.8.1. PortAudio development headers were installed
locally for this build. Automated tests used fake APIs/providers. Separately
authorized live experiments opened audio endpoints and radios; see the
[live report](HARDWARE_IO_LIVE_TESTS.md).

| Configuration | Result |
| --- | --- |
| Debug headless, all three backends explicitly ON, complete normal build | Passed |
| Hardware/audio/SDR, offline examples, plugin and installed-SDK checks plus existing rollback/rate-inference checks | 14/14 passed |
| ASan + UBSan + leak detection for the new modules and seven module/API/fake-driver tests | 7/7 passed; existing s7core linked from the installed SDK, not sanitizer-instrumented in this pass |
| Release with all three backends OFF and `PKG_CONFIG_EXECUTABLE=/bin/false` | Built; 7/7 profile/contract/SDK checks passed |
| Required PipeWire ON with pkg-config unavailable | Configuration failed with the expected missing-dependency diagnostic |
| Explicit PipeWire selection in the backend-disabled runner | Failed with the expected unavailable-backend diagnostic |
| Shell syntax and whitespace checks | Passed |

The normal build/check commands used the `/tmp/s7-hardware-build` build tree:

```sh
cmake -S . -B /tmp/s7-hardware-build -DS7_BUILD_STUDIO=OFF \
  -DS7_PORTAUDIO=ON -DS7_PIPEWIRE=ON -DS7_SOAPYSDR=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/s7-hardware-build --parallel 3
cmake --build /tmp/s7-hardware-build --target \
  s7hardware_smoke s7hardware_backend_signal_smoke s7hardware_plugin_smoke s7audio_smoke \
  s7audio_portaudio_smoke s7audio_pipewire_smoke s7sdr_smoke s7sdr_soapy_smoke \
  s7core_step_rollback_smoke s7core_rate_inference_smoke --parallel 3
ctest --test-dir /tmp/s7-hardware-build \
  -R 's7(hardware|audio|sdr)|s7core_(step_rollback|rate_inference)_smoke' --output-on-failure
cmake --install /tmp/s7-hardware-build --prefix /tmp/s7-hardware-sdk --component s7sdk
```

Native Windows/macOS execution and the updated AppDir packaging path remain
unqualified by this pass. Live measurements supplement automated checks without
extending their guarantees to all devices or full nominal-rate RF capture.
