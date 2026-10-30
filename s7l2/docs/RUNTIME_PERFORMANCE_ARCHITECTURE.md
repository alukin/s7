# Runtime performance architecture investigation

Status: indexed serial execution is the completed 0.2.0 performance increment,
2026-10-30. Parallel scheduling, region grouping and static cost tags are
unselected 0.3.0 planning questions, recorded separately in
[the 0.3.0 planning record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md). Earlier
parallel measurements and proposals below are research, not a supported
performance envelope or a 0.2.0 delivery commitment.

## Indexed serial execution design

Before indexed execution, the compiler kept topologically ordered string IDs.
Each runtime step resolved those IDs through the element hash map for direct
evaluation, memory staging and every coupled trapezoid RHS iteration. Output
snapshots and virtual dispatch are separate costs and stayed unchanged.

The executable graph keeps its ID orders and maps for diagnostics and
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

The gain comes from removing repeated string hashing and map traversal,
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

## Historical pre-indexed runtime mechanism

- Before indexed execution, `Compiler::compile` created an `Element` object for each instance, bound an
  input directly to the source object's output member, and creates string-ID
  `causal_order`, `memory_order` and `trapezoid_rhs` lists. The topological
  dependency adjacency is discarded after ordering (`s7core/src/compiler.cpp`).
- The former `NodeRuntime::step` published staged memory outputs, copied every direct
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

## Historical provisional microbenchmark

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
# Linux, when CPUs 2 and 3 are available:
./build-release/s7core/s7core_fft_convolution_benchmark --pin 3 2 --diagnose
```

The FIR results below are the original branch experiment. The FFT figures
originally reported here were invalid: `run_parallel` executed the right
convolution on the worker, then executed it *again* on the coordinator after
the completion barrier to obtain an output span. Thus its timed parallel path
did three convolutions per frame while the serial path did two. The output
comparison did not catch this because a convolution of the same frame is
idempotent. The benchmark now reads the worker's completed output directly and
checks that each branch executed exactly once per frame.

Controlled FFT Release comparison, measured 2026-10-30 on the Ryzen 7 4700U
with GCC 16.2.1, FFTW 3.3.10, coordinator pinned to CPU 3 and worker to CPU 2
(same L3 group). The duplicate-call and corrected binaries used the same
source and flags except for the output read and its step-count expectation.
They ran in alternating order, five times each. Values are medians of complete
runs; lower is better:

| Workload per branch | Serial | Parallel with duplicate call | Corrected parallel | Corrected speedup |
| --- | ---: | ---: | ---: | ---: |
| 2,048 samples + 1,024-tap impulse | 171 us | 284 us | 159 us | 1.08x; noisy |
| 8,192 samples + 4,096-tap impulse | 926 us | 1,492 us | 703 us | 1.32x |
| 16,384 samples + 8,192-tap impulse | 2.16 ms | 3.36 ms | 1.43 ms | 1.51x |

The earlier FIR prototype remains a separate result: 64 taps measured about
0.12 us serial versus 2.0-6.2 us parallel; 4,096 taps about 6.7-6.8 us versus
12.2-16.0 us; and 16,384 taps about 27.3-27.5 us versus 22.4-34.3 us. It
does not establish a result for the corrected FFT benchmark.

The worker prototype uses two `std::barrier` rendezvous per frame and separate
FFT state. It excludes compilation, runtime output snapshots, observation and
graph replacement. Use `--pin MAIN_CPU WORKER_CPU` to control each thread's
placement and `--diagnose` to report branch work, barrier waits and FFT phases.
Pinning both threads matters: a process-wide `taskset` mask still permitted
both threads to run on one core during some frames. In one diagnostic run on
CPU 2/3, the large case spent about 1.11 ms per branch serially, versus
1.22 ms on the coordinator and 1.50 ms on the worker concurrently; the
completion wait averaged 0.31 ms. A worker-only run was also slower in that
sample, so these measurements do not isolate cache pressure from core frequency
or transient machine load. The corrected benchmark establishes a speedup on
these large independent branches, but not a universal 2x gain or a production
scheduler result. FFTW plans were created on the coordinator and only executed
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

## Candidate parallel designs for 0.3.0 planning

The following ideas have not been selected. The indexed serial runtime already
meets the 0.2.0 performance scope. The [0.3.0 planning record](RELEASE_0.3.0_RUNTIME_PARALLELISM.md)
lists the questions and evidence needed before a parallel contract or release
commitment is made.

1. **Retain `Element`.** Element instances continue to own their outputs and
   state, and connections remain bound to their output pointers. `step()`,
   `Feedthrough`, `has_state`, port registration and the current plugin element
   interface remain the author-facing contract.
2. **Retain indexed execution.** The existing `ExecutableGraph` already owns
   stable `Element*` vectors in causal, memory and trial-RHS order. Keep these
   as the canonical serial order and retain the string IDs for diagnostics.
   Add direct-feedthrough dependency indices to the same compiled graph; the
   compiler already builds this adjacency while scheduling.
3. **Possible coarse regions.** One candidate would fuse cheap connected
   elements when their dependency edge has one successor and one predecessor,
   stopping at split and join points, causal-memory boundaries and unsafe
   elements. Expensive independent branches might run separately. Partition
   rules, task granularity, readiness and cost estimation remain open; element
   count alone is insufficient.
4. **Required phase invariants.** The coordinator publishes causal-memory
   outputs and prepares rollback snapshots. Workers evaluate audited direct
   elements and finish before coupled trapezoid trials and causal-memory
   staging. Observation reads only after step completion. On failure, stop
   dispatch, drain workers and restore published outputs before rethrowing;
   existing private-state and external-side-effect limits still apply.
5. **Unresolved concurrency policy.** `has_state()` does not prove thread
   safety. Built-ins, plugins, nested schemas and side-effecting file/network
   work need an audit before any concurrent execution. A serial fallback is
   the safe starting point; whether policy needs metadata or new interfaces
   remains undecided.

The **current fast serial path** traverses `causal_execution` directly, then
performs the coupled trial and memory phases. Any future worker-count-one mode
should preserve this queue-free behavior and exact causal order. A parallel
plan could share `ExecutableGraph`, but its representation and eligibility
rules have not been chosen. Independent chains may be interleaved in today's
serial order; concurrent reordering needs a proved safety contract.

Region eligibility and dispatch thresholds might be compile/runtime policy
rather than new element semantics. Compare candidate policies on the same
compiled graph, including narrow chains where the serial path should win.
The benchmark correction above shows why both branch call counts and output
parity must be checked: equal outputs alone missed duplicated work.

### Estimating region cost

One candidate is a static price tag assigned to each element instance when
its schema is compiled, summed into a region estimate `C(R)`. The tag could depend
on element kind, resolved signal type and fixed parameters: an Adder scales
with input count, a FIR filter with tap count, and a future FFT graph element
with transform size and plan. Nested schema cost includes its compiled internal
graph. The compiler currently sees `Element::type_name()` and port types but
not private parameters such as `FirFilterT::taps_`; parameter-sensitive tags
therefore need validated instance parameters retained as compiler-side metadata
or a read-only cost hint. This does not require per-step timing or a change to
`Element::step()`.

This approach would need calibration on target hardware using opt-in serial
benchmarks, and whole-region checks for virtual calls and locality effects.
Unknown or highly variable work should retain the serial path until measured
and audited. Concurrency safety is a separate gate from cost.

For the direct-feedthrough region DAG, let `W = sum C(R)` be total work and
`S = max_path sum C(R)` the longest causal path. With `p` workers, an optimistic
parallel lower bound is `max(S, W/p)`. If the potential saving
`W - max(S, W/p)` is smaller than measured dispatch/rendezvous overhead plus
a safety margin, use the serial path immediately. For two independent chains,
the saving is bounded by the cheaper chain's cost: a very expensive branch
cannot make a tiny sibling worthwhile to dispatch. This bound is only a
filter; cache contention and CPU frequency can raise actual parallel time.
These are candidate screening equations, not a chosen price model or threshold.
Any later implementation needs target-hardware benchmarks and a serial path
that keeps the canonical `causal_execution` order without queue operations.

Object storage still has a locality cost, but arraying execution records and
removing per-step hash lookups already has a measured opportunity. Measure
cache misses, latency and memory use on actual graphs before considering
allocators or a second data-layout change. Schema-backed elements currently
run nested runtimes; flattening them can be a later compiler optimization if
its feedback and step-boundary semantics are proved equivalent.

## Possible 0.3.0 investigation sequence

This sequence is a research proposal, not scheduled 0.3.0 implementation work.

1. Extend reproducible headless benchmarks and correctness fixtures: narrow and
   wide DAGs, the full double pendulum, unequal branch weights, heavy FIR/FFT
   elements, stateful feedback, nested schemas, implicit integration, I/O
   boundaries and observed runs. Measure compilation, throughput, p50/p99
   step latency, CPU, RSS, cache misses and worker-count scaling on named
   hardware.
2. Evaluate whether retaining direct-feedthrough dependency adjacency and
   compiling coarse regions provide enough benefit without changing element
   behavior. Compare alternatives before choosing a graph representation.
3. If a parallel contract is approved, prototype a bounded worker pool and
   region readiness on that contract. Test independently expensive elements,
   split/join ordering, failure draining, graph replacement and shutdown.
   Compare 1/2/4/8 workers and serial fallback on named hardware.
4. Review measured bottlenecks. Consider nested-schema flattening, alternate
   snapshot handling or data placement as separate changes only with parity
   tests and a demonstrated gain.

Set a calendar estimate and release commitment only after 0.3.0 planning
selects a contract and benchmark gates. Correctness and numerical acceptance
remain required for any implementation.

## Relevant external engineering guidance

- [oneTBB Dependency Flow Graph Example](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.1-rev-1/elements/onetbb/source/flow_graph/dependency_flow_graph_example): predecessors determine when joined work may run.
- [oneTBB Task Scheduler](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.4-rev-1/elements/onetbb/source/task_scheduler): pool tasks should represent enough nonblocking computation to justify dispatch.
- [Blumofe and Leiserson, Scheduling Multithreaded Computations by Work Stealing](https://www.cs.cornell.edu/courses/cs612/2006sp/papers/blumofe94.pdf): work and critical-path length frame the optimistic parallel bound; measured overhead and resource contention remain separate.
- [Linux kernel, False Sharing](https://docs.kernel.org/kernel-hacking/false-sharing.html): neighboring worker writes can cause cache-line contention; profile before padding.
