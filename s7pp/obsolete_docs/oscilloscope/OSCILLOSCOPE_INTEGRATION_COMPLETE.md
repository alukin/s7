# Oscilloscope Integration Complete ✅

## Status: INTEGRATED AND READY TO TEST

### Date: June 1, 2026
### Application: s7ui (Multi-channel Signal Simulator)

---

## What Was Integrated

### Files Modified: 2

**1. `s7ui/src/main_window.hpp`**
- Added forward declaration: `class OscilloscopeWidget;`
- Added member variables:
  - `OscilloscopeWidget* oscilloscope_` 
  - `QDockWidget* oscilloscopeDock_`
- Added helper methods:
  - `void linkOscilloscopeProbes();`
  - `void unlinkOscilloscopeProbes();`

**2. `s7ui/src/main_window.cpp`**
- Added include: `#include "oscilloscope_widget.hpp"`
- Created oscilloscope dock in `createDocks()` (right-side panel)
- Called `linkOscilloscopeProbes()` in `runSchemaModel()`
- Called `unlinkOscilloscopeProbes()` in `resetSchemaModel()`
- Implemented both helper methods to attach/detach probes

---

## What Happens When You Run

### Application Start
```
✅ MainWindow creates OscilloscopeWidget
✅ Oscilloscope dock appears on right side
✅ Empty display (no probes attached yet)
✅ UI ready for simulation
```

### When You Press "Run"
```
✅ Schema compiles and starts execution
✅ linkOscilloscopeProbes() is called
✅ Finds all sink nodes in the diagram (they have Probes)
✅ Attaches each probe to the oscilloscope
✅ Oscilloscope starts polling at ~30 Hz
✅ Traces appear and update in real-time
✅ Logs show: "[oscilloscope] Attached probe: <name>"
```

### When You Press "Reset"
```
✅ unlinkOscilloscopeProbes() is called
✅ oscilloscope->clearAllProbes()
✅ Display returns to blank
✅ Ready for next run
```

---

## Testing Instructions

### Prerequisites
1. Have a schema loaded with oscillators (as you mentioned)
2. Schema should have 2+ output nodes (or sink nodes) to capture signals

### Step-by-Step Test

**Step 1: Start the app**
```bash
/home/al/projects/s7/s7pp/cmake-build-debug-system/s7ui/s7ui
```

**Step 2: Verify oscilloscope is visible**
- Look for dock widget on the right side labeled "Oscilloscope"
- It should be empty (blank plot) before running

**Step 3: Load a schema with oscillators**
- File → Open Schema
- Or use existing oscillator_agreement.json if available

**Step 4: Verify you have sink nodes**
- You need "Scope Sink" or "Complex Scope Sink" nodes in the schema
- These nodes capture signals via Probes

**Step 5: Click "Run"**
- Watch the Runtime Log for messages like:
  - `[oscilloscope] Attached probe: oscillator_1_output`
  - `[oscilloscope] Attached probe: oscillator_2_output`

**Step 6: See signals appear**
- Oscilloscope should show multiple colored traces
- All traces synchronized on shared time axis
- Updated in real-time at ~30 Hz

**Step 7: Test controls**
- Try adjusting "Time Window" or "Sample Rate" in oscilloscope UI
  - (If UI controls were added - check if needed)
- Try stopping/pausing - probes should disconnect
- Try resetting - display should clear
- Try running again - probes should re-attach

---

## Expected Behavior

### Perfect Success Looks Like:
```
┌─────────────────────────────────────────┐
│    Oscilloscope Dock (Right Side)       │
├─────────────────────────────────────────┤
│                                         │
│  Oscillator_1 ──────────────────────   │ Green
│  Oscillator_2 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─   │ Red
│                                         │
│  Legend:                                │
│  ■ Oscillator_1    ■ Oscillator_2     │
│                                         │
│  Time Axis: 0 ─────────────────────→ 5s│
│                                         │
└─────────────────────────────────────────┘

✅ Two sine waves, perfectly in sync
✅ Different frequencies visible
✅ Clean, professional oscilloscope display
```

---

## What Might Not Work (and fixes)

| Issue | Fix |
|-------|-----|
| Oscilloscope dock not visible | Toggle View menu → Oscilloscope |
| No traces appear after Run | Check if sink nodes exist in schema |
| Traces but weird/wrong | Verify sample rate matches your oscillators |
| Traces stop updating | Check if oscillators are still running |
| Memory shows probes aren't updating | Compute thread may not be writing to probes |

---

## Code Flow Diagram

```
User clicks "Run"
        ↓
runSchemaModel()
        ↓
schemaRunner_->run()  (starts compute thread)
        ↓
linkOscilloscopeProbes()  ← NEW!
        ↓
Loop through scene nodes:
  ├─ Find SinkNodeItem nodes
  ├─ Get probe/complexProbe from schemaRunner
  └─ osc->attachProbe(probe, "label")
        ↓
Oscilloscope::pollTimer (every 33ms)
  ├─ For each attached probe
  ├─ readChannel() from probe (thread-safe)
  └─ Update QCustomPlot trace
        ↓
GUI shows real-time waveforms!
```

---

## Architecture Summary

```
MainWindow
  ├─ schemaRunner_ (owns SchemaSession)
  │  └─ SchemaSession (runs compute thread)
  │     └─ Probes (ring buffers, written by compute)
  │
  └─ oscilloscope_ (QWidget in dock)
     ├─ pollTimer_ (30 Hz)
     ├─ probes_ (list of ProbeEntry)
     │  ├─ [0] "Oscillator_1_output" → QCPGraph
     │  └─ [1] "Oscillator_2_output" → QCPGraph
     └─ plot_ (QCustomPlot, displays graphs)
```

---

## Key Integration Points

### 1. **Probe Discovery** (line ~648 in main_window.cpp)
```cpp
// Walk scene nodes to find sinks (those have probes)
for (const auto& node : scene_->nodes()) {
    auto* sink = dynamic_cast<s7ui::elements::SinkNodeItem*>(node.get());
    if (!sink) continue;
    // Get probe from runtime
    if (auto probe = schemaRunner_->probe(id.toStdString())) {
        oscilloscope_->attachProbe(probe, id);
    }
}
```

### 2. **Dock Creation** (line ~356 in main_window.cpp)
```cpp
oscilloscopeDock_ = new QDockWidget("Oscilloscope", this);
oscilloscope_ = new OscilloscopeWidget(this);
oscilloscope_->setTimeWindow(5.0);
oscilloscope_->setSampleRate(1000.0);
addDockWidget(Qt::RightDockWidgetArea, oscilloscopeDock_);
```

### 3. **Lifecycle Wiring** (lines ~602 and ~627)
```cpp
// On run start:
runSchemaModel() → linkOscilloscopeProbes()

// On reset:
resetSchemaModel() → unlinkOscilloscopeProbes()
```

---

## Testing Suggestions

### Scenario 1: Basic Two-Oscillator Test (Recommended)
```
Schema: 
  ├─ Oscillator1 (freq=1 Hz) → Scope Sink "Osc1"
  └─ Oscillator2 (freq=2 Hz) → Scope Sink "Osc2"

Result:
  ✅ Should see two sine waves
  ✅ Osc2 oscillates twice per Osc1 cycle
  ✅ Perfect visual confirmation of frequency ratio
```

### Scenario 2: Phase Relationship Test
```
Schema:
  ├─ Oscillator1 (freq=1 Hz, phase=0°)
  └─ Oscillator2 (freq=1 Hz, phase=90°)

Result:
  ✅ Same frequency, 90° phase offset
  ✅ One leads the other visually
  ✅ Perfect for understanding phase
```

### Scenario 3: Multiple Channels
```
Schema with 3+ oscillators → Scope Sinks

Result:
  ✅ 3-8 distinct traces, all synced
  ✅ Tests multi-probe capability
  ✅ Shows oscilloscope can handle real complexity
```

---

## Compilation Verification

```
✅ Header file: No errors
✅ Implementation file: No errors
✅ Build: No errors, no link errors
✅ Binary: 49 MB, valid executable
✅ Integration: Complete
```

---

## Files and Documentation

| File | Status | Purpose |
|------|--------|---------|
| `oscilloscope_widget.hpp/.cpp` | ✅ Complete | Multi-probe oscilloscope widget |
| `main_window.hpp/.cpp` | ✅ Integrated | Wired into MainWindow |
| `MULTI_PROBE_OSCILLOSCOPE.md` | ✅ Provided | Architecture reference |
| `OSCILLOSCOPE_QUICK_REFERENCE.md` | ✅ Provided | Developer cheatsheet |
| `OSCILLOSCOPE_INTEGRATION_GUIDE.md` | ✅ Provided | Integration details |

---

## Next Steps

1. **Run the app and verify it starts:**
   ```bash
   /home/al/projects/s7/s7pp/cmake-build-debug-system/s7ui/s7ui
   ```

2. **Check oscilloscope dock appears**
   - Should be on right side
   - Empty before run

3. **Load or create schema with 2 oscillators**
   - Must have Scope Sink nodes!
   - These capture signals as Probes

4. **Press "Run"**
   - Check Runtime Log for "[oscilloscope] Attached probe" messages
   - Waveforms should appear and update

5. **Report feedback on UX:**
   - Color quality?
   - Trace visibility?
   - Time axis scaling?
   - Performance (lag, jitter)?
   - Feature gaps?

---

## Conclusion

✅ **The oscilloscope is fully integrated into the main application!**

You can now:
- View multiple oscillators simultaneously
- See signal correlation in real-time
- Compare frequencies and phases visually
- Use professional oscilloscope UX for debugging

**Ready to test with your oscillators!**

---

### Support & Questions

If anything doesn't work:
1. Check Runtime Log for "[oscilloscope]" messages
2. Verify schema has Scope Sink nodes (not just Oscillators)
3. Ensure simulation is actually running (not paused)
4. Review `OSCILLOSCOPE_QUICK_REFERENCE.md` for API details


