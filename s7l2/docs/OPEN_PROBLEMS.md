# S7 Open Problems

Status: living document of unsolved problems. Update items in place; remove
them when resolved (Git history keeps the record). Release scope and order are
in the [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) and the
[roadmap](RELEASE_ROADMAP.md); the longer backlog is in
[DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md).

## Live SDR and audio performance

The FM receiver at 672 kS/s sustains 48 kHz stereo on the tested LAN radios.
Further optimization remains open. Profile the device reads, scalar graph
scheduling, resampling and audio callbacks separately. Evaluate block-oriented
processing and overlapping capture with graph execution while preserving
causal ordering, bounded queues and explicit continuity reporting.

GNU Radio limits each scheduling call by available buffer space and a work
size cap, and its rational resampler uses a polyphase FIR; use these as
references when measuring scalar dispatch and resampling cost. Any batching
experiment must preserve sample order, filter state and the causal execution
semantics across block boundaries.
[GNU Radio block executor](https://github.com/gnuradio/gnuradio/blob/main/gnuradio-runtime/lib/block_executor.cc#L245),
[GNU Radio rational resampler](https://wiki.gnuradio.org/index.php/Rational_Resampler).

SDRangel's broadcast FM sink stages stereo frames before writing them to an
audio FIFO. S7 already has a bounded playback queue; measure whether changing
the transfer size at the device boundary reduces wakeups, and record queue
occupancy and short transfers alongside underruns.
[SDRangel broadcast FM sink](https://github.com/f4exb/sdrangel/blob/master/plugins/channelrx/demodbfm/bfmdemodsink.cpp#L208-L224).

Use the [FM receiver](../examples/sdr/FM_RECEIVER.md) for matched Studio and
headless runs on the same release build, radio and audio device: realized
rates, CPU time per second of audio, graph headroom, underruns, stop latency,
and queue occupancy over a longer run to expose RF/audio clock drift. See the
[performance plan](SIMULATION_OBSERVATION_PERFORMANCE_PLAN.md#wp5--hardware-bound-fixes-h2-h3-h4-h5).

## Studio

- **Mixed observer/full-viewer UI latency gate.** With 12 observers, four dock
  previews and four full viewers, the UI timer gap target (250 ms) was not met
  reliably on the original qualification host. Plot renders are now paced,
  dock previews capped at 10 fps and status text at 10 Hz; on a faster host
  the maximum gap fell to 82 ms and on an emulated slow host (4 CPUs, 2 GHz) to
  201 ms. The gate has not been re-run on the original host. These are
  software-rendered UI measurements, not runtime throughput.
- **Live observer rebinding.** Observation bindings are fixed per run. The
  maintainer wants observers to change sources during execution: a
  non-invasive subscription update at an acknowledged causal step boundary,
  with binding generations, continuity markers and stale-work rejection;
  decide how new taps appear without restarting the graph, how missing or
  incompatible sources are reported, and whether recording starts a new
  segment. This is observation reconfiguration, not live editing of the
  functional graph.
- **ST-012 — mouse-grab warnings under Wayland.** Launching `s7studio` from a
  terminal prints `This plugin supports grabbing the mouse only for popup
  windows` twice (from `libQt6WaylandClient`). No explicit grab call exists in
  Studio or vendored UI code; the originating widget is unknown and the report
  is not independently reproduced. Trace both call stacks under Wayland and fix
  the cause in project code; do not suppress Qt warnings.
- **ST-016 — low-contrast labels with a light application palette.** Some
  Live Oscilloscope labels (Position, Gain ×, timebase title, channel name,
  hover text) render dark on the dark instrument background with a light
  synthetic palette. Trace the child-label palette source; acceptance is
  readable contrast in both synthetic palettes and the desktop theme.
- **Bulk observation work is synchronous.** Projection, export and import of
  large recordings run on the UI thread and may wait for storage I/O. Measure
  the supported envelope (rate, duration, channels, disk use, UI latency) and
  move bulk operations off the UI thread without changing execution.
- **Preview limits page.** Studio's Help → Preview limits still shows
  [RELEASE_0.1.2_NOTES.md](RELEASE_0.1.2_NOTES.md); it needs a 0.2.0 version
  before the release.

## Validation and CI

- CI (`.github/workflows/ci.yml`) builds and tests headless Release on one
  compiler only. Not covered: a Studio build and the GUI suite, Debug and
  sanitizer builds (`-DS7_SANITIZE=...` exists), a second compiler (a GCC 16
  Release miscompile was found locally), the Windows cross build, the Python
  and Java node tests, and tests that exit with the skip code.
- Cross-language conformance is not enforced: the wire, frame and control
  fixtures exist in C++, Python and Java, but nothing requires the Python and
  Java checks to pass.
- No targeted concurrency audit or deterministic stress suite covers graph
  replacement, stream ownership, endpoint shutdown and the Studio worker/UI
  handoff; existing tests are smoke-level.
- `s7studio_window_lifecycle` (`closeAndDestruct(destruct-paused)`) fails under heavy host load (load average 30+ on 16 cores) and passes in isolation; diagnose the paused-worker shutdown timing before treating it as flaky.
- Two-node integration (`s7_two_node_deployment`) covers deployment, a restart
  gap and recovery; clean standalone shutdown and multi-cycle restart remain
  untested.

## Simulation and rate domains

- **Feedback across rate domains** is rejected. Allowing a loop through two
  resamplers needs analysis of causal memory at the rate boundary, of the
  latency the interpolation and anti-aliasing filters add inside the loop, and
  of stability.
- **All-or-nothing across rate domains.** A fault partway through a run step
  leaves domains that completed their samples advanced (ADR-021 holds per
  domain step; retry exactness per ADR-029). Decide whether whole-run-step
  atomicity is needed before relying on faults in multi-rate graphs.
- **Hardware clock domains (0.3.0).** Device rates differ from nominal and
  drift against UTC and each other: master clock choice, pacing a node by a
  device clock, ratio-tracking resampling, network audio latency/jitter/loss
  and start alignment. See [§8.4](../../doc/S7_Sample_Rate_Model.md#84-not-covered-hardware-clock-domains)
  and the [audio/compression research](../../doc/S7_Audio_and_Frame_Compression_Research.md).
- **Numerical methods.** The production integrators and their limits are in
  [INTEGRATORS.md](INTEGRATORS.md), [COUPLED_INTEGRATION.md](COUPLED_INTEGRATION.md)
  and the [validation matrix](NUMERICAL_VALIDATION_MATRIX.md). Open: stiff
  systems, long-run chaotic agreement beyond the documented budgets, the full
  nonlinear double pendulum and higher-order symplectic methods. Forward Euler
  on marginally stable systems drifts by design (step matrix modulus
  `sqrt(1+dt²)`); that is not a defect.

## Distributed execution

- **Cross-node feedback** ([ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027),
  [guide](DISTRIBUTED_CAUSAL_SCHEDULE.md)): real-loop qualification, broader
  fan-out, bounded resources, revision/failure behavior, multi-rate
  distributed graphs and coupled numerical regions across nodes remain open.
- Streams are not relayed through other nodes; relay identity, buffering,
  latency and failure propagation need specifying first.
- Over plain WebSocket, node identity is only a claim (`S7-Node` header); only
  mTLS deployments are authorized.
- No remote observation; it needs an explicit network/data contract that
  keeps observation non-invasive.

## Production readiness

- **No production credential lifecycle:** enrollment authority, certificate
  profiles and role OIDs (the control-node key purpose is a development
  placeholder, `2.25.…`), rotation/revocation, key protection, bootstrap trust
  and recovery after losing the control Studio are undefined. Test
  certificates are fixtures only.
- **No independent security review** of TLS setup, certificate parsing and
  authorization, plugin loading, file/path handling, canonical JSON/digest
  validation or deployment persistence.
- **No operational limits or runbook:** maximum graph/schema/library size,
  plugin trust policy, stream buffer sizing, state backup/restore, disk-full
  behavior, service supervision and upgrades are unspecified.
- **Crash recovery is out of scope so far:** a process crash faults its
  streams and loses unacknowledged in-memory data; `RECOVER_STREAM` continues
  without it. Decide whether durable recovery is ever in scope, and surface
  the limit consistently in CLI, Studio and docs.
- No snapshot/restore; element-state serialization needs a validation
  contract first (this also bounds exact retry, ADR-029).
- Windows binaries are cross-built and tested only under Wine, not on native
  Windows; a Windows node's clock is never treated as synchronized without
  `clock.assume_synchronized`.

## Video

The frame-oriented direction is in [VIDEO_IO_FRAME_DESIGN.md](VIDEO_IO_FRAME_DESIGN.md)
and the Full HD baseline in [VIDEO_IO_FULL_HD.md](VIDEO_IO_FULL_HD.md). Frame
ownership/publication, media session and transport contracts, native capture
providers and native Windows display remain open.

## Code structure

- Large multi-responsibility files: `s7studio/src/main_window.cpp`,
  `s7transport/src/transport.cpp`, `s7runtime/src/control_plane.cpp`,
  `s7runtime/src/runtime_service.cpp`, and the GUI test
  `s7studio/tests/gui/workflows_test.cpp`. Run probe planning in
  `MainWindow::currentRunProbes` is model logic without a headless test, and
  about 1000 lines of `s7studio/src/main.cpp` are self-check modes shipped in
  the product binary.
- Schema-backed element libraries are plain JSON; decide whether an archive
  format is still wanted before building one.
- `SOURCE_TIME` is carried and checked, but no element produces it yet.
