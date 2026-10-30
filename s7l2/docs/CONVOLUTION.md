# Complex frame convolution in s7core

`s7::FirConvolution` and `s7::FftConvolution` are reusable processors for
fixed-size frames of `std::complex<double>` samples. They have the same
constructor, `step()`, `reset()`, and output-size semantics. The impulse response
is fixed for the lifetime of a processor; create a new instance to change it.

```cpp
#include "s7/convolution.hpp"

std::vector<std::complex<double>> impulse = /* taps */;
s7::FftConvolution convolution{1024, impulse, s7::ConvolutionMode::ContinuousStream};
std::span<const std::complex<double>> output = convolution.step(input_frame);
```

| Mode | Input per call | Output per call | State across calls |
|---|---|---|---|
| `IndependentFrames` (default) | Exactly `N` samples | Full linear convolution, `N + M - 1` samples | None |
| `ContinuousStream` | Exactly `N` consecutive samples | `N` samples aligned with the input frame | Last `M - 1` input samples |

`N` is the configured frame size and `M` is the impulse length. Both must be
positive. An input span of another size throws `std::invalid_argument`.
`reset()` clears stream history and the current output. The returned span is
owned by the processor and remains valid until its next `step()`, `reset()`, or
destruction. A single instance must not be used concurrently; separate
instances can run on separate workers. Keep input frames in storage separate
from the same processor's output buffer.

The FIR processor evaluates the direct complex sum. The FFT processor uses a
portable radix-2 FFT, precomputes the impulse spectrum, and uses overlap-save
for continuous streams. It zero-pads each transform to the next power of two
at least `N + M - 1` and normalizes the inverse transform. `step()` allocates
no memory in either processor. FFT results differ slightly from direct FIR
because floating-point operations occur in a different order. Choose the
algorithm based on measured frame and impulse sizes; the FFT has setup and
transform overhead and is not always faster.

These are core processing components, not registered graph elements. S7 ports
currently carry scalar samples, including complex scalars; they do not carry
frames. A graph element for this operation needs a frame signal type with
shape, ownership, observation, and rollback rules. The existing
`Element::step()` interface can remain unchanged. See
[runtime performance architecture](RUNTIME_PERFORMANCE_ARCHITECTURE.md) for
the graph design and buffered-convolution benchmarks.

Verification:

```bash
cmake --build build --target s7core_convolution_smoke
ctest --test-dir build -R s7core_convolution_smoke --output-on-failure
```

The smoke test compares both processors with direct complex convolution for
independent and consecutive frames, including impulses longer than frames,
reset behavior, a delayed matched-filter peak, and invalid sizes.
