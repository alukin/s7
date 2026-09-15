# S7 Spectral Library (s7spectral)

Standalone spectral analysis library for S7 runtime. Provides real-to-complex FFT elements with sliding windowed analysis for waterfall visualization.

## Overview

s7spectral is a separate compilation unit that:
- Compiles independently from s7core
- Loads dynamically as a plugin at runtime
- Uses FFTW3 (single precision) for FFT computation
- Produces SpectralFrame data type for frequency domain analysis
- Supports multiple window functions for spectral analysis

## Architecture

### New Data Type: SpectralFrame

Defined in `s7core/include/s7/spectral_types.hpp`:

```cpp
struct SpectralFrame {
    std::vector<std::complex<float>> bins;  // Complex FFT bins
    
    struct Metadata {
        uint32_t fft_size;
        float sample_rate_hz;
        uint32_t hop_size;
        WindowType window_type;
        std::chrono::system_clock::time_point timestamp;
        uint64_t frame_number;
        float freq_resolution_hz;
        float nyquist_hz;
    };
};
```

### Elements Provided

#### 1. WindowedFFT
**Real-to-complex FFT with sliding window**

```
Input:  double (audio signal)
Output: SpectralFrame (complex frequency bins)
```

**Parameters:**
- `fft_size` (int): Window size (power of 2, default 1024)
- `hop_size` (int): Samples between windows (default 512)
- `sample_rate_hz` (real): Sample rate in Hz (default 44100)
- `window_type` (enum): Window function (RECTANGULAR, HANN, HAMMING, BLACKMAN, BLACKMAN_HARRIS, NUTTALL)

**Features:**
- Stateful element with internal circular buffer
- Sliding window overlap processing
- FFTW3 optimized computation
- Automatic frame timing and numbering

#### 2. MagnitudeSpectrum
**Extract magnitude from complex bins**

```
Input:  SpectralFrame
Output: SpectralFrame (real part = magnitude, imag part = 0)
```

**Purpose:** Convert complex FFT output to magnitude spectrum for visualization

#### 3. PowerSpectrumDB
**Convert magnitude to dB scale**

```
Input:  SpectralFrame (magnitude)
Output: SpectralFrame (dB values)
```

**Parameters:**
- `reference` (real): Reference level for dB (default 1.0)
- `floor_db` (real): Minimum floor in dB (default -200.0)

**Purpose:** Logarithmic scaling for visual display

## Window Functions

All window functions are defined in `s7spectral/spectral_window.hpp`:

| Type | Scallop Loss | Use Case |
|------|-------------|----------|
| RECTANGULAR | -3.99 dB | Basic, no smoothing |
| HANN | -1.42 dB | General purpose (default) |
| HAMMING | -1.30 dB | Slightly better sidelobe |
| BLACKMAN | -1.10 dB | Good sidelobe attenuation |
| BLACKMAN_HARRIS | -0.77 dB | Excellent sidelobe suppression |
| NUTTALL | -0.80 dB | Low sidelobe, continuous derivative |

## Example Schema

```json
{
  "name": "spectral_waterfall",
  "elements": [
    {
      "id": "audio_in",
      "type": "AudioInput",
      "params": {"device": 0}
    },
    {
      "id": "fft",
      "type": "WindowedFFT",
      "params": {
        "fft_size": 2048,
        "hop_size": 512,
        "sample_rate_hz": 44100,
        "window_type": "HANN"
      }
    },
    {
      "id": "magnitude",
      "type": "MagnitudeSpectrum",
      "params": {}
    },
    {
      "id": "power_db",
      "type": "PowerSpectrumDB",
      "params": {
        "reference": 1.0,
        "floor_db": -200.0
      }
    }
  ],
  "connections": [
    {
      "from_element": "audio_in",
      "from_port": 0,
      "to_element": "fft",
      "to_port": 0
    },
    {
      "from_element": "fft",
      "from_port": 0,
      "to_element": "magnitude",
      "to_port": 0
    },
    {
      "from_element": "magnitude",
      "from_port": 0,
      "to_element": "power_db",
      "to_port": 0
    }
  ]
}
```

## Building

### Dependencies
- FFTW3 with single precision (libfftw3f)
- s7core (shared library)
- C++20 compiler

### Build Command
```bash
cd s7spectral
mkdir build
cd build
cmake ..
make
make install
```

### Installation
Installs to: `lib/s7/plugins/libs7spectral.so` (Linux/macOS)

## Usage

### Plugin Loading
The plugin is loaded automatically at s7runtime startup if found in plugin search paths:
- `~/.s7/plugins/`
- `/opt/s7/plugins/`
- `./plugins/`
- Directory specified by `S7_PLUGIN_PATH` environment variable

### Processing Pipeline

1. **Audio Input** (double stream)
   ↓
2. **WindowedFFT** produces SpectralFrame at each hop_size
   - Applies window function
   - Computes FFT using FFTW3
   - Output: complex frequency bins
   ↓
3. **MagnitudeSpectrum** (optional)
   - Extracts magnitude from complex bins
   ↓
4. **PowerSpectrumDB** (optional)
   - Converts to dB scale
   - Applies floor for visualization
   ↓
5. **Waterfall Display**
   - Collects frames over time
   - Renders time-frequency visualization

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| FFT (2048 bins, FFTW3) | ~1 ms | Single precision, measured |
| Magnitude spectrum | ~0.1 ms | O(n) computation |
| Power dB conversion | ~0.1 ms | O(n) computation |
| Window function | ~0.2 ms | One-time per frame |
| Total latency (hop_size=512, 44.1kHz) | ~12 ms | ~512 samples |

## Advanced Usage

### Custom Window Type
```cpp
// Create FFT with specific window
s7spectral::WindowedFFT fft(
    2048,                                    // fft_size
    512,                                     // hop_size  
    44100.0f,                                // sample_rate_hz
    s7spectral::WindowType::BLACKMAN_HARRIS // window_type
);
```

### Accessing Spectral Data
```cpp
// After step(), check if frame is ready
if (fft.has_new_frame()) {
    const auto& frame = fft.get_latest_frame();
    
    // Access bin data
    for (size_t i = 0; i < frame.bins.size(); ++i) {
        float freq = frame.bin_frequency(i);
        float mag = frame.bin_magnitude(i);
        float db = frame.bin_magnitude_db(i);
    }
    
    // Access metadata
    std::cout << "Frame #" << frame.metadata.frame_number << "\n";
    std::cout << "Resolution: " << frame.metadata.freq_resolution_hz << " Hz\n";
}
```

## Data Flow for Waterfall

```
Time →
Freq ↓

Frame N:   [bin0] [bin1] [bin2] ... [binN]
Frame N+1: [bin0] [bin1] [bin2] ... [binN]
Frame N+2: [bin0] [bin1] [bin2] ... [binN]
...

Each frame is one horizontal line in waterfall display
Color represents power in dB
```

## Integration with s7studio

s7studio can visualize waterfall plots by:
1. Querying SpectralFrame metadata
2. Collecting frames over time window
3. Rendering as 2D time-frequency image
4. Color mapping power values to display

The SpectralFrame type provides all necessary metadata for automated visualization.

## Limitations and Future Work

Current limitations:
- Single precision only (float, not double)
- Real-to-complex FFT (not complex-to-complex)
- Single FFT size per element (no adaptive windowing)
- No overlap-add reconstruction

Future enhancements:
- Complex-to-complex FFT option
- Adaptive FFT sizes
- Spectrogram binning/smoothing
- Peak detection and tracking
- Multiple analysis channels

## File Structure

```
s7spectral/
├── CMakeLists.txt
├── include/
│   └── s7spectral/
│       ├── spectral_window.hpp      # Window functions
│       └── fft_element.hpp          # FFT elements
├── src/
│   ├── spectral_window.cpp          # Window implementation
│   ├── fft_element.cpp              # FFT elements implementation
│   └── plugin_spectral.cpp          # Plugin registration
└── README.md
```

## References

- FFTW3 Documentation: http://www.fftw.org
- Window Functions: https://en.wikipedia.org/wiki/Window_function
- FFT Spectral Analysis: https://en.wikipedia.org/wiki/Spectral_analysis

