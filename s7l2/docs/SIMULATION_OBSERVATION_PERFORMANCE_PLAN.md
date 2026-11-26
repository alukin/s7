# Simulation and observation performance plan

Status: decisions D1–D5 taken 2026-11-25; implemented on branch `perf/observation-runtime-hardware` (see Results). It follows the 2026-11-25 review of
simulation correctness/performance and hardware-bound elements. Security
findings from the same review are tracked separately in
[SECURITY_TODO.md](SECURITY_TODO.md). Line numbers refer to commit `dfbecd9`.

## Goal

A run must produce the same model results, and achieve nearly the same step
rate, whether or not probes, the Scope, the Live Oscilloscope or other viewers
are open. This is the existing contract in
[the observation architecture](../../doc/S7_Observation_Architecture.md):
observation "must not significantly degrade execution performance" and should
use buffering, decimation and rate limiting. The current Studio implementation
does not meet it.

## Results (2026-11-25)

Measured on the 7940HS development host, Release build. Baseline is commit
`05f61fc` (before WP1–WP6); "branch" is the implementation. User machines are
slower, so the slow profile matters more than the fast one.

**Implemented:**
- **WP0:** load benchmark and matrix runner.
- **WP1:** inlined fixed-size copies, three-slot memory staging (5 → 3 copies
  per memory output per step), and branch-free snapshots of 8-byte direct
  outputs.
- **WP2:** compiled probes.
- **WP3:** block ring, distributor thread and the D1 policies, with the Run
  Configuration option.
- **WP4 (partial):** batched recording and live appends. The Scope preview
  mailbox was removed (D6, 2026-11-26).
- **WP5:** H2/H5 (ADR-028) and H4 (Linux semaphore waits).
- **WP6:** per-domain sample counters.
- **V1:** extreme-preserving 3D trajectory reduction.
- **D5:** inexact-retry reporting (ADR-029).

**Runtime step cost** (`s7core_indexed_benchmark`, one pinned core, median of
5 interleaved runs, identical checksums):

| case | baseline ns/step | branch ns/step | change |
|---|---|---|---|
| gain64 | 358 | 181 | −49% |
| gain512 | 2,790 | 1,619 | −42% |
| wide512 | 2,656 | 1,293 | −51% |
| feedback | 40.0 | 21.1 | −47% |
| trapezoid | 164 | 138 | −16% |
| pendulum | 299 | 135 | −55% |
| nested | 97.8 | 48.2 | −51% |

**Probe and viewer load** (`tools/run_observation_benchmark.py`, 1M ticks,
64-gain chain or Euler oscillator, median of 3). Runs with probes were 5.7–8.3×
faster on both profiles, and every recording was complete (lossless).
Observation share of run time, baseline → branch:

| profile | 4 probes | 8 probes |
|---|---|---|
| fast | 76–80% → 8–18% | 87–88% → 28–38% |
| slow (0.6 CPU) | 90–91% → 58–68% | 94% → 73–79% |

**Not met:** WP3's acceptance gate of ≤ 5% observation share.
- **What remains is recording work itself:** copying 4–8 values per step into
  the journal and the live buffers.
- **Why the slow profile is worst:** it gives the stepping and distributor
  threads 0.6 of one CPU in total, so in lossless mode every sample's delivery
  cost subtracts from stepping.
- **The largest avoidable part was** the per-sample `ScopeSample` built for a
  preview the Studio UI did not read. D6 removed it (results below).
- **Viewer call times:** the 60–90 ms maxima on the slow profile come from
  cgroup quota throttling (present in the baseline too), not lock contention.

**D6 measured (2026-11-26): Scope preview removed.** Same matrix, branch
before → after D6 (median of 3, fresh processes):

| profile | steps/s gain | observation share before → after |
|---|---|---|
| fast | 1.05–1.38× | 10–36% → 8–15% |
| slow (0.6 CPU) | 1.34–1.68× | 57–77% → 41–67% |

The gate is still not met; the rest is the lossless journal and live-buffer
work. With 4 probes on the fast profile the share was already near its floor,
so the gain there is small.

**D2 re-measured (2026-11-26): ADR-021 stays.** The same commit was built
with and without the direct-output snapshot loop and the builds were run
alternately (`s7core/benchmarks/compare_indexed.py`, 2,000,000 steps, median of
11, identical checksums). Snapshot share of step time, fast / slow profile:

| Graph | Per step, with → without (fast) | Fast | Slow |
|---|---|---|---|
| `gain512` | 1,619 → 1,334 ns | 17.6% | 17.5% |
| `gain64` | 184 → 158 ns | 14.3% | 14.9% |
| `pendulum` | 134 → 123 ns | 8.6% | 6.7% |
| `nested` | 49 → 45 ns | 8.0% | (throttling noise) |
| `trapezoid` | 139 → 137 ns | 1.5% | 1.2% |

The snapshot costs about 0.55 ns per 8-byte direct output per step. It is a
large share only when elements do almost nothing (gain chains). The slow
profile caps CPU time but does not slow the CPU, so it gives the same shares.

**Deferred WP4 items:** separate reader file handle; envelope sort outside
the live lock. The stepping thread no longer takes these locks, so they now
delay only the distributor.

## Findings this plan addresses

Measured findings used the headless Release build and
`s7core_indexed_benchmark`, pinned to one core of the 7940HS development host.
User machines are 3–10× slower, so treat the absolute numbers as optimistic.
Findings marked *traced* come from reading the code, without a measurement yet.

| ID | Finding | Evidence |
|---|---|---|
| O1 | Each per-step probe read through `ObservationService::sample_output` costs ~47 ns: two id-string hash lookups, an id string copy, and `Element::outputs()` allocating a `std::vector`. A cached pointer costs ~0. | measured with a scratch benchmark; `s7observe/src/observe.cpp:34–50`, `s7core/include/s7/element.hpp` `outputs()` |
| O2 | On every step, for every probe, Studio's stepping thread takes the recording mutex, the scope mailbox mutex and the live acquisition mutex. It also copies the probe-name string into a `ScopeSample`. | traced; `s7studio/src/local_schema_runner.cpp:295–321` |
| O3 | Viewers hold the same mutexes while doing slow work: `ObservationSource::read` seeks and reads up to 16,384 samples from the spill file under the lock. `LiveAcquisition::timeSnapshot` (60 Hz Live Oscilloscope) builds and sorts the whole envelope under the lock. `setTimeSpan` re-buckets history under it, and `tail` copies up to 4,096 positions per channel. The stepping thread waits for all of it. | traced; `observation/observation_source.cpp:127–157`, `observation/live_acquisition.cpp:188–250`, polls at `scope_view_window.cpp:576`, `live_oscilloscope_window.cpp:506` |
| O4 | After 20,000 samples the recording spills to a file. From then on every sample is one `QFile::write`, and every viewer read moves the shared file position. The next append must seek back, which also flushes the write buffer. Open viewers therefore make recording writes effectively unbuffered. | traced; `observation_source.cpp:81–87`, `memoryLimit = 20'000` |
| O5 | `ScopeSampleMailbox::take` moves up to 200,000 samples, each with its own string, while holding the mutex that `push` needs. | traced; `scope_sample_mailbox.hpp:56–66` |
| P1 | Copying outputs for publish/stage/rollback costs more than the elements' own arithmetic: ~36% of `gain512` and ~40% of `pendulum` step time is `memmove`/`memcpy@plt`, against ~23% and ~15% for the model's own arithmetic. | measured with `perf`; `s7core/src/runtime.cpp:491–546` |
| P2 | In multi-rate graphs whose rates are not whole numbers of Hz, `samples_through` runs a 256-bit division for each domain on every step. | traced; `s7core/src/resampler.cpp:91` |
| V1 | The 3D trajectory viewer reduces points by plain stride subsampling (every Nth point), so a short excursion between kept points disappears from the display. The Scope (per-pixel-column min/max with gaps) and the Phase Portrait (per-bucket x/y extrema with gaps) preserve extremes. | traced; `s7studio/src/viewers/trajectory_geometry.cpp:90–97`; compare `scope_view_window.cpp:60–97`, `phase_portrait_view_window.cpp:44–73` |
| R1 | A fault inside a Resampler's output rate domain cannot be retried exactly. The Resampler steps first in that domain, advancing its private input position (`step_++`) and discarding consumed samples. When a later element of the same domain step faults, rollback restores only published outputs (ADR-021), so the retry asks for the next input position and throws "Resampler read an input sample it has not received". This contradicts ADR-022's "a retry continues exactly". Faults in the Resampler's input domain retry exactly. | **confirmed** with a fault-injection test on the pre-change runtime (found while writing WP1 tests); `s7core/include/s7/resampler.hpp:115–135`, `doc/S7_Architectural_Decisions.md` ADR-022 |
| H1 | Device-paced runs call the O1/O2 path once per sample (672 kS/s for the FM receiver), on the thread that must keep up with the device. Observation load can therefore cause a capture overflow and end the run. | traced from O1/O2; `local_schema_runner.cpp:276–285` |
| H2 | SoapySDR requires exact readback of frequency, bandwidth and gain (`actual!=v`). Tuners quantize these values, so ordinary requests are refused. | **confirmed on hardware** (AD9361 at `ip:analog1.local`, set and read back with `iio_attr`): RX LO 100,100,000 → 100,099,998; 98,765,432 → 98,765,430; 101,123,457 → 101,123,454. The example stations 88.7 and 100.6 MHz happen to read back exactly, which is why `fm_receiver.s7diagram` works. Sample rate already needed `rate_tolerance_ppm` (672,000 → 671,999). End-to-end through `s7hw --run` with SoapyPlutoSDR, receive only, 48,000 frames: 100.6 MHz `status=ok`; 100.1 MHz refused with "frequency_hz readback mismatch: requested 100100000, got 100099998" (exit 1). `s7sdr/src/soapy.cpp` |
| H5 | The bundled `examples/sdr/sdr_receive.json` fails on the AD9361 before tuning: 2.4 MS/s reads back as 2,399,998, and `rate_tolerance_ppm` defaults to 0. A failed `prepare` also leaves the radio at the rejected setting. | **confirmed on hardware**: "nominal rate differs from graph: requested 2400000 Hz, got 2399998 Hz" |
| H3 | The capture-plus-AudioSink profile has no drift handling. A slower sound-card clock blocks audio writes until capture overflows, and the run fails instead of degrading. The lack of drift correction is documented; the failure mode and a test for it are not. | traced; `s7hardware/src/hardware.cpp:293–310`, `docs/HARDWARE_IO.md:99–100` |
| H4 | Callback-stream transfers poll with `sleep_for(200µs)`. | traced; `s7audio/src/callback_stream.hpp:138` |

## Design: block buffers between the stepping thread and observers

```
stepping thread                 distributor thread           viewers (UI / workers)
---------------                 ------------------           ----------------------
step()                          pop block                    read committed data
copy probe values  --SPSC-->    append block to:             without the writer's lock
into current block   ring of     - recording (batched I/O)
(no locks, no alloc) N blocks    - live acquisition (1 lock/block)
push when full or                - scope mailbox (blocks)
after flush interval
```

1. **Compiled probes.** Resolve probes once, when the run is prepared, into
   `{const void* value, SignalType type, channel}`. Each step converts the
   values into a preallocated frame `{step, model_time, values[]}`. No strings,
   hash lookups, allocations or locks on the stepping thread.
2. **Fixed-size blocks.** Frames accumulate in a block, for example 1,024
   frames or 20 ms of model time, whichever comes first. The stepping thread
   publishes a full block into a single-producer/single-consumer ring of
   preallocated blocks. A final partial block is flushed on completion, stop,
   pause and error.
3. **One distributor thread per run.** It drains the ring and appends whole
   blocks to the recording, the live acquisition and the scope mailbox: one
   lock per block instead of one per sample per sink. Slow consumers (disk,
   viewers) delay only the distributor.
4. **Explicit, visible overflow policy** for a full ring:
   - `lossless` (backpressure): the stepping thread waits for a free block.
     Results and recordings stay complete; only wall-clock speed drops. Every
     wait is counted and shown.
   - `drop`: the newest block is discarded, counted through the existing
     `captureDropped`/`incomplete()` mechanism, and the recording is marked
     incomplete. Execution timing is never affected.
   - Device-paced (hardware) runs always use `drop`; blocking the device thread
     is never acceptable.
   - Decided in D1: live views are always latest-wins and never cause waiting;
     only the recording applies backpressure (`lossless`, the default) or
     drops (`drop`, opt-in per run).
5. **Viewers read committed data without the writer's lock.** The distributor
   publishes an atomic committed-sample count after each block is fully
   written; viewers read only below it. File-backed recordings use a separate
   read-only file handle, so readers never move the writer's position (fixes
   O4). Envelope building (`timeSnapshot`, `setTimeSpan`) copies raw bucket
   data under a short lock and sorts outside it (fixes O3). The mailbox swaps
   its container in O(1) instead of moving elements (fixes O5).
6. **Decimation and rate limiting stay in viewers.** The stepping thread never
   decimates; it only copies probe values. Viewers keep refreshing at their own
   rates (16–40 ms timers), reading from committed data.

Every run keeps its `runId`/generation token, so blocks from a retired run are
discarded, as `ScopeSampleMailbox` already does.

## Work packages

Do them in this order. Each package is a separate branch and PR with
before/after measurements from the same harness. WP0 comes first so that every
later claim is measured.

### WP0 — Measurement harness (no behavior change)

- Add an opt-in benchmark, kept out of CTest, that runs a representative graph
  (gain chain, pendulum, FM-receiver-like scalar chain) with 0, 4 and 8 probes.
  It reports steps/s, p50/p99 step time, stepping-thread time spent in
  observation, and time blocked on observation locks.
- Add a mode that emulates viewers, polling `read`/`tail`/`timeSnapshot` at
  Studio's real intervals (16, 33 and 40 ms), so O3/O4 contention can be
  reproduced without a GUI.
- Run every gate both unrestricted and on an emulated slow host: one pinned core
  plus a CPU quota, since user machines are 3–10× slower.
- Extend the existing benchmark tooling (`s7core/benchmarks/`,
  `tools/ai/benchmark.py`) instead of creating a parallel runner. Record results
  in `RUNTIME_PERFORMANCE_ARCHITECTURE.md`.
- **Acceptance:** reproducible baseline numbers for O1–O5 and P1, committed with
  the exact commands used.
- **Implemented (2026-11-25):**
  - `s7studio_observation_load_benchmark` drives the real `LocalSchemaRunner`
    with a gain chain or a Euler oscillator plus gain chain, 0–8 probes,
    recording and/or live acquisition, and the viewer emulation. It reports
    steps/s and per-call viewer latencies.
  - `tools/run_observation_benchmark.py` runs the matrix in fresh processes.
    It waits for a quiet machine (load average) and supports a `slow` profile
    (two CPUs, 60% CPU quota, transient systemd user scope).
  - `tools/ai/benchmark.py` records agent task outcomes, not code
    performance, so it was not extended. Runtime step costs keep using
    `s7core_indexed_benchmark`.
  - Invocation:

    ```sh
    cmake --build build --target s7studio_observation_load_benchmark s7core_indexed_benchmark
    tools/run_observation_benchmark.py \
      --binary build/s7studio/s7studio_observation_load_benchmark \
      --output /tmp/observation-baseline.json   # --quick for a smoke check
    ```

### WP1 — Cheaper runtime bookkeeping copies (P1)

Two steps, both without semantic change. ADR-021 rollback, publication and
staging keep their exact behavior (decision D2).

1. **Cheaper copies.** Replace runtime-sized `memcpy` on per-step paths with
   copies specialized by storage size (1, 2, 4, 8, 16, 32 bytes) chosen when
   the domain is built, so each copy is inlined.
2. **Fewer copies.** Today a memory output is copied five times per step:
   before-publish, publish, pre-step snapshot, staged copy, restore. Every
   direct output is copied once more for rollback. Audit each copy (including
   duplicate registrations in trial regions) and remove those that are provably
   redundant. Candidates to evaluate:
   - Skip the before-publish copy when no publish is pending.
   - Restore the published value from the previous staging buffer, where the
     buffer discipline guarantees it is untouched. Coupled and trapezoid stages
     also write `staged[next]`, so this needs proof.
   - Avoid the snapshot/restore round-trip for memory elements whose step reads
     no other memory output.
   - Group copies per element when its outputs are contiguous.

   Each removal records why it is safe, and lands only with fault-injection
   tests for its case.
- **Tests:**
  - The existing core, coupled-integration and rollback suites pass with
    bitwise-identical outputs; compare the `checksum` of every benchmark case.
  - New fault-injection tests throw in each phase (direct element, coupled
    trial, trapezoid trial, memory element) at the first, middle and last
    position, in single- and multi-domain graphs. A retry must produce the same
    trajectory as a run without the fault.
- **Acceptance:** lower `gain512` and `pendulum` step time, reported with
  before/after `perf` shares. Re-evaluate the D2 fallback trigger with these
  numbers.

### WP2 — Compiled probes in `s7observe` (O1)

- Add an API that binds a probe list once and returns typed value pointers plus
  types; `sample_output` stays for occasional reads. `NodeRuntime` exposes what
  is needed without per-call allocation (for example, a non-allocating
  `output_binding_of`).
- Keep the stepping-thread check of `ObservationService`, but do it once at
  binding, not per read.
- **Tests:** values read through the compiled probes are bitwise equal to
  `output_of` for every signal type, including nested-schema aliases, across
  multiple rate domains.
- **Acceptance:** per-probe per-step cost within a few ns of a raw pointer read.

### WP3 — Block ring and distributor thread in Studio (O2, H1)

- Replace the per-sample work in `LocalSchemaRunner::emitScopeSamples`
  (`local_schema_runner.cpp:295–321`) with frame copies into blocks, the SPSC
  ring and the distributor thread described above. Hardware runs use the same
  path with `drop`.
- The policy and ring size are part of the run configuration and are recorded
  in the run provenance.
- **Tests (headless):**
  - Results are identical with 0 vs 8 probes: same final outputs and the same
    recorded values as direct reads.
  - `lossless` loses nothing: recorded samples = steps × recorded channels.
  - `drop`: losses are counted, `incomplete()` is set and the UI shows it.
  - Stop, pause, reset and error flush or discard exactly as specified; stale
    run generations are dropped.
  - Step numbers per channel increase strictly across block boundaries.
- **Acceptance:** with 4 probes and the viewer emulation at real intervals, the
  stepping thread spends ≤ 5% of step time on observation, on both host
  profiles.

### WP4 — Viewer reads without the writer's lock (O3, O4, O5)

- `ObservationSource`: batched file writes, an atomic committed count, a
  separate reader handle and no seeks on the writer handle.
- `LiveAcquisition`: block append; take snapshots under a short lock and do the
  envelope build and sort outside it. Move `setTimeSpan` re-bucketing off the
  lock path, or onto the distributor.
- `ScopeSampleMailbox`: carry blocks with channel ids instead of per-sample
  strings; `take` swaps its container.
- **V1:** replace stride subsampling in the 3D trajectory with
  extreme-preserving reduction. Per bucket, keep the first and last point, each
  axis's minimum and maximum, and non-finite gaps, as the Phase Portrait does in
  2D. Test: a one-sample spike on any axis stays in the reduced vertex set.
- Viewer refresh follows D3: no live view above 60 Hz; previews and docks at
  30 Hz or less where WP0 shows no visible difference.
- **Tests:** concurrent reader/writer stress under ThreadSanitizer, with readers
  at viewer intervals during a 10^7-sample run. No torn frames, no reads past
  the committed count, and file-backed recordings bitwise equal to in-memory
  ones.
- **Acceptance:** the distributor's maximum block-append latency stays bounded
  while viewers poll. Record the number; don't assume it.

### WP5 — Hardware-bound fixes (H2, H3, H4, H5)

- **H2/H5 (D4):** in `s7sdr/src/soapy.cpp`, replace the exact `actual!=v`
  checks for frequency, bandwidth, gain and sample rate with
  accept-and-report. Keep the failure for antenna/gain-mode mismatches and for
  an explicit `rate_tolerance_ppm`. Record requested and actual values in
  `Descriptor::backend_details` and print them in `s7hw` and the Studio run
  log. Update `HARDWARE_IO.md` and the SDR examples, removing the now
  unnecessary `rate_tolerance_ppm` where it was only a workaround.
  - **Tests:** a fake Soapy device with quantized readback, in CI. Opt-in on
    the AD9361: 100.1 MHz and 2.4 MS/s are accepted with actuals 100,099,998
    and 2,399,998 reported. Every bundled SDR example runs.

- **H3:** document the failure mode in `HARDWARE_IO.md`. Add a deterministic
  test with two fake streams at slightly different rates that expects a clear
  overflow status and an incomplete run, not a hang. Drift correction
  (adaptive resampling) stays future work under the hardware contract.
- **H4:** replace the 200 µs sleep-poll with `std::atomic::wait`/`notify_one`
  on the ring counters, signalled from the callback without locks or system
  calls that can block. Keep the deadline semantics. Re-run the PortAudio and
  PipeWire smoke tests and the FM receiver live benchmark (opt-in hardware).

### WP6 — Multi-rate step arithmetic (P2, low priority)

- Precompute, per domain, the reduced ratio and the integer fast path when the
  domain is built. Keep the overflow checks.
- **Tests:** exact equality with the current `samples_through` over randomized
  rates and step counts, including overflow boundaries.

## Decisions

- **D1 — Overflow policy for simulated runs. DECIDED 2026-11-25: per-destination
  rules.**
  - **Recording:** lossless by default. When it can't keep up, the stepping
    thread waits. Studio shows the wait as a share of wall-clock time
    ("run throttled by recording: N%").
  - **Live views** (Live Oscilloscope, Scope live preview, dock previews):
    latest-wins. They skip intermediate blocks and never make the stepping
    thread wait.
  - **Optional per-run recording mode `drop`:** the stepping thread never waits.
    Discarded blocks are counted through `captureDropped`, and the recording is
    marked incomplete. It is opt-in, never the default, and recorded in the run
    provenance.
  - **Device-paced (hardware) runs:** always use `drop` for every destination.
- **D2 — Per-step rollback snapshot (ADR-021). DECIDED 2026-11-25: keep
  ADR-021 unchanged; reduce copying in WP1 first. Re-decided 2026-11-26 after
  measurement (see Results): keep A; B stays rejected while the trigger below
  is not met.**
  - Rollback is required: the headless runtime resumes the same graph after
    `RECOVER_STREAM` (`s7runtime/src/runtime_service.cpp:1191–1206`), so
    removing the snapshot (option C) is rejected.
  - Fallback (option B, not adopted): if WP1 measurements still show the
    snapshot as a large share of step time, take it as a new decision.
    Trigger (restated 2026-11-26): more than 10% of step time in a realistic
    graph, `pendulum` or heavier. Gain chains are a stress test only; they
    measured 15–18%, `pendulum` 7–9%. B would let runs that never retry after a fault
    (Studio local runs, `s7hw`) skip it, while the headless runtime keeps
    ADR-021. B requires an ADR-021 amendment and a guarantee that nothing reads
    outputs after a fault in such a run.
- **D3 — Block flush interval. DECIDED 2026-11-25: fixed internal defaults.**
  - A block is handed over at 1,024 frames or 20 ms of wall-clock time,
    whichever comes first. The ring holds 64 blocks.
  - The stepping thread checks the clock every 64 frames, not every step.
  - Pause, stop, completion and error flush the partial block immediately.
  - These are internal constants tuned only through WP0 measurements; there is
    no UI setting.
  - **Human perception sets the budget.** People cannot follow changes faster
    than about 20–50 ms, so:
    - Live views never refresh faster than 60 Hz. Previews and docks may use
      30 Hz or less. No work is done to deliver data faster than this.
    - A fast change must not disappear just because the eye cannot follow it.
      Live views reduce data with a min/max envelope, as `LiveAcquisition`
      time buckets already do, never by plain subsampling. A one-sample spike
      stays visible as part of the envelope.
    - Fast events that need detail use triggered capture or the recording,
      not a faster refresh.
    - WP0 records render CPU time per viewer, so refresh rates can be lowered
      where nobody can see the difference.
- **D4 — SoapySDR readback policy. DECIDED 2026-11-25: the hardware is right;
  accept and report.**
  - **Continuous settings** (sample rate, frequency, bandwidth, gain): the
    device sets the closest value it can realize, and S7 accepts it. The
    requested and actual values are recorded in the run descriptor and the run
    log, so nothing changes silently.
  - **Model time keeps the nominal rate.** The graph's rate domain stays the
    requested sample rate, as documented today ("the graph and audio device use
    nominal rates"). The actual rate is reported, not used as model time, so
    integer capture/audio ratios such as 672 kS/s → 48 kHz keep working.
    Treating the actual rate as model time would be a separate semantic
    decision.
  - **Discrete settings** (antenna, gain mode) are not rounding: a readback
    mismatch means the device refused the choice, and preparation still fails.
  - `rate_tolerance_ppm` remains accepted for compatibility, as an opt-in
    strict check. No new tolerance options are added.
  - Out-of-range requests are reported as clamped by the device, but not
    refused.

- **D5 — Exact retry for elements with private state (R1). DECIDED
  2026-11-25: report now, snapshots later
  ([ADR-029](../../doc/S7_Architectural_Decisions.md#adr-029)).**
  - R1 is one instance of a general limit: ADR-021 never restored private
    state, so every stateful element that stepped before a fault (filters,
    oscillators, sources, the Resampler) advances again on retry.
  - Now: ADR-022's claim is corrected. `NodeRuntime::retry_exactness()`
    records failed steps after which stateful direct elements had advanced.
    The headless node logs `runtime.execution.retry_inexact` and reports it
    in `STREAM_STATUS.execution`. No per-step cost, no API break for plugins.
  - Later: exact retry through general state snapshots (roadmap
    "Snapshots/restore"), not a per-element `on_step_commit()` plugin hook.

- **D6 — Scope preview mailbox. DECIDED 2026-11-26: notification-only.**
  - The Studio UI read only the preview's drop count and drew from the
    recording, so the per-sample `ScopeSample` copy (with a probe-name string)
    was pure overhead.
  - `LocalSchemaRunner` now emits one coalesced `recordingAdvanced()` per
    acknowledgement (`acknowledgeRecordingAdvanced()`); no samples go to the
    UI. The "preview skipped N updates" message and counter are gone, and
    viewers show real capture loss from the recording.
  - `ScopeSample` and `ScopeSampleBatch` remain as the input of
    `ObservationSession` and `ViewerManager::deliver` for hosts that deliver
    samples themselves.
  - Tests that inspected preview samples now check the recording.
  - Also fixed: a drop-mode run no longer reports "run throttled by recording"
    for the lossless hand-over of its last partial block, which waits only
    after stepping has finished.

## Verification rules

- Observation must never change model results. Every package that touches the
  observation path compares final outputs and recorded values with and without
  probes, bitwise.
- Report performance only from WP0 harness runs, with commands, host profile
  and build type. Optimizing for the development host alone is not enough.
- Run GUI tests headless, according to
  [STUDIO_GUI_TESTING.md](STUDIO_GUI_TESTING.md); the core and headless suites
  run in CI as today.
- Hardware tests remain opt-in and are never run by CI. Available on the
  development host (2026-11-25):
  - **SDR:** one AD9361 LibreSDR on the LAN at `ip:analog1.local`. `iio_info
    -S` finds it; `analog.local` and the USB SDR were not visible.
  - **Sound:** PipeWire `default` and PortAudio devices.
  - **Soapy access:** the radio is reached through SoapyPlutoSDR, which is not
    installed yet. Install it before running the FM receiver or WP5 checks.
  - **Receive only:** no RF transmission without explicit approval.
