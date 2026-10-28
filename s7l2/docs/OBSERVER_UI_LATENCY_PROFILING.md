# Observer UI latency: problem and profiling handoff

Date: 2026-10-28. Scope: S7 Studio 0.1.1 release qualification after the
recorded Scope analysis PR was merged. This document is a profiling task, not a
claim that the root cause is known or that simulation data is lost.

## Problem to answer

Under a heavy mixed observer workload, the Studio UI sometimes goes more than
250 ms between callbacks of a 10 ms timer on the GUI thread. Identify where
that time goes. In particular, determine whether one operation blocks the GUI
thread, several smaller operations accumulate, Qt Quick rendering waits, or
the host scheduler delays the thread. Obtain evidence before changing code.

The 250 ms figure is the proposed UI responsiveness target in
[`RELEASE_0.1.1.md`](RELEASE_0.1.1.md). It is not a simulation step deadline,
a data-integrity limit, or an automatic failure condition in the optional
benchmark. The callback gap is a proxy for UI responsiveness; it is not a
direct measurement of frame time or input latency. Qt timers can also fire
late when their event loop or the host is busy.

## Workload and existing evidence

The opt-in benchmark is [`s7studio/tests/observer_panel_benchmark.cpp`](../s7studio/tests/observer_panel_benchmark.cpp).
It publishes synthetic samples on a separate thread at a requested 1 kHz. It
selects 12 observers, permits four active dock previews, and in `mixed full`
mode opens four independent full viewers: Scope, Phase Portrait, 3D Trajectory,
and Live Oscilloscope. It scrolls the dock by 700 pixels about every five
seconds. Its GUI-thread `QTimer` is set to 10 ms and measures elapsed time
between callbacks with `QElapsedTimer`. The first 10 seconds are a hidden
baseline; the next 10 seconds warm the visible UI. The reported workload
starts after 20 seconds. This is **not** a `NodeRuntime` throughput test.

The latest recorded two-minute `mixed full` run is
`build-release/observer-artifacts/qual-rebased-120.json` (untracked artifact
on the original qualification host; not available on other hosts). It
recorded:

| Measure | Result |
|---|---:|
| Largest GUI timer gap | 271 ms |
| Gaps over 250 ms | 8 in 120 seconds |
| Achieved synthetic source rate | 1,000.007 steps/s |
| Captured samples | 5,465,187 |
| Capture errors | None |
| Peak process RSS | 968 MB |

Long gaps in that trace tend to follow dock scroll events, but not necessarily
in the same callback. For example, the scroll marker at 90,076 ms is followed
by a 271 ms gap ending at 90,663 ms. This is correlation, not proof that the
scroll call or any particular function takes 271 ms.

Useful comparisons in [`RELEASE_0.1.1.md`](RELEASE_0.1.1.md): the mixed
preview-only two-minute run peaked at 158 ms; a 60-second full-viewer-only run
peaked at 187 ms. Those runs differ in duration and configuration, so do not
subtract their maxima as if they were matched per-event costs. Before geometry
reduction, `mixed full` peaked at 458 ms and about 3 GB RSS. Scope and Phase
Portrait geometry reduction substantially lowered memory and isolated viewer
latency, but the mixed-workload gap persisted. In one diagnostic 60-second run,
an instrumented panel refresh stayed below 50 ms while the maximum timer gap
was 237 ms. Keeping hidden dock scenes alive, reducing dock refresh to 5 Hz,
and sharing a preview QML engine did not make the full two-minute run pass;
those experiments were removed. Thus neither one `ObserversPanel::refresh()`
call nor scene construction alone has been established as the cause.

All cited GUI measurements used Xvfb with software OpenGL at 2000x2600. They
do not establish behavior on a physical GPU, native Windows, or a real graph.
See [`STUDIO_GUI_TESTING.md`](STUDIO_GUI_TESTING.md) and
[`OPEN_PROBLEMS.md`](OPEN_PROBLEMS.md) for the release context.

## Profiling plan

1. **Reproduce on one fixed revision and environment.** Start from the merged
   `main`, record its commit, compiler/Qt versions, CPU/GPU, display backend,
   load, and the selected source/temp filesystem. Do not run concurrent builds
   or tests. Build the optional benchmark and run `mixed full` at least three
   times for 120 seconds. Retain each JSON and GUI log. Run `full-only` for the
   same duration as a matched configuration check. Compare the number and
   timestamps of gaps over 100 and 250 ms, not just the single maximum. If
   possible, repeat on a physical Linux desktop. A slow host or software
   renderer may be part of the result.

   ```bash
   cmake --build build-release --target s7studio_observer_panel_benchmark --parallel 2
   python3 tools/run_gui_test.py --artifacts build-release/observer-artifacts/profile-mixed \
     --capture build-release/s7studio/s7studio_gui_capture --screen-size 2000x2600 \
     --timeout 200 -- env TMPDIR=build-release/observer-artifacts \
     build-release/s7studio/s7studio_observer_panel_benchmark \
     build-release/observer-artifacts/profile-mixed.json 120 mixed full
   ```

   Change the artifact/output names per run. Use a disk-backed `TMPDIR` with
   sufficient space for the recording. For `full-only`, use `120 full-only`
   instead of `120 mixed full`. The benchmark's optional final viewer index
   `0` through `3` isolates each full viewer in `full-only` mode.

2. **Capture a timeline with a shared monotonic clock.** Add opt-in,
   low-overhead instrumentation to the benchmark or Studio test build. Log
   begin/end timestamps, thread ID, and duration for:

   - the benchmark's scroll-bar `setValue` call and the resulting scroll/layout
     or paint events;
   - [`ObserversPanel::refresh()`](../s7studio/src/observers_panel.cpp),
     separately for visibility checks, `Card::deactivate()`, presentation
     creation/deletion, result application, and `present()`;
   - [`ViewerManager::refreshViews()`](../s7studio/src/viewers/viewer_manager.cpp)
     and each full viewer's `refreshSource()`;
   - GUI-thread Qt Quick scene synchronization/rendering where a profiler can
     expose it, plus time spent waiting rather than executing;
   - the 10 ms timer callback's start/end and every gap above 100 ms.

   Buffer records in memory and write them after the run, or record only slow
   spans, so logging does not create the stalls. Correlate every long gap with
   work in its actual interval; a timer gap can contain several callbacks and
   paint events. A single fast `refresh()` does not exonerate its subsequent
   paint/render work.

3. **Take GUI-thread call-stack samples during long gaps.** Use a sampling
   profiler with thread stacks and CPU versus wait time, if available (for
   example Linux `perf`, Qt Creator Analyzer/QML Profiler, or an equivalent).
   `perf` was not installed on the original qualification host; check its
   presence and `kernel.perf_event_paranoid` (Ubuntu defaults to 4, which
   blocks unprivileged sampling) on each host. Capture enough repeated gaps to
   distinguish Qt Graphs/QQuickWidget rendering, widget layout/paint,
   allocation, disk I/O, synchronization, and host scheduling. Check whether
   the source/worker threads continue while the GUI thread is delayed.

4. **Isolate only after a stack or timeline points to a candidate.** Compare
   `mixed`, `mixed full`, `full-only`, and isolated full viewers. Add a
   benchmark-only no-scroll mode if needed; compare it with scrolling while
   keeping viewer count, duration, and renderer fixed. If preview activation
   appears involved, record which card enters/leaves the viewport and whether
   a QQuickWidget scene is created or retired. Avoid broad production changes
   based solely on the 250 ms threshold.

5. **Report the finding with a repeatable before/after measurement.** Name the
   function, Qt subsystem, or external wait responsible, supported by aligned
   timestamps and stack evidence. Record whether it is one long operation or
   accumulated work. If a fix is justified, keep it in `s7studio`, preserve
   exact recording/export data and runtime causality, and run the affected GUI
   workflows plus the same 120-second benchmark. Compare gap counts, maximum,
   source rate, capture status, and RSS under the same renderer and host. Also
   state what remains unknown if the result is environment-dependent.

Do not edit `external/` for this task without explicit maintainer approval.

## Findings, second host (2026-10-28)

Revision `e4297f7` plus benchmark-only instrumentation (`slow_ui_gaps`,
`clock_start_monotonic_ns`, `gui_thread_id` in the JSON). Host: AMD Ryzen 9
7940HS (8C/16T, up to 5.26 GHz), 59 GB RAM, Ubuntu, GCC 16.0.1, Qt 6.10.2,
Xvfb 2000x2600, Mesa 26.0.8 llvmpipe (software GL). This host is roughly 3–10x
faster than typical user machines, so a slower host was emulated by pinning
Xvfb and the benchmark to CPUs 0–3 (2 cores/4 threads) and capping all CPUs at
2.0 GHz. Artifacts are untracked under `build-release/observer-artifacts/`
(`profile-*`, `slow-*`). Every run sustained 1,000.007 source steps/s with no
capture error and at most four active dock plots.

| Run (120 s) | Max gap | Ticks | > 100 ms | > 250 ms | Peak RSS |
|---|---:|---:|---:|---:|---:|
| `mixed full`, unconstrained, 3 runs | 149–154 ms | 1,393–1,449 | 93–146 | 0 | 965–979 MB |
| `full-only`, unconstrained | 95 ms | 2,069 | 0 | 0 | 763 MB |
| `mixed full`, emulated slow host | 397 ms | 490 | 490 | 200 | 923 MB |
| `mixed full`, emulated, under `perf` | 487 ms | 451 | 451 | 289 | — |
| `full-only`, emulated slow host | 248 ms | 654 | 639 | 0 | 678 MB |

A 10 ms timer would tick about 12,000 times in 120 s. The GUI loop is
saturated on both hosts. On the fast host it averages about 85 ms per
iteration with `mixed full` (58 ms with `full-only`); 100–150 ms gaps appear
only once the dock previews are added. On the emulated slow host it averages
180–250 ms per iteration, even with only the four full viewers. That is a
throughput problem, not an isolated stall.

`perf record -k monotonic -g -e cpu-clock/period=500000/ -e
sched:sched_switch` on the benchmark and Xvfb processes (frame-pointer Release
build, emulated slow host, 120 s visible window):

- **CPU use by thread.** The four `llvmpipe` rasterizer threads used about 2.2
  of the 4 CPUs. The GUI thread used 0.43 CPU, Xvfb 0.08, and the analysis
  pool under 0.02.
- **GUI thread time.** On-CPU 44% of wall time, off-CPU 60% (the two overlap
  by up to 0.5 ms per interval because of the sampling period):
  - 26% blocked in `QRhi::endOffscreenFrame` from
    `QQuickWidget::timerEvent`, i.e. waiting for each `QQuickWidget`'s
    software-GL frame to complete. Single waits reached 77 ms.
  - 19% blocked in the top-level backing-store `rhiFlush`/`QRhi::endFrame`
    plus 7.8% in X server round trips (`XSync`, `XGetGeometry` via
    `makeCurrent`), i.e. compositing the widget textures and presenting to
    Xvfb.
  - 4.3% runnable but preempted. 1.8% in the Quick3D renderer's
    `QThreadPool::waitForDone`.
- **GUI thread CPU.** About 70% of the GUI thread's CPU time is under
  `QQuickWidget::timerEvent` (scene polish, sync and render). The Qt Graphs
  line stroke triangulation (`QQuickShapeCurveRenderer` /
  `QSGCurveProcessor::processStroke`) alone is about 22%. Code in the
  benchmark/Studio binary (observer presentation, `refresh()` and so on) is
  about 4% self time.
- **Scene churn.** Preview scene creation and teardown is visible but small in
  this run: `QQuickWidget::showEvent` 172 ms on-CPU, Quick3D shader-cache
  teardown 47 ms on-CPU plus 90 `fdatasync` waits totalling 400 ms (max
  18 ms). There were 121 short-lived `QQmlThread`s in 120 s.

Conclusion for this environment: the GUI-thread time is dominated by
synchronous Qt Quick rendering through `QQuickWidget` on software GL, mostly
waiting for llvmpipe and for presentation to the X server. It is not one long
operation and not S7 observer computation. Moving S7's own data work to worker
threads would therefore not remove the gaps measured here. Reducing rendered
frames or pixels, using cheaper line geometry, or rendering outside the GUI
thread would. None of these has been implemented or measured yet.

Remaining unknowns: the fast-host 100–150 ms post-scroll gaps were not
profiled. Emulation by core pinning and frequency capping does not reproduce a
slower machine's memory, cache or GPU. All results use Xvfb with software GL;
with a hardware GL driver the `endOffscreenFrame` and flush waits may shrink
substantially, which has not been measured. The `perf` run itself cost about
8% of timer ticks (451 versus 490).

### Per-plot frame rate and cost

With `S7_BENCHMARK_EVENT_PROFILE=1` the benchmark times every event the GUI
loop dispatches at the outermost level, grouped by receiver. It also counts,
for each plot `QQuickWidget`, renders whose `QXYSeries` emitted
`pointsReplaced` since that plot's previous render. Results are in
`gui_event_profile` (artifacts `events-*.json`):

| 120 s run | Loop rate | Plot renders | Window repaint+flush | Refresh timers | Other |
|---|---:|---:|---:|---:|---:|
| `mixed full`, unconstrained | 11.4/s | 62% | 28% | 7% | 4% |
| `mixed full`, emulated slow | 3.9/s | 61% | 29% | 5% | 5% |
| `full-only`, emulated slow | 5.5/s | 70% | 24% | 4% | 1% |

Percentages are shares of GUI-thread busy time, which was 100% of the window
in all three runs. "Refresh timers" covers `ViewerManager::refreshViews`,
`ObserversPanel::refresh` and the viewers' own timers, i.e. S7 data work on
the GUI thread.

- **Every visible plot renders once per loop iteration.** Each plot's frame
  rate equals the loop rate: about 11 fps unconstrained and 4–5.5 fps on the
  emulated slow host. Full viewers push data every 16 ms and dock previews
  every 100 ms, so neither can be reached. 80–100% of line-plot renders follow
  new series data, so these are mostly not redundant frames. The 3D plot is
  not covered by this counter.
- **A timer gap is roughly one iteration's total render work.** Plots and
  windows render in sequence within one pass, so the gap is about their summed
  cost. No single operation dominates: the longest single event was 140 ms
  (the 3D full viewer, slow host).
- **3D trajectory is the most expensive plot.** In the full viewer it costs
  14.6 ms per frame unconstrained and 71–83 ms on the emulated slow host:
  27% of GUI time in `mixed full` and 45% in `full-only`.
  `Trajectory3D.qml` requests `SceneEnvironment.MSAA` at `High` quality,
  which is costly in software rasterization. Its effect has not been
  measured separately.
- **Other costs.** Full Scope and Live line plots cost about 10–13 ms per frame
  unconstrained and about 22–24 ms on the slow host. Each top-level window's
  repaint and flush (`UpdateRequest`) costs 2.5–8 ms unconstrained and 7–25 ms
  slow, once per iteration per window.

Implication: a frame-rate cap only removes work when it is below the
achieved rate (about 11 fps here, 4 fps on the emulated slow host), and it
does not shorten an iteration in which every plot is dirty. Measures that do
shorten the gap: make each frame cheaper (3D MSAA, line geometry, window
size), stop all plots rendering in the same iteration (stagger or budget the
renders), or move rendering off the GUI thread. Each of these changes Studio's
rendering behaviour and needs a maintainer decision. None has been implemented
or measured yet.

### Change: paced plot rendering, 10 Hz status, cheaper 3D frames

Maintainer decisions (2026-10-28): dock previews need at most 10 fps, 3D
frames may be cheaper, plot renders should be spread across event-loop
passes, and status text should update at 10 Hz. Rendering outside the GUI
thread for heavy plots is to be designed separately (below).

- **Render pacing** (`viewers/quick_support.cpp`). Every plot `QQuickWidget`
  is an `ObservationQuickWidget`, and a shared `RenderPacer` gates its render
  timer. Qt 6.10's `QQuickWidget` renders from a 5 ms timer that it stops only
  in `timerEvent()`, so a withheld event returns 5 ms later.
  - A render's cost *d* runs from its start until a low-priority marker,
    posted behind the window repaint the render causes, is delivered. The
    next render then waits *d*/2.
  - Due widgets render in the order they became due. A widget that received
    input in the last 250 ms goes first.
  - Dock previews have a 100 ms minimum frame interval; full viewers have none.
  - Scene data is still applied immediately; only the frame waits.
- **10 Hz status** (`ui::Throttle` in `instrument_controls`). Status updates
  driven by the data refresh in the Live, Phase Portrait and 3D windows, and
  the Scope's auto-following analysis interval, run at most every 100 ms. The
  first change applies immediately and the latest state always lands.
  - Updates caused by user actions are not throttled, nor are changes in
    capture or display loss, nor changes of the displayed Live frame's run,
    trigger capture or trigger state.
  - Scope's Analyze applies a pending interval update before reading it.
- **3D**: 2x MSAA in the full viewer (was 4x), none in dock previews.

Same host, renderer and benchmark with `S7_BENCHMARK_EVENT_PROFILE=1`. All
runs: 1,000.007 source steps/s, no capture error, at most four dock plots.
"Before" is `events-*`, "after" is `final-*` (two `mixed full` runs
unconstrained):

| 120 s run | Max gap | > 100 ms | > 250 ms | Timer ticks | GUI busy | Full-viewer fps | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| `mixed full`, unconstrained, before | 156 ms | 175 | 0 | 1,363 | 100% | 9.8–11.3 | 999 MB |
| `mixed full`, unconstrained, after | 78–82 ms | 0 | 0 | 8,555–8,656 | 88–89% | 6.2–6.3 | 960–969 MB |
| `full-only`, unconstrained, after | 49 ms | 0 | 0 | 8,500 | 85% | 9.8 | 741 MB |
| `mixed full`, emulated slow, before | 394 ms | 463 | 261 | 463 | 100% | 3.6–3.8 | 939 MB |
| `mixed full`, emulated slow, after | 201 ms | 119 | 0 | 2,591 | 100% | 1.3 | 863 MB |
| `full-only`, emulated slow, before | 248 ms | 645 | 0 | 655 | 100% | 4.4–5.5 | 668 MB |
| `full-only`, emulated slow, after | 166 ms | 131 | 0 | 3,080 | 99% | 2.4 | 675 MB |

A 3D full-viewer frame on the emulated slow host fell from 71 ms to 43 ms.
Dock previews render at about 4 fps unconstrained and 1.1–1.4 fps on the slow
host, below their 10 fps cap, because they share the paced budget.

Trade-off: responsiveness improves sharply, but full viewers show fewer frames
per second. In particular, on the emulated slow host the four full viewers drop
to 1.3 fps with the dock active. The remaining GUI time is dominated by
top-level window repaint and flush (50% unconstrained, 63% slow). In a window
containing `QQuickWidget`s, every repaint re-composites all of its plot
textures and presents them to Xvfb, which is costly with software GL.

Tests: `ctest --test-dir build-release -R s7studio -j16`. The change fixed
three failures it had introduced: viewer loss text, Live trigger status and
Live spectrum readout under load. Remaining failures also fail on unmodified
`e4297f7`: `s7studio_scope_lifecycle` ("Saved capture loss was hidden"),
`Workflows::instrumentControls` and `Workflows::runToolbarPresentation`.
`s7studio_window_lifecycle` failed once under `-j16` and passed 5 runs alone
and 24 runs under 8-way concurrency on both builds; that failure is
unexplained.

### Rendering heavy plots outside the GUI thread (design note, not implemented)

`QQuickWidget` always renders on the GUI thread through `QQuickRenderControl`.
Moving rendering to another thread means hosting a `QQuickWindow` via
`QWidget::createWindowContainer()`. That window then gets the threaded render
loop, where the platform supports it; whether it does on xcb with Mesa
llvmpipe has not been checked. Expected effect and costs:

- The `endOffscreenFrame` wait (26% of GUI time on the slow host before this
  change) and the per-window composite/flush of plot textures would leave the
  GUI thread. Polish and sync, including Qt Graphs' `QQuickShape` stroke
  processing, still block the GUI thread.
- A native child window does not clip or stack like a widget inside a
  `QScrollArea`, so this suits full viewers, not dock previews. Wheel routing
  (`ObservationQuickWidget::wheelEvent`), focus, grab-based screenshots and
  GUI tests would need rework, and Windows behaviour must be checked natively.
- Suggested first step: prototype the 3D full viewer, the costliest plot, in a
  window container. Measure it with this benchmark on Xvfb and on a hardware
  GL desktop before deciding.
