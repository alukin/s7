# Testing surfaces and control/time-frequency analysis

Build Studio and the tests, then launch the resulting application:

```sh
cmake --build build-release --target build-tests --parallel 6
./build-release/s7studio/s7studio
```

## Surfaces

Open any diagram in `examples/surfaces/` or `examples/complex_functions/`.
Run **5 seconds at 1 kHz**, then open `surface`. Try surface/wireframe/heatmap,
camera presets, contours, equal numeric aspect and fixed ranges. Real surfaces
also offer log10 height for positive cells; complex surfaces offer magnitude,
real/imaginary and log(1+|w|) heights with cyclic phase color.

Inspect a numeric row/column or hover the heatmap. Save `.s7analysis`, clear the
display, and open the saved result. Closing/reopening a viewer should replay its
original captured run. Clearing does not delete captured data. Missing cells
remain holes; zero complex values have undefined phase and gray color.

## Impulse responses and parameter surfaces

Open `examples/frequency_response/gain.s7diagram`, `delay.s7diagram`, or
`rc.s7diagram`. Run **2 seconds at 1 kHz** and open `response`. Its small completed
capture is analyzed automatically. Select frequencies and compare
Bode/Nyquist/Nichols/3D views; the blue analysis button also permits explicit analysis.
The Bode horizontal coordinate is explicitly log10(f / 1 Hz). Try the dB
reference and result save/open. Gain should be 2.5; three explicit Delays should
have unit magnitude and frequency-dependent phase.
The new input/output scopes show the same experiment in time. Phase defaults
to unwrapped; the three-sample delay reaches −540° at 500 Hz.

Enable the analytical second-order reference explicitly, set omega0, damping and DC gain,
and inspect its complex-plane landscape. The imaginary-axis path and selected
frequency link to the response plots. This is a separately declared continuous
reference; Studio does not infer it from an arbitrary diagram.

Generate a measured damping surface using nine fresh schema runs:

```sh
./build-release/s7runtime/s7analysis_lab /tmp/damping_surface.csv
```

Open a Real Surface window and use Open result on that CSV. Every constituent
run is checked against its independent discrete transfer formula and retained
in the metadata. X is frequency, Y is damping ratio and height is gain.

## Spectrograms and wavelets

Open `examples/time_frequency/tone.s7diagram`, `chirp.s7diagram`, or another
laboratory example. Run **8 seconds at 1024 Hz**, open `map`, and analyze with
STFT or Morlet CWT. The PLL copy retains its original rate and 6-second run.
These small completed captures auto-analyze on opening or completion. Larger
records show an explanation and a prominent blue **Analyze / freeze** action.
Time–frequency previews display computed STFT maps, rather than raw input
traces; response previews display measured gain. Completed small previews keep
the whole interval, so the impulse example remains visible at 1.5 seconds.

Try magnitude/squared magnitude/phase, STFT power/PSD, log frequency, 3D/wireframe, numeric
inspection, the input trace and frequency slice. Gray edge regions are excluded
from the trusted full-support map; their coefficients remain inspectable and
exportable with an explicit mask. STFT amplitudes and CWT L2 coefficients have
different normalization and must not be compared as calibrated PSD.
The display band initially ends at 100 Hz; raise **Display up to (Hz)** to see
higher-frequency STFT bins. This changes the map/slice only; saving retains
all coefficients. The input envelope preserves isolated pulses and extrema.

Save a result, reopen it and change transform settings to reanalyze its frozen
original input. Follow uses the latest 65,536 retained samples and can skip
intermediate windows. Hidden/minimized views cancel work. It must not change
plant execution or recorded values.

## Optional graph elements

Studio's viewers use the same Qt-free library processors directly. To use
`FourierCoefficient` or causal `WaveletCoefficient` inside a graph, build their
optional plugins against the installed SDK:

```sh
./tools/build_analysis_plugins.sh build-release
S7_PLUGIN_PATH="$PWD/build-release/analysis-plugins/install/lib/s7/plugins" ./build-release/s7studio/s7studio
```

Use `ComplexToReIm` for scalar observation of their C64 outputs. Fourier's update
flag distinguishes held results. Wavelet's delay output identifies the earlier
center represented by a current causal coefficient; validity requires full
support. These stateful direct-feedthrough elements do not break feedback cycles.

The packaging scripts include these optional modules when built through that
helper. The normal headless runtime and Studio builds do not add the out-of-tree
plugins as main-build subdirectories.
`./run_on_linux` also finds these plugins automatically when built for `build/`;
`./run_on_linux --release` uses the matching `build-release/` plugins. An
explicit `S7_PLUGIN_PATH` remains authoritative.

## Automated checks

Focused tests are `s7studio_surface`, `s7studio_response`,
`s7studio_time_frequency`, `s7core_grid_scan_smoke`, `s7analysis_damping_sweep`
and `s7spectral_sdk_smoke`. The SDK check also builds/loads the wavelet plugin and
runs both processor suites. The bundled GUI workflows run the example galleries
and both time-frequency methods through production windows. GUI checks remain
local; screenshots and reports are kept outside Git.

Qualification on 2026-11-13: the full native suite passed 112/112 tests;
the fresh headless checks passed 2/2; Windows cross-built viewer and headless
checks under Wine passed 5/5. Both optional plugins built and loaded on both
platforms. Actual Linux OpenGL example rendering was also checked. Native
Windows GPU rendering remains a manual check; Wine/offscreen results do not
qualify that rendering path.

The subsequent educational/usability review ran all 25 analysis-gallery
examples through production Studio windows: 27 Qt Test cases passed, with no
failures or skips. It checked actual drawing bounds and overlaps, all surface
presentations, both transforms, and the optional coefficient plugins. A final
five-example rendering run passed 7 cases; the full native regression suite
passed 112/112 in 363 seconds. After the last logarithmic-height mask correction,
focused checks passed 3/3 in Debug, 4/4 in Release and 3/3 under Wine.
The normal Debug launcher was also verified to load spectral, wavelet and noise
plugins. Render captures and reports remain outside Git under
`/tmp/s7-review-all` and `/tmp/s7-review-final-render`.

Automatic-analysis/preview qualification (2026-11-14): full native suite
112/112 passed; final focused Debug and Release checks each passed 3/3, and
Windows/Wine viewer checks passed 2/2. The rendered late-open/preview-cache,
chirp and RLC workflows passed 5 Qt Test cases without skips. The 8,192-sample
CWT check took approximately 0.63 s in Debug and 0.17 s in Release on this
machine, including presentation; these measurements are not timing guarantees.
Local evidence is in `/tmp/s7-auto-analysis-full-tests.log`,
`/tmp/s7-auto-analysis-final-gui`, and the corresponding focused test logs.

Preview pagination regression: run `time_frequency/impulse.s7diagram` for
8 seconds at both 1024 and 4096 Hz. The completed preview must retain the event
at 1.5 seconds, including after hide/show. The latter capture crosses the
source's 16,384-record page limit. Boundary checks cover 16,384, 16,385, 32,768
and 65,536 records, plus multi-channel response captures. Analysis lifecycle
checks retain a valid plot through queued/failed work, recover the action on
Input trace, freeze the latest samples, and protect input-less imports from
retired jobs. The GUI workflow checks rendered pixels around the impulse in
the compact map as well as data bounds and completed-result cache reuse.

Pagination/lifecycle qualification (2026-11-14): full native suite passed
112/112 in 359 seconds. After the final heatmap downscaling adjustment, focused
Debug checks passed 3/3, Windows/Wine checks passed 2/2, and the rendered impulse
workflow passed 4 Qt Test cases without failures or skips. Evidence is retained
in `/tmp/s7-impulse-full-tests.log`, `/tmp/s7-impulse-final-debug-tests.log`,
`/tmp/s7-impulse-resample-wine-tests.log` and `/tmp/s7-impulse-render-final`.
