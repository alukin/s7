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
`rc.s7diagram`. Run **2 seconds at 1 kHz** and open `response`. Click Analyze
captured impulse, select frequencies, and compare Bode/Nyquist/Nichols/3D views.
The Bode horizontal coordinate is explicitly log10(f / 1 Hz). Try the dB
reference and result save/open. Gain should be 2.5; three explicit Delays should
have unit magnitude and frequency-dependent phase.

Enable the analytical second-order reference explicitly, set omega0 and damping,
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

Try magnitude/squared magnitude/phase, STFT power/PSD, log frequency, 3D/wireframe, numeric
inspection, the input trace and frequency slice. Gray edge regions are excluded
from the trusted full-support map; their coefficients remain inspectable and
exportable with an explicit mask. STFT amplitudes and CWT L2 coefficients have
different normalization and must not be compared as calibrated PSD.

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
