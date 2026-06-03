# Quick Test Guide: Multi-Probe Oscilloscope

## 🚀 Ready to Test Right Now!

### Binary Location
```bash
/home/al/projects/s7/s7pp/cmake-build-debug-system/s7ui/s7ui
```

### Start Application
```bash
/home/al/projects/s7/s7pp/cmake-build-debug-system/s7ui/s7ui &
```

---

## What You'll See

### Before Running Simulation
```
Main Window Layout
├─ Left:     [Remote Models] [Schema Properties] tabs
├─ Center:   [Blank schematic canvas]
├─ Right:    [Node Palette] [Oscilloscope] tabs ← LOOK HERE!
└─ Bottom:   [Runtime Log]
```

The **Oscilloscope dock** (right side) shows an empty plot initially.

---

## Test Step-by-Step

### 1. Load a Schema with Oscillators
```
File → Open Schema...
```
Choose a schema with 2+ oscillators that feed into Scope Sink nodes.

Example structure:
```
Oscillator1 ─┐
             ├→ Scope Sink "Osc1"
             
Oscillator2 ─┐
             ├→ Scope Sink "Osc2"
```

### 2. Verify Oscilloscope is Visible
- Check right side for "Oscilloscope" dock
- Should show empty grid currently

### 3. Click "Run Model" Button
- This starts the simulation
- Compute thread writes to probes
- GUI thread reads from probes at ~30 Hz

### 4. Watch Three Things Happen
```
Runtime Log (bottom):
  [model] Running...
  [oscilloscope] Attached probe: Osc1
  [oscilloscope] Attached probe: Osc2
  [model] Completed successfully

Oscilloscope (right):
  Two colored traces appear
  Traces update smoothly
  Legend shows "Osc1" and "Osc2"

Time Axis:
  Shows 0 to 5 seconds
```

### 5. You Should See:
```
┌────────────────────────────────────┐
│    OSCILLOSCOPE                    │
├────────────────────────────────────┤
│                                    │
│  ▁▂▃▂▁  ▁▂▃▂▁  ▁▂▃▂▁  (Green)     │ Osc1
│  ▔▅▆▅▔  ▔▅▆▅▔  ▔▅▆▅▔         (Red) │ Osc2
│                                    │
│  ◻ Osc1    ◻ Osc2                │
│                                    │
│  t=0s ─────────────────→ 5s       │
│                                    │
└────────────────────────────────────┘

✅ Two sine waves, time-synchronized
✅ Different colors (green and red)
✅ Both oscillate at their frequencies
✅ Perfect visual correlation!
```

---

## What You're Watching

### Multi-Channel Oscilloscope = Real-Time Signal Comparison

**Before this integration:**
```
❌ Had to view one signal at a time in separate windows
❌ Impossible to see timing/frequency relationship
❌ Cluttered UI with many windows
```

**After this integration:**
```
✅ Both signals visible together
✅ Perfect time alignment (shared X-axis)  
✅ Clean, professional UI
✅ One display = full signal correlation
```

---

## Controls Available

### In the Oscilloscope Dock

```
Time Window:
  - Default: 5 seconds
  - Shows most recent 5s of data
  - Modify if you want to zoom in/out on time

Sample Rate:
  - Default: 1000 Hz (1 ms per sample)
  - Affects X-axis scaling
  - Should match your schema's dt or sample rate

Zoom/Pan:
  - Scroll to zoom
  - Drag to pan
  - (Via QCustomPlot interactions)

Export:
  - Right-click for options
  - Export to PDF
  - Print to printer
```

---

## Expected Behavior Per Action

### When Simulation Runs
```
Action: Click "Run Model"
Result:
  ✓ Oscilloscope dock becomes active
  ✓ Traces start appearing immediately
  ✓ Log shows probe attachment messages
  ✓ Time axis scrolls left as new data arrives
```

### When Pausing
```
Action: Click "Pause"
Result:
  ✓ Traces freeze
  ✓ Polling stops
  ✓ Can inspect waveforms at that moment
```

### When Resuming
```
Action: Click "Resume"
Result:
  ✓ Traces resume updating
  ✓ Time axis continues from where paused
```

### When Stopping
```
Action: Click "Stop"
Result:
  ✓ Computation stops
  ✓ Traces become stale (stop updating)
  ✓ Can still view last captured data
```

### When Resetting
```
Action: Click "Reset"
Result:
  ✓ Oscilloscope display clears
  ✓ All probes detached
  ✓ Ready for next run
  ✓ Log shows "cleared"
```

---

## Troubleshooting

### No Traces Appear After Running
```
✗ Problem: Empty oscilloscope display
? Possible Causes:
  1. Schema has no Scope Sink nodes
  2. Probes not written by compute thread
  3. Output signals are all zero
  
✓ Solutions:
  1. Check schema has "Scope Sink" nodes connected to outputs
  2. Verify oscillators are actually generating signals
  3. Check Runtime Log for "[oscilloscope] Attached probe" lines
```

### One Trace but Not the Other
```
✗ Problem: Only one probe appears

✓ Solutions:
  1. Check the second node is a Scope Sink
  2. Verify it has correct type (ScopeSink or ComplexScopeSink)
  3. Look in log: did it say "Attached probe: Osc2"?
  4. If not, probe may not exist in runtime
```

### Traces Look Wrong/Distorted
```
✗ Problem: Waveforms don't look like sine waves

✓ Solutions:
  1. Check if time window is too wide (zoom in)
  2. Verify sample rate matches oscillator frequency
  3. Check Y-axis auto-scaling hasn't clipped data
  4. Look at oscillator amplitude parameters
```

### High CPU Usage
```
✗ Problem: App feels sluggish

✓ Solutions:
  1. Reduce number of oscillators (fewer probes)
  2. Try removing complex probes (Real/Imag pairs are heavier)
  3. Reduce sample rate if possible
  4. Typical: 4 probes should use <5% CPU
```

---

## What Feels Good = Success!

### Green Lights ✅
- [ ] Oscilloscope dock visible on right
- [ ] Before run: empty grid displayed
- [ ] After run: traces appear immediately
- [ ] Multiple colors for different signals
- [ ] Legend shows signal names
- [ ] Time axis shows "t=0s" to "t=5s"
- [ ] Zooming/panning works smoothly
- [ ] Reset clears the display
- [ ] Log shows attachment messages
- [ ] No crashes or warnings

### "Wow That's Nice!" Moments 🌟
- [ ] See frequency relationships visually
- [ ] Can pause and inspect data
- [ ] Time alignment is perfect
- [ ] Colors are distinct and nice
- [ ] Updates at ~30 Hz (smooth, not jittery)
- [ ] Legend helps identify signals
- [ ] Everything feels responsive

---

## Testing Recommendations

### Test 1: Simple Dual-Oscillator (5 min)
```
Goal: Verify basic functionality

Setup:
  - 2 sine oscillators, different frequencies (1 Hz and 2 Hz)
  - Each connected to a Scope Sink
  - Run for 5 seconds

Verify:
  ✓ Two traces appear
  ✓ One oscillates twice per other
  ✓ No lag or jitter
  ✓ Legend correct
```

### Test 2: Phase Relationship (5 min)
```
Goal: Demonstrate time-alignment advantage

Setup:
  - 2 sine oscillators, SAME frequency
  - Phase offset: 90 degrees
  - Each connected to a Scope Sink
  - Run for 2-3 seconds

Verify:
  ✓ Same frequency but offset phase visible
  ✓ Shows exact 90° lag perfectly
  ✓ Great illustration of oscilloscope value!
```

### Test 3: Complex Signals (5 min)
```
Goal: Test complex probe support (if available)

Setup:
  - Any FFT or frequency-domain transform
  - Connected to Complex Scope Sink
  - Run with real oscillator input

Verify:
  ✓ Shows real and imaginary parts
  ✓ Real: solid line, Imaginary: dashed line
  ✓ Same color for parts of same channel
  ✓ Frequency spectrum visible
```

### Test 4: Stress Test (10 min)
```
Goal: Find limits and performance

Setup:
  - MAX oscillators reasonable (4-6)
  - Different frequencies
  - Run for 30+ seconds
  
Measure:
  - CPU usage stays reasonable (<10%?)
  - No memory leaks
  - No perceptible slowdown
  - Traces remain smooth
```

---

## Pro Tips 💡

### Tip 1: Adjust Time Window for Detail
```
Wide window (10s):     See overall trends
Narrow window (0.5s):  See fine detail
Try different values!
```

### Tip 2: Use Pause/Resume for Inspection
```
1. Run simulation
2. Press Pause at interesting moment
3. Zoom in and inspect waveforms
4. Press Resume to continue
```

### Tip 3: Export for Documentation
```
1. Get your oscilloscope showing perfect traces
2. Right-click → Export PDF
3. Include in reports/documentation
4. Looks professional!
```

### Tip 4: Compare Before/After Integration
Remember the old single-probe oscilloscope?
```
OLD: "[oscilloscope_1] oscillator_1"    (separate window)
     "[oscilloscope_2] oscillator_2"    (another window)
     ❌ Can't correlate timing

NEW:  "[oscilloscope] oscillator_1, 2, 3, ..."  (one window)
      ✅ Perfect time alignment!
```

---

## Share Your Thoughts!

After testing, answer:

1. **Visual Appeal:** Does it look professional? Colors good?
2. **Performance:** Is it smooth (no jitter/lag)?
3. **Usability:** Is it intuitive to use?
4. **Feature Gaps:** Missing anything obvious?
5. **Wow Factor:** Does it feel like a real oscilloscope?

---

## Summary

```
✅ Install: Already done!
✅ Compile: Already done!
✅ Integrate: Already done!

🚀 Ready to test with your oscillators!
```

**Go test it and tell me how it feels!**


