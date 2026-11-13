# s7wavelet

Qt-free wavelet processors and an out-of-tree element plugin, built against
an installed S7 SDK. Install the spectral headers first (the CWT uses its
portable radix-2 transform). No new external dependency is required.

```sh
cmake -S s7spectral -B /tmp/spectral -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build /tmp/spectral
cmake --install /tmp/spectral --prefix /path/to/sdk
cmake -S s7wavelet -B /tmp/wavelet -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build /tmp/wavelet
ctest --test-dir /tmp/wavelet --output-on-failure
```

Load `libs7wavelet` with the runtime/Studio plugin-directory mechanism.
`WaveletCoefficient` accepts F64 and emits C64 `coefficient`, F64 `valid`, and
F64 `delay_samples`. Use `ComplexToReIm` for current Studio scalar capture.
`frequency_hz` defaults to 25 and `cycles` (Morlet omega0) to 6. The element
adopts the graph rate. Frequency must be below Nyquist and cycles in [5,16].

The DC-corrected Morlet has support ±4 sigma and discrete L2 normalization
(sum of squared tap magnitudes equals one). Output is computed causally but
represents a center `delay_samples` earlier. `valid` becomes one only after a
full kernel has arrived. Coefficient magnitude is not calibrated PSD or
scale-independent tone amplitude. The element has direct feedthrough and
cannot break a feedback cycle merely because it owns history.

`analysis.hpp` provides centered, zero-padded CWT results with original complex
coefficients, times, frequencies and a full-support mask. Limits are 65,536
input samples, 128 frequencies, 65,535 taps per kernel, 64 MiB of kernels and
192 MiB of estimated job storage. Cancellation is checked between transforms
and FFT stages. Studio uses this processor in its bounded time-frequency/scalogram window.

The standalone test checks an independent long-double direct convolution and
causal-element alignment with the centered offline transform.
