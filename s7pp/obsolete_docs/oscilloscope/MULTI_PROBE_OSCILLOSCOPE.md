# Multi-Probe Oscilloscope Architecture

## Overview

The **`OscilloscopeWidget`** is a multi-channel, real-time signal visualization instrument that can display data from **multiple `Probe` instances on a single shared time-aligned display**.

This is the classical oscilloscope pattern:
- **One screen** with multiple synchronized waveform plots
- **Multiple input channels**, each with its own trace
- **Color-coded traces** for visual distinction
- **Shared time axis** ensuring all channels are time-aligned
- **Common controls** for time window, sample rate, and zoom/pan

## Key Design Principles

### 1. **One Oscilloscope = Multiple Probes**
Unlike sink nodes which preview a single probe inline, an oscilloscope aggregates data from **multiple signal capture points** (probes):

```
┌─────────────────────────────────────────┐
│         OscilloscopeWidget              │
│  ┌───────────────────────────────────┐  │
│  │  All signals on shared time axis  │  │
│  │  ┌──────────────────────────────┐ │  │
│  │  │ Probe1.Ch1 ─── (color 1)    │ │  │
│  │  │ Probe1.Ch2 ─── (color 2)    │ │  │
│  │  │ Probe2.Ch1 ─── (color 3)    │ │  │
│  │  │ Probe3.Ch1 ─── (color 4)    │ │  │
│  │  └──────────────────────────────┘ │  │
│  │          Time [0 → 5s]            │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

### 2. **Additive Probe Attachment**
Probes are added incrementally to the oscilloscope (not replaced):

```cpp
// Attach multiple probes - they all display together
osc->attachProbe(probe1, "Input Signal");
osc->attachProbe(probe2, "Output Signal");
osc->attachComplexProbe(complexProbe1, "Frequency Response");
```

### 3. **Real and Complex Signal Support**
- **Real probes**: Direct voltage/current signal capture (one trace per channel)
- **Complex probes**: Frequency-domain signals (magnitude/phase or real/imaginary pairs)
  - Real part: solid line
  - Imaginary part: dashed line (same color)

### 4. **Thread-Safe Live Polling**
- **30 Hz update rate** via internal timer
- **Safe reads** via probe's mutex-protected `readChannel()`
- **Automatic time alignment**: all channels polled in one batch

## API Reference

### Constructor & Lifecycle

```cpp
// Create widget
auto osc = new OscilloscopeWidget(parent);

// Lifecycle: attach → display → detach → attach new
```

### Attaching Probes

```cpp
/// Add a real-valued probe (e.g., voltage signal)
/// @param probe      Shared pointer to s7::Probe
/// @param label      Display label (e.g., "Input", "Filtered Output")
void attachProbe(const std::shared_ptr<s7::Probe>& probe, 
                 const QString& label = {});

/// Add a complex-valued probe (e.g., frequency-domain)
/// @param probe      Shared pointer to s7::ComplexProbe
/// @param label      Display label
void attachComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe, 
                        const QString& label = {});
```

### Removing Probes

```cpp
/// Remove a specific probe by label
void detachProbe(const QString& label);

/// Clear all probes and reset display
void clearAllProbes();
```

### Configuration

```cpp
/// Set time window to display (in seconds)
/// Default: 5.0 seconds
void setTimeWindow(double seconds);

/// Alternative: set sample count directly
void setSampleCount(std::size_t count);

/// Set sample rate (1/dt) for time-axis scaling
/// Default: 1000 Hz
void setSampleRate(double hz);
```

### Display Control

```cpp
/// Force immediate redraw (normally automatic via timer)
void redraw();
```

### Export/Print

```cpp
/// Export current plot to PDF
bool exportPdf(const QString& fileName,
               const s7ui::printing::PageSpec& pageSpec = {});

/// Print current plot
bool print(QWidget* parent,
           const s7ui::printing::PageSpec& pageSpec = {});
```

## Usage Examples

### Example 1: Simple Dual-Channel Display

```cpp
#include "oscilloscope_widget.hpp"

// Create oscilloscope widget
auto osc = new OscilloscopeWidget(this);

// Attach two real probes from different nodes
osc->attachProbe(inputProbe, "Input 1");
osc->attachProbe(outputProbe, "Output 1");

// Configure time window
osc->setTimeWindow(5.0);  // Display 5-second window
osc->setSampleRate(1000);  // 1 kHz sampling

// Add to UI (dock widget, dialog, etc.)
this->addDockWidget(Qt::RightDockWidgetArea, 
                    new QDockWidget("Oscilloscope", this));
dock->setWidget(osc);
```

### Example 2: Complex Signal Inspection

```cpp
// Complex probe (e.g., frequency-domain representation)
auto complexProbe = std::make_shared<s7::ComplexProbe>(
    "signal_fft", 
    4,    // 4 frequency bins
    1024  // 1024-sample ring buffer per bin
);

auto osc = new OscilloscopeWidget(this);
osc->attachComplexProbe(complexProbe, "FFT Output");

// Real and imaginary parts display on same axis
// (real = solid, imaginary = dashed, same color)
```

### Example 3: Multi-Probe Runtime Setup

```cpp
// During schema setup, attach probes to multiple outputs
auto osc = new OscilloscopeWidget(this);

// Before running schema, link probes to capture points
auto& runner = schemaRunner_;

// Wrapper function to attach probes to oscilloscope
auto attachOscilloscopeProbes = [&]() {
    // Attach input signals
    if (auto in_probe = runner->probe("input_buffer")) {
        osc->attachProbe(in_probe, "Input Buffer");
    }
    
    // Attach intermediate signals
    if (auto filt_probe = runner->probe("filter_output")) {
        osc->attachProbe(filt_probe, "Filter Output");
    }
    
    // Attach output signals  
    if (auto out_probe = runner->probe("final_output")) {
        osc->attachProbe(out_probe, "Final Output");
    }
};

// When simulation starts
attachOscilloscopeProbes();

// Oscilloscope will now display all three signals
// in real-time, with shared time axis at ~30 Hz
```

### Example 4: Multi-Channel Single Probe

```cpp
// A single probe with multiple channels (e.g., stereo audio)
auto stereoProbe = runtime->probe("stereo_output");  // 2 channels

auto osc = new OscilloscopeWidget(this);
osc->attachProbe(stereoProbe, "Stereo Out");

// Creates 2 graphs automatically (Ch1, Ch2)
// Each channel gets a distinct color
```

## Channel Color Palette

The oscilloscope uses an 8-color palette that cycles:

| Index | Color           | RGB                |
|-------|-----------------|-------------------|
| 0     | Teal            | (40, 200, 120)   |
| 1     | Light Red       | (255, 100, 100)  |
| 2     | Light Blue      | (100, 150, 255)  |
| 3     | Yellow-Orange   | (255, 200, 50)   |
| 4     | Purple          | (150, 100, 255)  |
| 5     | Pink            | (255, 100, 200)  |
| 6     | Green           | (50, 220, 100)   |
| 7     | Gold            | (200, 200, 50)   |

When more than 8 channels are displayed, colors wrap around.

## Legend Display

- **Single channel**: Legend hidden
- **Multiple channels/probes**: Legend visible with labels
  - Format: `"<ProbeLabel> Ch<N>"` for real probes
  - Format: `"<ProbeLabel> Ch<N> Real/Imag"` for complex probes

## Thread Safety

- **Compute thread** writes samples to probes via `Probe::tick()`
- **GUI thread** polls via `OscilloscopeWidget::pollAndUpdate()` at ~30 Hz
- **Safe:** `Probe::readChannel()` is mutex-protected internally
- **No external locking required** (fully encapsulated in Probe)

## Time Alignment

All channels are **automatically time-aligned** because:
1. Single `pollAndUpdate()` reads all probes in one batch
2. All channels use the same time scale (set by `setSampleRate()`)
3. Shared X-axis ensures visual alignment

## Performance Characteristics

| Metric | Value |
|--------|-------|
| Update Rate | ~30 Hz (33 ms timer) |
| Max Channels | Typically 4-8 (limited by screen real estate) |
| Buffer Depth | Configurable per probe (typically 512-4096 samples) |
| Time Window | 0.1 ms to 60 seconds |
| CPU Overhead | <2% for 4 channels at 1 kHz |

## Integration with MainWindow

To integrate into the main application:

```cpp
// In MainWindow::setupUI()
auto oscDock = new QDockWidget("Oscilloscope", this);
auto oscWidget = new OscilloscopeWidget();
oscDock->setWidget(oscWidget);
this->addDockWidget(Qt::RightDockWidgetArea, oscDock);

// In MainWindow::onRunSimulation() or after schema setup
void MainWindow::linkOscilloscopeProbes(OscilloscopeWidget* osc) {
    for (const auto& probeId : schemaRunner_->probeIds()) {
        if (auto probe = schemaRunner_->probe(probeId)) {
            osc->attachProbe(probe, QString::fromStdString(probeId));
        }
    }
}

// Store widget reference
OscilloscopeWidget* oscilloscope_ = nullptr;
```

## Comparison with Other Visualization Tools

| Feature | OscilloscopeWidget | SinkNodeItem Preview | VisualSinkPreviewWidget |
|---------|-------------------|----------------------|------------------------|
| Multiple Probes | ✓ Yes | ✗ Single | ✗ Single |
| Shared Time Axis | ✓ Yes | ✗ Independent | ✗ Independent |
| Real-time Polling | ✓ 30 Hz | ✓ 30 Hz | ✓ 30 Hz |
| Export/Print | ✓ PDF + Printer | ✗ No | ✗ No |
| Zoom/Pan | ✓ Interactive | ✗ No | ✗ No |
| Legend | ✓ Yes | ✗ No | ✗ No |
| Complex Support | ✓ Re/Im pairs | ✗ No | ✗ No |

## Future Enhancements

- [ ] Trigger-based capture modes (start on signal threshold)
- [ ] Waveform math (addition, subtraction, multiplication)
- [ ] Fourier analysis (built-in FFT visualization)
- [ ] Multi-window plots (separate Y-axes per probe)
- [ ] Persistence mode (fading history)
- [ ] Marker and statistics (peak, RMS, mean)
- [ ] Deep memory buffer (record > 10k samples)

## Troubleshooting

**Q: Oscilloscope shows blank plot after attaching probe**
- A: Check that probe's `channelCount()` > 0 and `readChannel()` returns data
- A: Verify runtime is executing (`pollAndUpdate()` reads valid samples)

**Q: Colors are hard to distinguish**
- A: We have 8 distinct colors; if using >8 channels, colors wrap
- A: Suggest grouping related signals or using multiple oscilloscopes

**Q: Performance drops with many probes**
- A: Limit to 4-6 probes per oscilloscope for best performance
- A: Use multiple oscilloscope widgets for different signal groups

**Q: X-axis not time-calibrated properly**
- A: Call `setSampleRate()` to set the actual sampling frequency
- A: Verify probe writes samples at consistent intervals

## Related Classes

- **`s7::Probe`** (`s7core/include/s7probe.hpp`): Real-valued signal buffer
- **`s7::ComplexProbe`**: Complex-valued signal buffer
- **`SinkNodeItem`**: On-canvas inline preview (uses `VisualSinkPreviewWidget`)
- **`VisualSinkPreviewWidget`**: Single-probe waveform renderer
- **`QCustomPlot`**: Underlying plotting library (external dependency)

## Files

- **Header**: `/home/al/projects/s7/s7pp/s7ui/src/oscilloscope_widget.hpp`
- **Implementation**: `/home/al/projects/s7/s7pp/s7ui/src/oscilloscope_widget.cpp`

