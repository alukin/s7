# Runtime performance architecture investigation

Status: indexed serial execution implementation for 0.2.0, 2026-10-30.
Earlier parallel-scheduler measurements and proposals below remain research,
not a supported performance envelope.

## Indexed serial execution design

The compiler currently keeps topologically ordered string IDs. Each runtime
step resolves those IDs through the element hash map for direct evaluation,
memory staging and every coupled trapezoid RHS iteration. Output snapshots
and virtual dispatch are separate costs and stay unchanged in this change.

The executable graph will keep its ID orders and maps for diagnostics and
public lookup, plus parallel vectors of `Element*` in exactly the same order.
The compiler fills these vectors after validation, element creation, input
binding and scheduling. The map owns each element through `unique_ptr`;
moving the graph or rehashing its map moves `unique_ptr` objects, not their
heap-allocated elements. The pointers therefore remain stable while the graph
owns its elements. Runtime construction moves the complete graph into its
private member and uses the records directly. Destruction destroys the records
with the graph. Graph replacement requires constructing a new runtime from a
new compiled graph; nested schemas already discard and recompile their private
runtime when their schema changes.

The phases remain publication of previously staged causal-memory outputs,
causal output snapshots, direct elements, coupled trapezoid trial evaluation,
ordinary causal-memory staging, then step-count advance. The same memory-order
index continues to select its output range and implicit-stage flag. Failed
direct or trial evaluation restores published outputs; a failing memory step
restores only that memory element's outputs, as before. Private state and
external side effects have no new rollback guarantee. Existing port pointers,
signal types, plugin-created elements and nested runtimes use the same
`Element::step()` contract. Only the dispatch reference changes.

Expected gain is the removal of repeated string hashing and map traversal,
particularly for small elements and repeated implicit iterations. Compiling
three pointer vectors adds linear storage and a small one-time lookup cost.
Virtual calls, output snapshots, nested-schema output lookup and actual element
work remain potential bottlenecks. The benchmark below measures the effect
with the full production step path.

## Indexed execution implementation and measurements

`Compiler::compile` now resolves each ordered ID to an `Element*` after binding
and scheduling, storing three pointer vectors on `ExecutableGraph`. Runtime
initialization uses them to build the existing output ranges and snapshots;
`NodeRuntime::step()` and coupled trials call these pointers directly. The ID
orders, UUID mappings, output storage and phase boundaries are unchanged.
The only per-step string lookup left in these workloads is inside a nested
`Schema::step()` when it reads each exposed internal output; optimizing that
boundary is separate from this dispatch change.

The opt-in benchmark `s7core/benchmarks/indexed_execution.cpp` uses the full
`NodeRuntime::step()` path. It covers 64/512 Gain chains, 512 independent
Gains, the committed `double_pendulum_full.s7diagram`, a feedback loop, 16
nested one-Gain schemas and a coupled trapezoid oscillator. Both versions run
the same benchmark source, with the old and new `s7core` shared libraries and
matching compile-time `ExecutableGraph` headers. The comparison script in the
same directory alternates version order, checks final output equality and
reports medians from five fresh processes per case. Each process warms up for
1,000 steps, times 20,000 continuous steps as one interval, then times 2,000
individual steps for latency percentiles. The output binding is read after
execution, so the graph work contributes to an observable checksum.

Measured 2026-10-30 on an AMD Ryzen 7 4700U, Linux x86-64, GCC 16.2.1,
`Release` (`-O3 -DNDEBUG`), Studio off, pinned to CPU 0 with `taskset -c 0`:

| Graph | Compile before/after (us) | Avg step before/after (ns) | Speedup | Throughput before/after (M steps/s) | p50 / p99 before (ns) | p50 / p99 after (ns) | RSS delta before/after (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 Gain chain | 356 / 358 | 1317 / 338 | 3.90x | 0.76 / 2.96 | 2444 / 2934 | 1466 / 1956 | 472 / 536 |
| 512 Gain chain | 1942 / 1950 | 13006 / 2659 | 4.89x | 0.08 / 0.38 | 14108 / 22139 | 3841 / 4680 | 964 / 964 |
| 512 independent Gains | 1890 / 1884 | 13003 / 2364 | 5.50x | 0.08 / 0.42 | 14108 / 21930 | 3701 / 4261 | 912 / 976 |
| Full double pendulum | 792 / 799 | 842 / 301 | 2.80x | 1.19 / 3.32 | 1956 / 2445 | 1397 / 1886 | 516 / 644 |
| Stateful feedback | 107 / 104 | 56 / 35 | 1.59x | 17.81 / 28.31 | 978 / 1467 | 978 / 1467 | 464 / 464 |
| Nested schemas | 937 / 935 | 1785 / 1133 | 1.58x | 0.56 / 0.88 | 2864 / 3422 | 2375 / 2864 | 496 / 560 |
| Coupled trapezoid | 108 / 109 | 151 / 107 | 1.41x | 6.62 / 9.34 | 1397 / 1467 | 1397 / 1467 | 464 / 528 |

Compile timing is `Compiler::compile` only; runtime construction is measured
separately by the benchmark and was 2-21 us in these runs. The RSS column is
the process resident-set change from immediately before compilation to after
runtime construction. It includes allocator/page granularity and library
activity, so differences of one or two 64 KiB pages do not establish a graph
memory regression. Independently timed p50/p99 samples include two clock reads
per step; for very short graphs their timer overhead dominates. The aggregate
average and throughput avoid that per-step measurement cost. Runs were pinned
but not thermally isolated, and these workload results do not predict gains
for I/O-heavy or expensive plugin elements.

Reproduce with a Release build of each revision, using this benchmark source
and that revision's headers/library in each build. The parent revision needs
the opt-in source copied into its worktree and linked against its own `s7core`.
Then run, for example:

```sh
taskset -c 0 python3 s7core/benchmarks/compare_indexed.py \
    /path/to/baseline/s7core_indexed_benchmark \
    build-release/s7core/s7core_indexed_benchmark \
    --baseline-lib-dir /path/to/baseline/library-directory \
    --repeats 5 --steps 20000
```

Correctness checks: the full headless Release suite passed (45/45), including
saved numerical diagrams, typed F16/F128/C16/C128 integration, nested schemas,
plugin loading and output rollback. A focused indexed-record test checks
pointer alignment after graph moves/replacement, split/join order, multiple
memory elements and coupled trials. Clang 22 AddressSanitizer/UndefinedBehavior
Sanitizer passed the four focused core tests (4/4). The benchmark verified the
same final F64 outputs before and after for all seven workloads. The Windows
`s7core` DLL also rebuilt with Clang 22 MinGW, and the focused test passed
under Wine. This change does not alter arbitrary private-state rollback or
plugin interface behavior.

## Objective and constraints

Optimize compile time, step throughput, step latency and memory use for causal
graphs while keeping the current schema -> validate -> compile -> execute ->
observe contract and the simple `Element::step()` interface. Compile the runtime
connection graph and its scheduling data, retaining each element's own storage
and port bindings. A single-worker implementation remains the numerical
reference. Causal-memory publication, model time, explicit network boundaries,
read-only observation and the coupled trapezoid trial semantics remain intact.
No global ODE/event solver is introduced.

The runtime must work headlessly and on the supported C++ toolchains. Its 26
signal types include narrow integers, F16/F128 and complex values; the compiled
plan must retain exact port types and existing overflow rules. Studio and
plugins consume the same compiled runtime.

## What the current runtime does

- `Compiler::compile` creates an `Element` object for each instance, binds an
  input directly to the source object's output member, and creates string-ID
  `causal_order`, `memory_order` and `trapezoid_rhs` lists. The topological
  dependency adjacency is discarded after ordering (`s7core/src/compiler.cpp`).
- `NodeRuntime::step` publishes staged memory outputs, copies every direct
  output to a rollback snapshot, looks up every direct element by string ID,
  calls virtual `step()`, runs the coupled implicit stage, then steps and stages
  causal-memory elements (`s7core/src/runtime.cpp`). An exception restores
  published outputs, but arbitrary private element state cannot be rolled back.
- Small element objects carry cold metadata and port vectors alongside hot
  state. On this 64-bit build, `sizeof(Element) == 104`, `sizeof(Gain) == 128`
  and `sizeof(Port) == 56`; objects and port vectors are separately allocated.
- A schema-backed element is a nested `Schema` whose `step()` copies exposed
  inputs, runs a separate `NodeRuntime`, then copies exposed outputs. This
  preserves a useful composition boundary but adds per-step dispatch and lookup
  (`s7core/src/schema.cpp`).
- Plugin entry points are C symbols, but registration passes a C++
  `ElementRegistry*` and plugins subclass `Element`; this is already a
  same-toolchain C++ SDK contract (`docs/PLUGIN_INTERFACE.md`). In-tree
  `s7noise` and the out-of-tree SDK reference `s7spectral` exercise it.

## Provisional microbenchmark

One unpaced Release (`-O3 -DNDEBUG`) probe on an AMD Ryzen 7 4700U compared
simple F64 Gain graphs. It was compiled against the current `build-release`
`s7core` library. The indexed variant resolves `Element*` once and retains
per-output rollback snapshots; the current variant calls `NodeRuntime::step`.
The nested variant puts one Gain in each schema-backed element. Values below
are approximate wall nanoseconds per graph step over repeated runs.

| Graph | Current runtime | Indexed elements + snapshots | Nested schemas |
| --- | ---: | ---: | ---: |
| 64 Gain chain | 1,400 ns | 330 ns | 6,400-6,900 ns |
| 512 Gain chain | 13,900 ns | 2,700 ns | not measured |
| 512 independent Gains | 13,600-14,000 ns | 2,350-2,400 ns | not measured |

The indexed probe is not a replacement implementation: it omits the general
memory/implicit phases and some runtime bookkeeping. The synthetic graphs do
almost no arithmetic, so they expose dispatch and snapshot overhead and do
not predict speedups for FIR, FFT, I/O or realistic diagrams. Runs were not
CPU-pinned or thermally controlled. These results are strong enough to justify
a reproducible benchmark suite and profile before selecting a storage layout;
they are not release performance targets.

## Representative diagram: `double_pendulum_full`

The saved diagram has 39 elements and 58 connections: four Euler integrators
are causal memory, and 35 elements form the direct-feedthrough DAG. Ignoring
the integrator edges that cross a step boundary, this DAG has nine longest-path
levels, with at most six elements in one level. The two acceleration paths
split and rejoin, so their operations can overlap when their inputs are ready;
they cannot be permanently assigned one worker each. A separate unpaced
Release probe of 200,000 `NodeRuntime::step()` calls measured about 0.86 us per
step on the same Ryzen 7 4700U, without observation or Studio. That is a
provisional baseline, and pool dispatch may consume much of it.

Heavy elements change the economics: if independent branches contain costly
`step()` calls, overlapping them can be valuable. A single heavy element on
the critical path remains serial inside that call. The scheduler should
preserve the element interface and use coarse ready work where possible,
without assigning every trivial arithmetic element its own task.

## Heavy-branch experiments

Two opt-in, reproducible benchmarks now live in `s7core/benchmarks/`:
`fir_branches.cpp` uses the existing scalar `FirFilter`, and
`fft_convolution_branches.cpp` processes independent complex frames with FFTW.
Both compare two serial branches with one persistent worker plus the coordinator
and verify parallel results against serial results. The FFT benchmark also
checks full complex linear convolution against a direct reference and verifies
the expected peak location for a delayed matched-filter echo. Neither
benchmark is the production graph scheduler.

Run them from a Release build with FFTW3 installed:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target s7core_fir_branch_benchmark s7core_fft_convolution_benchmark
./build-release/s7core/s7core_fir_branch_benchmark
./build-release/s7core/s7core_fft_convolution_benchmark
```

Representative Release results on the Ryzen 7 4700U (ranges reflect repeated
local runs; lower time is better):

| Workload per branch | Serial time per step/frame | Two-branch parallel time | Observed outcome |
| --- | ---: | ---: | --- |
| Scalar FIR, 64 taps | ~0.12 us | 2.0-6.2 us | large regression |
| Scalar FIR, 4,096 taps | 6.7-6.8 us | 12.2-16.0 us | regression |
| Scalar FIR, 16,384 taps | 27.3-27.5 us | 22.4-34.3 us | mixed, unstable result |
| Buffered FFT, 2,048 samples + 1,024-tap impulse | 163-169 us | 207-270 us | regression |
| Buffered FFT, 8,192 samples + 4,096-tap impulse | 922-1,074 us | 1,121-1,473 us | regression |
| Buffered FFT, 16,384 samples + 8,192-tap impulse | 2.17-2.34 ms | 2.66-3.41 ms | regression |

The worker prototype uses `std::barrier` once per step/frame and separate
element/FFT state. It excludes compilation, runtime output snapshots,
observation and graph replacement. The runs were not isolated or CPU-pinned;
these measurements establish neither a scheduler speedup nor a universal
parallelism limit. They do show that a thread pool by itself is insufficient:
dispatch, synchronization and memory behavior must be measured on the intended
hardware. FFTW plans were created on the coordinator thread and only executed
concurrently, following [FFTW's thread-safety contract](https://www.fftw.org/fftw3_doc/Thread-safety.html).

The buffered FFT benchmark matches the radiolocation example more closely than
the scalar FIR: each call consumes a complete complex frame and returns the
full linear convolution for that frame. A production graph element cannot yet
use that frame value. Current `SignalType` values are scalar/complex scalar,
and `NodeRuntime` snapshots at most 32 raw bytes per output. A frame signal
needs explicit shape, ownership, validity across a step, observation and
rollback semantics before it can cross an S7 connection. The simple
`Element::step()` interface can remain; the port value contract must grow.

The [core convolution processors](CONVOLUTION.md) support both independent
frames (full linear convolution) and a continuous stream of frames (history
carried across calls, one output frame per input frame). Their smoke test
compares both algorithms against the same direct reference with exact frame
index alignment. They are not yet graph elements because frame ports need the
value contract described above.

## Proposed compiled runtime connection graph

1. **Retain `Element`.** Element instances continue to own their outputs and
   state, and connections remain bound to their output pointers. `step()`,
   `Feedthrough`, `has_state`, port registration and the current plugin element
   interface remain the author-facing contract.
2. **Compile dense execution records.** Resolve each ID once to a stable
   `Element*`; store direct-feedthrough successors, predecessor counts,
   topological order, output snapshot locations, memory order, trial-RHS order
   and observation ID mappings in arrays. Strings and maps stay available for
   diagnostics but leave the steady-state step loop. The compiler already
   builds the dependency adjacency temporarily, so it can retain this plan.
3. **Compile scheduling regions.** Identify independent ready nodes and chains
   separated by split/join points. A bounded pool takes ready regions; a join
   becomes ready only when every required predecessor finishes. Worker count
   one uses the same graph plan as the numerical reference. Keep a fast serial
   path for narrow or cheap graphs. Use benchmarks to choose task granularity;
   element count or visible diagram layout alone is not a cost model.
4. **Keep step phases explicit.** The coordinator publishes causal-memory
   outputs and prepares rollback snapshots. Workers evaluate audited direct
   elements and finish before coupled trapezoid trials and causal-memory
   staging. Observation reads only after step completion. On failure, stop
   dispatch, drain workers and restore published outputs before rethrowing;
   existing private-state and external-side-effect limits still apply.
5. **Handle safety outside the element API.** `has_state()` does not prove
   thread safety. Audit built-in instances; keep blocking file/network work
   on a serial or separate lane. Unknown plugin types default to serial until
   their registration metadata or a separate policy explicitly permits
   concurrent instances. This need not add methods to `Element`.

Object storage still has a locality cost, but arraying execution records and
removing per-step hash lookups already has a measured opportunity. Measure
cache misses, latency and memory use on actual graphs before considering
allocators or a second data-layout change. Schema-backed elements currently
run nested runtimes; flattening them can be a later compiler optimization if
its feedback and step-boundary semantics are proved equivalent.

## Work sequence and decision gates

1. Add reproducible headless benchmarks and correctness fixtures: narrow and
   wide DAGs, the full double pendulum, unequal branch weights, heavy FIR/FFT
   elements, stateful feedback, nested schemas, implicit integration, I/O
   boundaries and observed runs. Measure compilation, throughput, p50/p99
   step latency, CPU, RSS, cache misses and worker-count scaling on named
   hardware.
2. Compile indexed `Element*` and dependency arrays without changing element
   behavior. Demonstrate serial parity and measure the gain from removing hot
   string lookups.
3. Add a bounded worker pool and coarse ready-region scheduling. Test actual
   concurrency with independently expensive elements, split/join ordering,
   failure draining, graph replacement and shutdown. Compare 1/2/4/8 workers
   and automatically use serial execution where parallel dispatch loses.
4. Review measured bottlenecks. Consider nested-schema flattening, alternate
   snapshot handling or data placement as separate changes only with parity
   tests and a demonstrated gain.

This is an AI-assisted implementation plan. The previous engineer-week range
for a complete element/kernel redesign does not apply to this narrower graph
compilation proposal. Set a calendar estimate after the indexed-plan prototype
and the heavy-branch benchmark; correctness and numerical acceptance remain
release gates.

## Relevant external engineering guidance

- [oneTBB Dependency Flow Graph Example](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.1-rev-1/elements/onetbb/source/flow_graph/dependency_flow_graph_example): predecessors determine when joined work may run.
- [oneTBB Task Scheduler](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.4-rev-1/elements/onetbb/source/task_scheduler): pool tasks should represent enough nonblocking computation to justify dispatch.
- [Linux kernel, False Sharing](https://docs.kernel.org/kernel-hacking/false-sharing.html): neighboring worker writes can cause cache-line contention; profile before padding.
