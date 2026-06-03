# S7 Probe & Oscilloscope Architecture - Visual Reference

**Status:** Active  
**Purpose:** System diagrams, data flow, and architecture visualization  
**Last Updated:** May 13, 2026

---

## System Overview Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         S7 Simulation Engine                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────┐     ┌─────────────┐     ┌──────────────────────────────┐  │
│  │ DigitalOsc  │────>│  Combine    │────>│  Probe (2 channels)          │  │
│  │ (pin 0)     │     │  Real/Imag  │     │  ├─ input[0] (Real)          │  │
│  └─────────────┘     │  (pins 0,1) │     │  ├─ input[1] (Imag)          │  │
│                      │             │     │  ├─ ringbuffer[0]: 8K×Cplx  │  │
│                      │  ToComplex  │     │  └─ ringbuffer[1]: 8K×Cplx  │  │
│                      └─────────────┘     └──────────────────────────────┘  │
│                                               (Stateful Sink Element)        │
│                                                                               │
│  [tick()] [tick()]   [tick()]              [tick()]                         │
│  Compute every 0.04s, accumulate probe samples in ring buffers              │
│                                                                               │
└──────────────────────────┬──────────────────────────────────────────────────┘
                           │  Thread-safe readChannel(ch, max_samples)
                           │  Lock (<1 µs), copy samples, unlock
                           │
┌──────────────────────────▼──────────────────────────────────────────────────┐
│                              GUI Layer (Qt)                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                       OscilloscopeWidget                               │ │
│  │  ┌──────────────────────────────────────────────────────────────────┐ │ │
│  │  │  QCustomPlot (2-graph display)                                   │ │ │
│  │  │  ┌─────────────────────────────────────────────────────────────┐│ │ │
│  │  │  │ Graph 0 (Teal, Re part)                                     ││ │ │
│  │  │  │ ↕↕↕↕↕ ↕↕↕↕↕ ↕↕↕↕↕                                          ││ │ │
│  │  │  │                                                            ││ │ │
│  │  │  │ Graph 1 (Teal dashed, Im part)                             ││ │ │
│  │  │  │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─                                       ││ │ │
│  │  │  │                                                            ││ │ │
│  │  │  └─────────────────────────────────────────────────────────────┘│ │ │
│  │  │  Pan/Zoom Interactions                                            │ │ │
│  │  └──────────────────────────────────────────────────────────────────┘ │ │
│  │                                                                        │ │
│  │  [QTimer ~30Hz] ──> pollAndUpdate() ──> readChannel() ──> updateGraphs() │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                               │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │  OscilloscopeWidget (Alternative: ComplexProbe variant)                │ │
│  │  [Same structure, 4 graphs for 2-ch complex: Re₀, Im₀, Re₁, Im₁]      │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Flow Diagram

### Real Probe Path

```
Compute Thread                   Probe Ring Buffer          GUI Thread
════════════════════════════════════════════════════════════════════════

Tick 0:  input[0] = 0.5
         input[1] = 0.3      ┌──────────────┐
         ├─────────────────> │ ringbuf[0]   │
                             │ [0, 1, 2...] │
                             │   ^write_pos │
         input[0] = 0.7      └──────────────┘
         input[1] = 0.4
         ├─────────────────> │ ringbuf[1]   │
                             │ [0, 1, 2...] │
                             │   ^write_pos │
         (repeat per tick)    └──────────────┘

                                              Timer fires every 33ms
                                              (GUI Thread)
                                              ├─ lock mutex
                                              ├─ copy 512 samples
                                              ├─ unlock
                                              └─ draw graphs

Time: ════════════════════════════════════════════════════════════════════════
      0ms    10ms   20ms   30ms   40ms   50ms   60ms   70ms   80ms   90ms
                             ↑                   ↑
                          Compute             GUI poll
                          loop
```

### Complex Probe Path

```
Compute Thread              ComplexProbe Ring Buffer        GUI Thread
═══════════════════════════════════════════════════════════════════════

Tick N:  input[0] = 0.5 + 0.3j
         ├─────────────────> │ ringbuf[0]   │
                             │ [...(0.5+0.3j)] │
                             │   ^write_pos   │
                             └────────────────┘

GUI Poll (readChannel):
                             │ ringbuf[0]     │
                             │ [...(0.5+0.3j)]│
                             │       ↑        │
                             └────────────────┘
                                     │
                                     └─> Extract Re: 0.5 → Graph 0
                                         Extract Im: 0.3 → Graph 1 (dashed)
                                         Draw both traces
```

---

## Class Hierarchy

```
QWidget
  └─ OscilloscopeWidget
      │
      ├─ members:
      │   ├─ QCustomPlot* plot_
      │   ├─ std::vector<QCPGraph*> graphs_
      │   ├─ QTimer* pollTimer_
      │   ├─ weak_ptr<Probe> probe_
      │   └─ weak_ptr<ComplexProbe> complexProbe_
      │
      ├─ public methods:
      │   ├─ attachProbe(shared_ptr<Probe>)
      │   ├─ attachComplexProbe(shared_ptr<ComplexProbe>)
      │   ├─ setTimeWindow(double)
      │   ├─ setSampleCount(size_t)
      │   ├─ clearProbe()
      │   └─ redraw()
      │
      └─ slots:
          ├─ pollAndUpdate() [30Hz]
          ├─ updateRealData()
          └─ updateComplexData()

Element (in s7core)
  └─ TypedProbe<T>
      │
      ├─ TypedProbe<Real>  [Probe]
      └─ TypedProbe<Complex>  [ComplexProbe]
```

---

## Channel Color Mapping

```
Probe(channels=8) → OscilloscopeWidget

┌─────────────────────────────────────────────────────────┐
│  Graph 0 ███████  Channel 0 (Teal)     #28C878          │
│  Graph 1 ███████  Channel 1 (Red)      #FF6464          │
│  Graph 2 ███████  Channel 2 (Blue)     #6496FF          │
│  Graph 3 ███████  Channel 3 (Orange)   #FFC832          │
│  Graph 4 ███████  Channel 4 (Purple)   #9664FF          │
│  Graph 5 ███████  Channel 5 (Pink)     #FF64C8          │
│  Graph 6 ███████  Channel 6 (Lt.Green) #32DC64          │
│  Graph 7 ███████  Channel 7 (Yellow)   #C8C832          │
│  Graph 8 ███████  Channel 8 (Teal)     #28C878 (repeat) │
└─────────────────────────────────────────────────────────┘

ComplexProbe(channels=3) → OscilloscopeWidget

┌──────────────────────────────────────────────────────────────┐
│  Graph 0 ───────  Channel 0 Real (Teal, solid)              │
│  Graph 1 - - - -  Channel 0 Imag (Teal, dashed)             │
│  Graph 2 ───────  Channel 1 Real (Red, solid)               │
│  Graph 3 - - - -  Channel 1 Imag (Red, dashed)              │
│  Graph 4 ───────  Channel 2 Real (Blue, solid)              │
│  Graph 5 - - - -  Channel 2 Imag (Blue, dashed)             │
└──────────────────────────────────────────────────────────────┘
```

---

## Memory Layout

### Probe Ring Buffer (Per Channel)

```
Probe object (8 channels × 8KB buffers)

Channel 0: ┌─────────────────────────┐
           │  data: [Real×8K]        │
ring_buf[] │  write_pos: (0..8191)   │  Circular overwrite
           │  count: (0..8192)       │
           └─────────────────────────┘

Channel 1: ┌─────────────────────────┐
           │  data: [Real×8K]        │
           │  write_pos: (0..8191)   │
           │  count: (0..8192)       │
           └─────────────────────────┘

...

Channel 7: ┌─────────────────────────┐
           │  data: [Real×8K]        │
           │  write_pos: (0..8191)   │  Thread-safe via
           │  count: (0..8192)       │  std::mutex
           └─────────────────────────┘

Total: ~64 KB per probe (8 channels × 8 KB)
```

### Widget Display Buffer (Per Channel)

```
OscilloscopeWidget graphs display

Graph 0: QVector<double> x;    (512 samples)
         QVector<double> y;    (512 samples)
         Size: ~8 KB per axis = 16 KB per graph

Graph 1: QVector<double> x;
         QVector<double> y;
         Size: 16 KB per graph

Total: N channels × 16 KB per channel~= <1 MB for typical 8-ch display
```

---

## Interaction Diagram (User Perspective)

### Real Probe Workflow

```
User clicks "Add Oscilloscope Device"
  │
  ├─> Select probe "debug_probe" from list
  │
  ├─> OscilloscopeWidget created
  │
  ├─> attachProbe(probe) called
  │   ├─> clearGraphs()
  │   ├─> addGraph() × N (2 channels)
  │   ├─> setPen() with colors
  │   ├─> start pollTimer
  │   └─> redraw()
  │
  ├─> Widget shows now
  │   ├─ Graph 0 (teal): Channel 0 signal
  │   └─ Graph 1 (red):  Channel 1 signal
  │
  ├─> User interacts
  │   ├─ Drag to pan
  │   ├─ Scroll to zoom
  │   └─ Data updates at 30 Hz
  │
  └─> User closes widget
      ├─> clearProbe()
      ├─> stop pollTimer
      └─> resources freed
```

### Complex Probe Workflow

```
User selects "debug_complex_probe" from device list
  │
  ├─> OscilloscopeWidget.attachComplexProbe(probe) called
  │   ├─> clearGraphs()
  │   ├─ For each channel (N=2):
  │   │  ├─ addGraph() for Real (solid line)
  │   │  └─ addGraph() for Imag (dashed line)
  │   ├─> setPen() with cycling colors
  │   ├─> start pollTimer
  │   └─> redraw()
  │
  ├─> Widget shows now
  │   ├─ Graph 0 ▬▬▬▬ (teal solid):   Channel 0 Real
  │   ├─ Graph 1 ----  (teal dashed): Channel 0 Imag
  │   ├─ Graph 2 ▬▬▬▬ (red solid):    Channel 1 Real
  │   └─ Graph 3 ----  (red dashed):  Channel 1 Imag
  │
  ├─> Legend shows all 4 traces
  │
  └─> Real-time update at 30 Hz
```

---

## Build System Integration

```
s7ui/CMakeLists.txt
├─ add_executable(s7ui ...)
├─   src/oscilloscope_widget.cpp     ← NEW
├─   src/oscilloscope_widget.hpp     ← NEW (AUTOMOC)
├─   src/main_window.cpp
├─   src/schema_session.cpp
│   ... other sources ...
│
├─ target_include_directories(s7ui PRIVATE
│   ${CMAKE_CURRENT_SOURCE_DIR}/src  ← finds oscilloscope_widget.hpp
│   ...
│ )
│
└─ set_target_properties(s7ui PROPERTIES
   AUTOMOC ON  ← processes Q_OBJECT macros
   ...
)
```

---

## Thread Safety Model

```
Compute Thread (s7::Schema::run())    GUI Thread (Qt Main Loop)
═════════════════════════════════════════════════════════════════

Loop every dt:
  1. schema->tick()
     ├─ element->tick() for all
     │  └─ Probe::tick()
     │     ├─ lock mutex               (GUI may be reading)
     │     ├─ write to ringbuf[ch]
     │     ├─ advance write_pos
     │     └─ unlock
     │
2. Continue
                                       QTimer fires every 33ms:
                                       ├─ OscilloscopeWidget::pollAndUpdate()
                                       │  ├─ Probe::readChannel()
                                       │  │  ├─ lock mutex (compute may writing)
                                       │  │  ├─ copy samples
                                       │  │  └─ unlock
                                       │  │
                                       │  └─ updateRealData()
                                       │     ├─ graph->setData()
                                       │     ├─ plot->replot()
                                       │     └─ show new waveform
                                       │
                                       └─ Continue
```

**Guarantees**:
- No data corruption (mutex protects riinbuf[])
- No UI stalls (lock <1 µs)
- Smooth display (continuous data flow)
- No frame drops (GUI already updated)

---

## API Surface

```cpp
OscilloscopeWidget
│
├─ Constructor
│  └─ OscilloscopeWidget(QWidget* parent = nullptr)
│
├─ Data Attachment
│  ├─ void attachProbe(shared_ptr<Probe>)
│  ├─ void attachComplexProbe(shared_ptr<ComplexProbe>)
│  └─ void clearProbe()
│
├─ Configuration
│  ├─ void setTimeWindow(double seconds)
│  ├─ void setSampleCount(size_t count)
│  └─ void redraw()
│
├─ Signals (Q_OBJECT)
│  └─ (internal polling only)
│
└─ Slots (internal)
   ├─ void pollAndUpdate() [timer callback, 30 Hz]
   ├─ void updateRealData()  [data display update]
   └─ void updateComplexData()  [complex data update]
```

**Total public API**: 6 methods (minimal, focused)

---

## Future Extension Points

```
OscilloscopeWidget (Current)
│
├─ [Phase 2] Wrap in device window
│  └─ OscilloscopeDevice (QMainWindow or QDockWidget)
│     ├─ Probe selector UI
│     ├─ Device controls
│     └─ Embedded OscilloscopeWidget
│
├─ [Phase 3] Add features
│  ├─ triggerMode(TriggerType type)
│  ├─ setYRange(double min, double max)
│  ├─ exportToCSV(QString filename)
│  ├─ exportToPNG(QString filename)
│  └─ measurements() -> MeasurementData
│
├─ [Phase 4] Advanced display
│  ├─ showFFT(bool enable)
│  ├─ overlay(shared_ptr<Probe> probe2)
│  ├─ annotation(QString label)
│  └─ cursorMode(CursorType type)
│
└─ [Phase 5] Integration
   ├─ Synchronize multiple scopes
   ├─ Remote probe access
   └─ Historical data playback
```

---

## Version History

**v1.0** (May 12, 2026)
- ✅ Multi-channel display (N-channel support)
- ✅ Real and Complex signal support
- ✅ 30 Hz polling, thread-safe access
- ✅ Color-coded traces, QCustomPlot rendering
- ✅ Interactive pan/zoom
- ✅ Clean API (6 public methods)
- ✅ Comprehensive documentation

**Future Versions** (Roadmap)
- v1.1 - Device window wrapper, GUI integration
- v1.2 - Trigger system, measurement tools
- v1.3 - Export functionality, multi-probe overlay
- v2.0 - Advanced features (FFT, historical analysis)

---

**Last Updated**: May 12, 2026  
**Diagram Version**: 1.0  
**Status**: Reference - for development and documentation

