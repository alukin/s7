# Analysis elements inside causal graphs

These two optional examples require the spectral/wavelet plugins. Build them
using `tools/build_analysis_plugins.sh build-release`, then launch Studio with:

```sh
S7_PLUGIN_PATH="$PWD/build-release/analysis-plugins/install/lib/s7/plugins" ./build-release/s7studio/s7studio
```

Run **2 seconds at 1024 Hz** and open `scope`.

- `fourier_coefficient.s7diagram`: a 32 Hz unit sine, a 256-sample rectangular
  Fourier window and a 128-sample hop. Real coefficient is approximately zero,
  imaginary coefficient is −1. The update flag pulses after a full window and
  every hop; the coefficient holds between updates.
- `wavelet_coefficient.s7diagram`: a causal 32 Hz Morlet coefficient. Real and
  imaginary parts oscillate; full-support validity starts after the kernel
  fills. Center delay is explicitly 123 samples at these settings. L2-normalized
  magnitude is not calibrated sinusoidal amplitude or PSD.

Both split C64 explicitly through `ComplexToReIm` for real-valued observation.
The canonical headless inputs are `s7runtime/examples/schema_fourier_coefficient.json`
and `schema_wavelet_coefficient.json`; the installed-SDK smoke test executes
both JSON graphs. GUI gallery tests explicitly skip these examples if their
optional plugins are unavailable, and run them when the plugins are loaded.
