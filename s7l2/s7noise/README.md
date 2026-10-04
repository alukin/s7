# S7 Noise Library (s7noise)

Standalone noise generator library for S7 runtime. Provides white and pink noise elements for testing and simulation.

## Overview

s7noise is a simple plugin library that:
- Compiles independently from s7core
- Loads dynamically at runtime via plugin system
- Provides two noise generator elements
- Uses standard C++ random number generation (no external dependencies)

## Elements

### WhiteNoiseGenerator
**Type**: Source (0 inputs, 1 output)

Generates white noise with normal (Gaussian) distribution.

**Parameters**:
- `amplitude` (real, default 1.0): Standard deviation of noise
- `seed` (int, default 0): Random seed (0 = use random device)

**Output**: Double-precision white noise values

**Example**:
```json
{
  "id": "noise",
  "type": "WhiteNoiseGenerator",
  "params": {
    "amplitude": 0.5,
    "seed": 42
  }
}
```

### PinkNoiseGenerator
**Type**: Source (0 inputs, 1 output)

Intended to generate pink (1/f) noise. The current implementation is a 7-sample moving average of white noise, which is low-pass filtered noise rather than true 1/f noise (see `docs/CODE_REVIEW_REQUIRED_FIXES.md`).

**Parameters**:
- `amplitude` (real, default 1.0): Standard deviation of noise
- `seed` (int, default 0): Random seed (0 = use random device)

**Output**: Double-precision pink noise values

**Example**:
```json
{
  "id": "pink",
  "type": "PinkNoiseGenerator",
  "params": {
    "amplitude": 0.5,
    "seed": 42
  }
}
```

## Building

### Prerequisites
- C++20 compiler
- s7core (shared library)
- CMake 3.20+

### Build Command
`s7noise` is built as part of the top-level build (`cmake --build build`); the library is `build/s7noise/libs7noise.so`. `cmake --install` places it in `lib/s7/plugins`.

### Installation
Installs to: `lib/s7/plugins/libs7noise.so` (Linux/macOS)

## Plugin Discovery

Studio loads it from the build tree automatically. For `s7runtime`, pass `--plugin-dir build/s7noise` or install it. The plugin is also loaded if found in:
1. `~/.s7/plugins/`
2. `/opt/s7/plugins/`
3. `./plugins/`
4. `$S7_PLUGIN_PATH` (environment variable)

## Schema Example

```json
{
  "name": "noise_test",
  "elements": [
    {
      "id": "white_noise",
      "type": "WhiteNoiseGenerator",
      "params": {
        "amplitude": 0.1,
        "seed": 12345
      }
    },
    {
      "id": "pink_noise",
      "type": "PinkNoiseGenerator",
      "params": {
        "amplitude": 0.1,
        "seed": 54321
      }
    }
  ],
  "connections": []
}
```

## Performance

| Operation | Time |
|-----------|------|
| White noise sample | ~0.5 μs |
| Pink noise sample | ~2 μs |
| Plugin load | ~5 ms (one-time) |

## Noise Characteristics

### White Noise
- Flat frequency spectrum
- Equal power at all frequencies
- Math: Independent Gaussian samples N(0, σ²)
- Use: General noise source, testing, flat spectrum

### Pink Noise (1/f)
- Target: power decreasing with frequency, -3 dB/octave
- Current implementation: 7-sample moving average of white noise (not yet 1/f)

## Seeding for Reproducibility

Set `seed` parameter for deterministic output:

```json
{
  "id": "white",
  "type": "WhiteNoiseGenerator",
  "params": {"amplitude": 1.0, "seed": 12345}
}
```

Same seed produces identical sequence in subsequent runs.

Leave `seed` as 0 (default) for non-deterministic noise.

## Integration with s7studio

s7studio can visualize noise output:
1. Display waveform plot
2. Display spectrogram/waterfall (with FFT plugin)
3. Show amplitude statistics
4. Real-time playback

## File Structure

```
s7noise/
├── CMakeLists.txt
├── README.md
├── include/s7noise/
│   └── noise_elements.hpp       (60 lines)
└── src/
    ├── noise_elements.cpp       (110 lines)
    └── plugin_noise.cpp         (120 lines)
```

## Algorithm Details

### White Noise
Uses `std::mt19937` with `std::normal_distribution<double>`.

### Pink Noise
Current implementation: keeps the last 7 white-noise samples in a ring buffer and outputs their mean. A true Voss-McCartney generator updates octave rows at different rates (row *k* every 2^k samples).

## Testing

### Basic Test
```json
{
  "name": "test_noise",
  "elements": [
    {"id": "gen", "type": "WhiteNoiseGenerator", "params": {"amplitude": 0.5}}
  ],
  "connections": []
}
```

Run simulation and observe output values:
- Should be normally distributed around 0
- Standard deviation should match amplitude parameter

### Spectrum Test (with s7spectral)
```json
{
  "name": "noise_spectrum",
  "elements": [
    {"id": "noise", "type": "WhiteNoiseGenerator", "params": {"amplitude": 1.0}},
    {"id": "fft", "type": "WindowedFFT", "params": {"fft_size": 2048}}
  ],
  "connections": [
    {"from_element": "noise", "from_port": 0, "to_element": "fft", "to_port": 0}
  ]
}
```

Observe:
- White noise: flat spectrum
- Pink noise: -3dB/octave slope

## References

- Voss-McCartney Algorithm: https://en.wikipedia.org/wiki/Pink_noise#Generation
- Normal Distribution: https://en.cppreference.com/w/cpp/numeric/random/normal_distribution
- Mersenne Twister: https://en.cppreference.com/w/cpp/numeric/random/mt19937

