# MIT Audio Labs — implementation plan and Codex handoff

**Status (2026-11-20): planning only.** No new AU lab, duplex audio
runtime, physical latency result or effect processing test is claimed
to exist merely because this plan is committed.

Read `AGENTS.md`, the repository's formal model/ADR documents,
[hardware I/O contract](../../../doc/S7_Hardware_IO_Contract.md),
[implemented sound I/O](../../docs/HARDWARE_IO.md),
[2026-11-16 evidence](../../docs/HARDWARE_IO_LIVE_TESTS.md),
[the educational standard](../LAB_STANDARD.md),
[course map](COURSE_MAP.md), [source ledger](SOURCES.md),
[real-time architecture proposal](REALTIME_PROFILE.md)
and [verification requirements](VERIFICATION_MATRIX.md).

## Mission

Use twelve original MIT 6.341-aligned audio DSP and S7 practical-effects
labs to **improve and test S7**. Our central result is a guitar or microphone
connected to a suitable audio interface -> explicit S7 processing graph ->
headphones, with correct numerical results and *measured* low latency.
WAV input/output supports deterministic replay without sound hardware.
This is *not* a music theory, music-recognition or performance course.

## Phase 0: capability and measurement audit

1. Pin main/revision and run current `s7hardware`, `s7audio` and
   relevant S7 core suites. Record commands, environment and exact results.
   Do not call historical results current. Keep hardware tests opt-in.
2. Inspect `s7audio` PortAudio/PipeWire streams, callback queues,
   source/sink lifecycle, `s7hardware::Run`, clock and frame-rate
   semantics, port types, Studio integration and package paths.
3. Inventory existing WAV/PCM source/sink, delays, filter primitives,
   FFT/resampling, input/output metering and parameter updates. Mark
   each feature implemented / partial / unsupported with concrete API
   references; do not invent functionality to make the plan appear easy.
4. Specify minimal full-duplex session ownership and a common-clock
   audio run profile. Develop an explicit ADR for changing the current
   **single hardware boundary** contract; maintain causal publication,
   feedback delay and rollback rules.
5. Specify a safe, single-device, 48 kHz initial hardware bench with
   headphones and low output level. For guitar, use actual Hi-Z input.
   Document backend, driver, sample format and channel routing, not
   guessed device names.
6. Specify an independent **analog** latency measurement method:
   level-safe signal split/reference, measured input-output lag,
   repetitions, uncertainty, dropout telemetry and normal/stressed
   host conditions. No virtual sink timing may be presented as analog
   performance.

**Exit:** reviewed architecture/measurement contracts, observed existing
test baseline and a prioritized issue/capability matrix.

## Phase 1: duplex audio execution, the AU02 foundation

**Infrastructure PR 1 — fake full-duplex backend and graph contract.**
Add a single-clock synthetic input/output provider with bounded preallocated
buffers; accept an explicit `AudioSource -> graph -> AudioSink` topology.
Specify first/last sample mapping, startup silence, block sizes, lifecycle,
accepted vs presented frames, cancellation, drain and all fault statuses.
Test *the exact saved S7 graph* against an independently coded recurrence,
with delay and feedback. Reject unsupported multi-clock or implicit
resampling configurations.

**Infrastructure PR 2 — first physical full-duplex implementation.**
Investigate native PortAudio full-duplex stream on one qualified interface
and compare with native PipeWire options. Avoid placing arbitrary graph
work, locks, heap allocations, logs or visualization in the callback.
Have one graph execution owner and observable bounded queue/pacing policy.
Run first through opt-in virtual endpoints, then physical analog cable
loopback on an explicitly selected interface. Test overrun/underrun,
disconnect, failure during start and safe stop/drain.

**Teaching PR 3 — AU02.** Purpose-built identity live-through diagram,
student guide, simulation input and deterministic output oracle; physically
measure latency with and without S7 under equivalent hardware settings.
The 10 ms local input-to-output goal is **conditional on measurement**,
not an a priori pass declaration.

## Phase 2: deterministic audio files and practical effects

- Deliver AU01 as a reusable WAV/PCM fixture path, with sample-format,
  endianness, stereo ordering, clipping, EOF, replay and full output-tail
  tests. Ensure file operations do not occur on a real-time callback.
- Deliver AU03 EQ, AU04 echo, AU06 overdrive, AU07 compressor in small,
  reviewable PRs. Independent oracle includes exact filter recurrences,
  transfer functions or declared nonlinear waveshapers, early and late
  samples, phase/scale, numerical error and negative cases.
- Compare offline output with device-paced output of **the same DSP graph**
  using captured or synthetic fixtures. Report any difference instead
  of weakening the reference.
- Add safe runtime controls: gain, bypass and dry/wet mix with state
  ownership and explicit smoothing/transition policy. Check live CPU,
  deadline misses, output silence and clipping.
- Test S7's stateful feedback explicitly for causal off-by-one mistakes
  and stability under deliberately adverse parameter settings.

## Phase 3: time-varying, frame-based and more expensive effects

Implement AU05 reverb, AU08 chorus/flanger, AU09 tremolo/vibrato,
AU10 pitch/time manipulation and AU11 FFT-based effects.
Test interpolation, phase continuity, exact group delay,
feedback stability, framed overlap/add, sample-rate transformations and
buffer-size sensitivity with independent short-array references.
Develop missing frame/rate capabilities as separate reviewed engineering
changes; do not introduce hidden frame delays or scheduling exceptions.
Qualify additional live profiles only after the numerical tests pass.

## Phase 4: AU12 guitar pedalboard and performance assessment

- A canonical editable S7 effects chain with explicit, bounded live
  activation and selectable processors.
- A reproducible demo using synthetic and licensed/user-owned WAV audio;
  *optionally* a live guitar through a suitable instrument interface.
- Physical analog loopback with the effect **bypassed**, then each effect
  alone and combinations. Report delta latency, measured p50/p95/p99,
  max observed lag, runtime service times, dropouts/underruns,
  device and buffer settings, CPU load and trial duration.
- Stress test buffer periods, intentional worker stalls, denormal/NaN
  input, gain extremes, slow parameter changes and cancellation.
  A plausible audible effect is **not** a numerical or real-time pass.
- Show a clear degraded/unsupported profile when the target latency
  is not met; do not promise hard real-time or universal <10 ms.

## Release and coordination

The maintainer's **2026-11-20 0.3.0 scope decision** makes all
**AU01–AU12 mandatory release targets**, coordinated with the 24 SDR
labs and four preliminary video tests in the
[0.3.0 work plan](../../docs/RELEASE_0.3.0_WORKPLAN.md). This does not
retroactively enlarge the separate 0.2.0 MIT-EE gate. Deliver a
working, independently measured **AU02 vertical slice** first, then
qualify the remaining eleven labs in reviewable increments.

All accepted changes must include independent regression tests, exact
execution commands, a source/commit, Studio interaction evidence where
required and a qualified platform. Use small PRs; require maintainer
review for changes to causal/scheduler/hardware-clock contracts.
