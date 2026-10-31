# S7 0.3.0 planning record — within-node parallel execution

Status: deferred investigation, 2026-10-30. No parallel scheduler, region
partition, static cost model, worker-pool policy, or plugin concurrency contract
is approved for implementation. This page records evidence and questions for
0.3.0 planning; it does not promise that parallel execution will ship in 0.3.0.

## What is established

- S7 remains a causal graph runtime. `NodeRuntime::step()` publishes staged
  causal-memory outputs at the boundary, evaluates direct-feedthrough elements
  in causal order, performs coupled trapezoid trials, stages ordinary memory,
  then advances the step count. Observation reads a completed step. An
  exception restores published outputs under the existing rules, not arbitrary
  private element state or external effects.
- Indexed serial execution is implemented: `ExecutableGraph` retains stable
  `Element*` arrays for causal, memory and trapezoid-RHS order. IDs and UUID
  mappings remain for diagnostics and public APIs. The graph owns elements via
  `unique_ptr`; moving the graph does not move those element objects.
- The [performance investigation](RUNTIME_PERFORMANCE_ARCHITECTURE.md) measured
  the production serial path in Release on an AMD Ryzen 7 4700U. Median step
  speedups over string lookup ranged from 1.41x (coupled trapezoid) to 5.50x
  (512 independent Gains); the full double pendulum improved from 842 ns to
  301 ns (2.80x). All 45 headless tests, four focused sanitizer tests and a
  focused Windows/Wine test passed for that change. These are workload results,
  not a general performance guarantee.
- The saved full double-pendulum diagram has 39 elements and 58 connections.
  Its direct graph has 35 elements, nine longest-path levels and at most six
  elements at one level. Narrow graphs and cheap chains leave little time to
  pay for task dispatch.
- The corrected opt-in [FFT branch benchmark](../s7core/benchmarks/fft_convolution_branches.cpp)
  reuses one worker thread for each workload and uses two barriers per frame.
  An earlier reported slowdown was a benchmark defect: the coordinator
  executed the worker's right convolution a second time to read its output.
  A paired comparison with one call per branch measured 926 us serial versus
  703 us parallel at 8,192 samples and 2.16 ms versus 1.43 ms at 16,384
  samples on pinned cores. The 2,048-sample case is noisy. The separate FIR
  branch prototype showed regressions or mixed results. These prototypes do
  not execute an S7 graph and do not establish scheduler performance.
- Graph ports currently carry scalar or complex scalar values, not FFT frames.
  A production frame-valued element needs a separate port, ownership,
  observation and rollback contract. Concurrent FFTW plan execution is valid
  under its documented thread-safety rules when planning is done separately;
  the benchmark creates plans before worker execution.

## Candidate ideas, not decisions

- Keep a fast serial path over the existing `causal_execution` array. A
  potential parallel plan could live in the same `ExecutableGraph`, with no
  schema recompile when changing worker count.
- Coarse regions could group cheap connected chains and dispatch expensive
  independent branches, avoiding a task per trivial element. Split/join and
  causal-memory boundaries are likely partition points. Whether this is the
  right partition, and whether noncontiguous chains may be grouped without
  changing observable behavior, remains to be proved.
- A static price tag per compiled element instance could depend on type,
  resolved signal type and fixed parameters. Region work and critical-path
  length give an optimistic parallel bound; measured dispatch, cache pressure
  and CPU frequency determine whether it pays. Current `Element` exposes its
  type and ports, but not private parameters such as FIR tap count. A metadata
  sidecar or read-only hint is possible; neither is selected. A price tag does
  not establish concurrency safety.
- A bounded persistent worker pool, explicit serial fallback and separate
  eligibility for built-ins/plugins are candidates. No pool implementation,
  worker count, affinity policy, task granularity or cost threshold is chosen.

## Decisions deferred to 0.3.0 planning

1. Define which built-in, stateful, nested-schema, I/O and plugin elements may
   run concurrently. Unknown plugin behavior, private state and side effects
   require an explicit policy. Keep the `Element::step()` contract unless a
   concrete requirement justifies changing it.
2. Choose whether to retain graph dependency adjacency, how to partition
   regions, how joins become ready, and how a one-worker path preserves the
   exact current causal order. Keep causal-memory publication, coupled trials,
   model time and observation boundaries intact.
3. Choose whether cost tags are needed, which fixed parameters they use, how
   hardware calibration works and how variable work is handled. The observed
   FFT and FIR results rule out a universal threshold based on element count.
4. Define failure draining, exception propagation, output rollback, graph
   replacement and shutdown when workers are active. Do not silently extend
   rollback to private plugin state or external effects.
5. Decide whether frame signals and FFT graph elements are separate 0.3.0
   scope. Parallel branch benchmarks alone do not supply that value contract.
6. Establish a benchmark and correctness gate before selecting a scheduler:
   cheap chains, wide and split/join DAGs, unequal branch weights, stateful
   feedback, nested schemas, coupled trapezoids, plugin-created elements,
   numerical traces, faults and repeated graph replacement. Compare the same
   compiled graph at one and several workers on named hardware, including
   throughput, latency, CPU, memory, synchronization and regressions. Test
   sanitizer and supported-platform behavior where practical.

## Related 0.3.0 research: hardware and multi-rate time

0.3.0 adds hardware-based elements, starting with sound sources and sinks
using the OS sound system. Their device clocks are independent rate domains
that drift against UTC and each other, locally and across nodes. Pacing by a
device clock, drift-tracking resampling, networked audio streams and feedback
across rate domains are open research, recorded in
[open problems](OPEN_PROBLEMS.md#ratedt-inference-and-validation--implemented-multi-rate-execution-deferred);
a rate-domain scheduler interacts with any worker-pool design above.

## 0.2.0 boundary

The indexed serial runtime is sufficient for the 0.2.0 performance increment.
0.2.0 does not require or claim within-node parallel execution. The choices
above are postponed to 0.3.0 planning and need their own reviewed contract
before implementation or release assignment.
