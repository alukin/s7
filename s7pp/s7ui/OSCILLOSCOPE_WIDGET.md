# Oscilloscope Widget

## Overview

The `OscilloscopeWidget` is a multi-channel real-time waveform display component for visualizing probe data from the S7 simulation engine.

## Architecture

### Concept: Probes as Oscilloscope Inputs

```
S7 Schema
├── DigitalOscillator
│   └── output → Probe (2 channels: Re, Im)  ← Captured here
│                  ├── Channel 0: Real part
│                  └── Channel 1: Imaginary part
│
GUI Application
├── OscilloscopeWidget #1
│   └── attachProbe(probe) → Displays 2 channels in real-time
```

**Key Principle**: A **Probe** in the schema is an N-channel data sink. When displayed in the GUI, it becomes an **Oscilloscope device** with N inputs (probes).

### Features

- **Multi-Channel Display**: Renders N simultaneous signal traces
- **Color-Coded Traces**: Each channel has a distinct color (8 colors in palette)
- **Real & Complex Support**:
  - Real probes: Display each channel as-is
  - Complex probes: Display Re/Im pairs (solid/dashed lines)
- **Live Data Polling**: ~30 Hz update rate from probe ring buffers
- **Interactive**: Pan/zoom via mouse (QCustomPlot)
- **Thread-Safe**: GUI thread reads from compute thread's probe buffers

## API Reference

### Creating and Using

```cpp
#include "oscilloscope_widget.hpp"
#include <s7probe.hpp>

// Create widget
auto oscilloscope = new OscilloscopeWidget(parent);

// Attach a real probe (N channels)
std::shared_ptr<s7::Probe> probe = /* from schema */;
oscilloscope->attachProbe(probe);

// Or attach a complex probe
std::shared_ptr<s7::ComplexProbe> complexProbe = /* from schema */;
oscilloscope->attachComplexProbe(complexProbe);

// Configure display
oscilloscope->setTimeWindow(10.0);  // 10 seconds
oscilloscope->setSampleCount(512);  // or fixed sample count

// Retrieve from schema session
auto probe = schemaSession->probe("probe_id");
oscilloscope->attachProbe(probe);
```

### Widget Configuration

#### View Window

Control what time period or how many samples to display:

```cpp
// Time-based window (samples scaled to match window)
oscilloscope->setTimeWindow(5.0);  // Show 5 seconds

// Sample-based window (fixed number of samples)
oscilloscope->setSampleCount(1024);  // Show 1024 samples
```

#### Manual Redraw

```cpp
// Force immediate update (normally automatic via timer)
oscilloscope->redraw();
```

#### Clear Display

```cpp
// Remove attached probe and reset
oscilloscope->clearProbe();
```

## Channel Color Palette

Channels are rendered with distinct colors:

| Channel | Color | Hex |
|---------|-------|-----|
| 0 | Teal | #28C878 |
| 1 | Red | #FF6464 |
| 2 | Blue | #6496FF |
| 3 | Orange | #FFC832 |
| 4 | Purple | #9664FF |
| 5 | Pink | #FF64C8 |
| 6 | Light Green | #32DC64 |
| 7 | Yellow | #C8C832 |
| 8+ | (cycles through palette) | - |

## Real vs Complex Display

### Real Probes

```
Probe (4 channels) → OscilloscopeWidget
                    ├─ Graph 0: Channel 0 (teal)
                    ├─ Graph 1: Channel 1 (red)
                    ├─ Graph 2: Channel 2 (blue)
                    └─ Graph 3: Channel 3 (orange)
```

### Complex Probes

```
ComplexProbe (2 channels) → OscilloscopeWidget
                            ├─ Graph 0: Channel 0 Real (teal, solid)
                            ├─ Graph 1: Channel 0 Imag (teal, dashed)
                            ├─ Graph 2: Channel 1 Real (red, solid)
                            └─ Graph 3: Channel 1 Imag (red, dashed)
```

## Integration with S7 Schema

### Schema Session Integration

```cpp
// In SchemaSession, after building schema:
auto probe = schema_->element("probe_1");
auto probePtr = std::dynamic_pointer_cast<s7::Probe>(probe);

// In MainWindow:
oscilloscope->attachProbe(probePtr);
```

### Visual Sink Nodes → Probes

GUI nodes marked as "visual sinks" or "oscilloscopes" are converted to Probe elements:

```json
{
  "id": "oscope_1",
  "type": "ScopeSink",     // GUI node type
  "params": {
    "channels": 2
  }
  // Converted to:
  // "type": "Probe",
  // "params": {"channels": 2}
}
```

## Performance Considerations

### Buffer Management

- `Probe::readChannel(ch, maxSamples)` returns the most recent N samples
- Widget requests up to `sampleCount_` samples per update
- Probe ring buffer has capacity (default 8192 samples)
- Circular overwrite: when full, oldest entries are replaced

### Update Rate

- Polling interval: 33 ms (~30 Hz refresh)
- Safe for multiple widgets polling the same probe (no contention)
- Sub-millisecond lock time in `readChannel()`

### Memory

- Per oscilloscope: ~1 KB widget overhead + QCustomPlot resources
- Per graph: ~100 KB for 512 samples (2 doubles per sample point)
- Typical multi-channel setup: <1 MB total

## Example Use Cases

### Case 1: Single Probe, Multi-Channel Display

**Schema:**
```json
{
  "type": "Vco",
  "id": "vco1",
  "params": {"base_hz": 440}
}
→
{
  "type": "ToComplex",
  "inputs": [{"from": "vco1", "output": 0}, ...],
  "id": "complex_converter"
}
→
{
  "type": "Probe",
  "id": "debug_probe",
  "channels": 2,
  "inputs": [{"from": "complex_converter", "output": 0, "pin": 0}, ...]
}
```

**GUI:**
```cpp
auto probe = session->probe("debug_probe");
auto osc = new OscilloscopeWidget();
osc->attachComplexProbe(
    std::dynamic_pointer_cast<s7::ComplexProbe>(probe));
```

**Result**: Real-time display of VCO output (real & imaginary components)

### Case 2: Multi-Probe Monitoring

Multiple oscilloscope widgets, each observing different probes:

```cpp
auto oscVco = new OscilloscopeWidget(tab1);
oscVco->attachProbe(session->probe("vco_probe"));

auto oscFiltered = new OscilloscopeWidget(tab2);
oscFiltered->attachProbe(session->probe("filtered_probe"));

// Each widget independently polls its probe at 30 Hz
```

## Thread Safety

- **Write**: Compute thread calls `Probe::tick()` which updates ring buffers
- **Read**: GUI thread calls `Probe::readChannel()` which locks mutex
- **Contention**: Minimal (~<1 µs locks in typical use)

No synchronization issues if multiple widgets read the same probe.

## Future Enhancements

1. **Trigger Modes**: Edge/level detection, auto-trigger
2. **FFT Display**: Transform-domain visualization
3. **Measurements**: Automatic peak, RMS, frequency detection
4. **Export**: Save/print waveform as PNG or CSV
5. **Time Markers**: Zoom, scroll, annotation tools
6. **Multi-Probe Display**: Show two probes overlay for comparison

---

**API Version**: 1.0  
**Last Updated**: May 12, 2026  
**Status**: Active Development

