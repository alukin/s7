# s7spectral

The reference **third-party plugin**: it is built out of tree against the installed S7 SDK, exactly as an external plugin would be, and is used to test that path. It is deliberately not part of the main build.

## Elements

| Element | Inputs | Outputs | Parameters (defaults) |
|---|---|---|---|
| `WindowedFFT` | `in` (F64) | `peak_frequency_hz`, `peak_amplitude` (F64) | `fft_size` 1024 (power of 2), `hop_size` 512, `sample_rate_hz` 44100, `window_type` HANN (RECTANGULAR, HANN, HAMMING, BLACKMAN, BLACKMAN_HARRIS, NUTTALL) |

Every `hop_size` samples, once `fft_size` samples are buffered, `WindowedFFT` transforms the latest window (FFTW, single precision) and outputs the dominant frequency, refined by parabolic interpolation between bins, and the amplitude at the peak bin. The amplitude is subject to the window's scalloping loss (up to about 15% for HANN when the frequency falls between bins). Outputs that carry the whole spectrum wait for block/frame signal types in the runtime.

## Build

Requires FFTW3 single precision (`libfftw3-dev`, pkg-config module `fftw3f`).

```bash
cmake --install <s7-build> --component s7sdk --prefix <sdk>
cmake -S s7spectral -B <plugin-build> -DCMAKE_PREFIX_PATH=<sdk>
cmake --build <plugin-build>
./build/s7runtime/s7runtime --plugin-dir <plugin-build> ...
```

`ctest -R s7spectral_sdk_smoke` runs these steps in the build tree and checks the loaded element.

## Reusable analysis and Fourier coefficient

`analysis.hpp` now exposes Qt-free, bounded processors for a portable radix-2
complex transform, single-pulse frequency-response estimation and periodic-Hann
STFT amplitude coefficients. Complex results remain authoritative. Single-pulse
response divides by the actual pulse height, retains delay, and applies neither
one-sided doubling nor Hann taper. It requires zero pre-pulse output, exactly
one nonzero input sample and a finite paired record of at most 65,536 samples.
A finite record does not establish decay or LTI behaviour by itself.

`FourierCoefficient` is a rate-following scalar-input element with C64
`coefficient` and F64 `updated` outputs. Parameters are `frequency_hz` (10),
`window_time` (1 second), and `hop_time` (0.5 seconds). Durations must be exact
whole samples at the adopted graph rate. The rectangular-window coefficient
uses peak-tone normalization: 2/N except at DC/Nyquist, where it is 1/N. Phase
is relative to the first window sample. Results hold between updates; the
updated flag distinguishes a new result. Use explicit complex-component
adapters for Studio observation. This element has direct feedthrough.

The processor headers install with the package and are usable without loading
its plugin. `ctest --test-dir <plugin-build> --output-on-failure` checks an
independent DFT, pulse gain/delay, STFT amplitude and element update timing.
Studio uses these same processors in its impulse-response and time-frequency
windows. See `docs/STUDIO_ANALYSIS_TESTING.md` in the main project.
