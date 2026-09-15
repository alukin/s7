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

Generates pink noise (1/f noise) using Voss-McCartney algorithm.

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
```bash
cd s7noise
mkdir build
cd build
cmake ..
make
make install
```

### Installation
Installs to: `lib/s7/plugins/libs7noise.so` (Linux/macOS)

## Plugin Discovery

The plugin is loaded automatically if found in:
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
- Power decreases with frequency
- More low-frequency content than white noise
- Algorithm: Voss-McCartney with 7-stage filter
- Use: Natural sounds, music, frequency sweep testing
- Spectral characteristics: -3dB/octave

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

### Pink Noise (1/f)
Voss-McCartney algorithm:
- Maintains 7 random values (one for each octave)
- On each step, updates one random value (round-robin)
- Sums all 7 values to produce output
- Produces -3dB/octave spectrum (true 1/f)
- Very efficient: 7 operations per sample

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

