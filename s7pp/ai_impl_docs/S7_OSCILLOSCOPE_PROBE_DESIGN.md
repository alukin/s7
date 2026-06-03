# S7 Oscilloscope and Probe Architecture

**Status:** Active  
**Purpose:** Design intent and architectural overview  
**Last Updated:** May 13, 2026

---

## Overview

An **Oscilloscope** is a multi-channel data visualization device. A **Probe** is an input connection to the oscilloscope. This architecture treats the oscilloscope as the primary widget and probes as its inputs.

## Current State Analysis

### Probe Architecture (Already Complete ✓)

The `TypedProbe<T>` class in `s7probe.hpp` already implements N-channel data capture:
- **N Input Channels**: Creates N typed inputs (Real or Complex)
- **N Ring Buffers**: Circular overwrite semantics (oscilloscope-like)
- **Thread-Safe Reads**: Separate GUI/compute thread access
- **API**: `readChannel(ch, maxSamples)` for GUI polling

**Example**: A Probe with 4 channels has 4 inputs and 4 independent ring buffers.

### Current Naming Issues

| Context | Current Name | Target Name | Status |
|---------|--------------|-------------|--------|
| Widget class | `OscilloscopeSinkWidget` | `OscilloscopeWidget` | ❌ Needs rename |
| Widget file | `oscilloscope_sink_widget.*` | `oscilloscope_widget.*` | ❌ Needs rename |
| Schema element | `Probe` (name is good) | `Probe` | ✓ Keep as-is |
| Complex variant | `ComplexProbe` | `ComplexProbe` | ✓ Keep as-is |
| GUI node type | `ScopeSink` / `ComplexScopeSink` | `Oscilloscope` / `ComplexOscilloscope` | ⚠️ Consider rename |
| GUI item class | `SinkNodeItem` | `OscilloscopeNodeItem` (or keep generic) | ⚠️ Optional |

## Design: "Probes Are Inputs to Oscilloscope"

### Conceptual Model

```
Schema "Experiment"
├── Source (DigitalOscillator)
├── Filter (FIRFilter)
├── Probe "probe_filtered"     ← Captures filtered signal
│   └── 2 inputs (Re/Im)
└── Probe "probe_debug"        ← Another probe for debugging
    └── 4 inputs (multi-channel)

GUI "Experiment"
├── Oscilloscope Device #1     ← Displays probe_filtered
│   └── Connected to: probe_filtered
├── Oscilloscope Device #2     ← Displays probe_debug
│   └── Connected to: probe_debug
```

### Widget Features

The refactored `OscilloscopeWidget` should:

1. **Display Multiple Channels**
   - Each channel from the probe is a separate graph
   - Layout: stacked vertically or grid layout
   - Color-coded traces

2. **Real-Time Data Streaming**
   - Poll probe data at ~30 Hz via QTimer
   - Update graphs incrementally
   - Circular buffer scrolling (time-window sliding)

3. **Configuration per Channel**
   - Input count: Determined by connected probe
   - Scale: Auto or manual per channel
   - Trigger: Optional (future)

4. **Interaction**
   - Pan/zoom on time axis
   - Legend showing channel info
   - Double-click to open detailed view

## Implementation Plan

### Phase 1: Rename and Refactor Widget (Immediate)

- [ ] Rename `oscilloscope_sink_widget.hpp` → `oscilloscope_widget.hpp`
- [ ] Rename `oscilloscope_sink_widget.cpp` → `oscilloscope_widget.cpp`
- [ ] Rename class `OscilloscopeSinkWidget` → `OscilloscopeWidget`
- [ ] Update CMakeLists.txt to reference new filenames
- [ ] Update all includes throughout the codebase

### Phase 2: Extend Widget for Multi-Channel Display

- [ ] Refactor widget to accept channel count parameter
- [ ] Create layout manager for N channels (grid or stacked)
- [ ] Use QCustomPlot to display N graphs
- [ ] Apply distinct colors per channel
- [ ] Implement synchronized time-axis scaling

### Phase 3: Integrate with Probe Data

- [ ] Accept Probe reference in widget
- [ ] Implement probe data polling loop
- [ ] Memory-efficient ring buffer handling (don't copy all data every frame)
- [ ] Handle Real vs Complex probe variants

### Phase 4: GUI Node and Device Integration

- [ ] Consider renaming `SinkNodeItem` for clarity (or keep as utility class)
- [ ] Document "visual sink" nodes as "Oscilloscope devices"
- [ ] Ensure schema properly maps visual sink nodes to Probe elements
- [ ] Add device palette entry for "Add Oscilloscope"

### Phase 5: Complex Probe Support

- [ ] Display complex channels as Re/Im pair
- [ ] Option to show magnitude/phase instead
- [ ] Apply appropriate scaling for complex waveforms

## Code Structure After Refactor

```
s7ui/
├── src/
│   ├── oscilloscope_widget.hpp       ← New (was .._sink_widget.hpp)
│   ├── oscilloscope_widget.cpp
│   ├── elements/
│   │   ├── sink_node_item.hpp        ← Unchanged (generic GUI node)
│   │   └── sink_node_item.cpp
│   ├── main_window.cpp               ← Update include references
│   └── schema_session.*              ← Unchanged (already uses Probe/ComplexProbe)
├── CMakeLists.txt                    ← Update source file list
└── resources/
    └── ...
```

## Terminology Guide

After refactoring:

| Term | Meaning | Example |
|------|---------|---------|
| **Probe** | Data-capture sink element with N inputs | `Probe` with 2 channels for Re/Im |
| **Oscilloscope** | GUI visualization device displaying probe data | Widget showing 4 channels of real data |
| **Channel** | One input/output of a probe or display | Re and Im parts of complex signal |
| **Visual Sink Node** | Graph item in schematic representing an oscilloscope | "scope_1" node in the schema editor |
| **Device** | User-facing concept in GUI | "Add Oscilloscope Device" from palette |

## Backward Compatibility Notes

- Netlist format: No changes (Probe/ComplexProbe element names unchanged)
- JSON schema: No changes (element types stay the same)
- API: `Probe::readChannel()` unchanged
- Breaking change: File names and class names in s7ui (requires code update only)

## Future Enhancements

1. **Trigger Logic**: Detect specific signal conditions (rising edge, threshold)
2. **FFT Display**: Transform-domain visualization option
3. **Multi-Oscilloscope Correlation**: Display two probes' data together
4. **Export**: Save visible waveform to CSV/image
5. **Measurement Tools**: Automatic peak, frequency, RMS detection

---

**Status**: Design document created May 12, 2026.  
**Next Step**: Implement Phase 1 (rename and refactor).

