# s7spectral

The reference **third-party plugin**: it is built out of tree against the installed S7 SDK, exactly as an external plugin would be, and is used to test that path. It is deliberately not part of the main build.

## Elements

| Element | Inputs | Outputs | Parameters (defaults) |
|---|---|---|---|
| `WindowedFFT` | `in` (DOUBLE) | `peak_frequency_hz`, `peak_amplitude` (DOUBLE) | `fft_size` 1024 (power of 2), `hop_size` 512, `sample_rate_hz` 44100, `window_type` HANN (RECTANGULAR, HANN, HAMMING, BLACKMAN, BLACKMAN_HARRIS, NUTTALL) |

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
