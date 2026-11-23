# Independent review: array-valued signals, zero-copy and S7 0.2.0 planning

**Review date:** 2026-11-23
**Reviewed:** `alukin/s7`, `main`, commit [`db200146c9b3f6d16cfe5d547c2462c4328b5cf9`](https://github.com/alukin/s7/commit/db200146c9b3f6d16cfe5d547c2462c4328b5cf9).
**Status:** Independent findings and *proposed* fixes; **not an accepted ADR or completed implementation**. No production code, tests or existing planning documents are changed by this review.
**Evidence:** Source and documentation inspected; no builds, tests, performance measurements, GUI screenshots or installed-package verification performed. A code constraint below is not necessarily an existing bug. Architectural risks are hypotheses to validate, not reproduced failures.

## Executive assessment

**Direction: sound; implementation plan: revise before AV01.** Keep native scalar elements and their fast path; add separate array/vector elements behind a common logical port/wire model. One `step()` is one causal computation/state transition, never an implicit per-sample scheduler loop. Zero payload copies are realistic for local bindings, fan-out, forwarding, handle staging and rollback; FFT, gain, conversions, image rendering and transport may require separate output storage or copies. Keep observer processing outside the functional graph.

The critical unresolved questions are: (1) array publication/rollback state machine; (2) producer versus optional observer ownership and pool exhaustion; (3) plugin binary compatibility; and (4) separation of graph evaluation rate, physical sample rate, block identity and presentation time.

## Findings and recommended fixes

### F1 — CRITICAL — Define publication atomicity precisely
**Confirmed code constraint; specification ambiguity.** `s7core/src/runtime.cpp` (around 373–391, 464–548) snapshots native scalar outputs; rollback does not restore arbitrary private state or external side effects. Earlier completed rate domains can remain advanced when a later domain faults. These are documented limits of [ADR-021/022](../../doc/S7_Architectural_Decisions.md).

**Fix:** Define atomic publication **per rate-domain evaluation**, not all-or-nothing rollback of an entire multi-rate run. Separate previous committed handle, current direct-feedthrough candidate, next staged causal-memory handle, and externally observable committed snapshot. Acquire fallible resources before nonthrowing commit. A failed domain restores published handles and prior valid staging without payload copies. Preserve existing limits on private state and irreversible I/O (which may require a latched fault). Add faults in direct, early/late memory and multi-rate scenarios, including consecutive retries and retained observers.

### F2 — CRITICAL — Prevent optional observers from starving producers
**Architectural risk, not an observed defect.** Immutable viewer-retained leases can exhaust a pool that the producer needs for its next writable candidate, inadvertently making execution depend on a slow viewer. See [observation architecture](../../doc/S7_Observation_Architecture.md) and [array design §3](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md).

**Fix:** Specify separate execution-critical reserve/admission and optional-observer retention budgets, with explicit maximum retained bytes/frames, drop/eviction accounting and cancellation. Optional Waterfall/Video Player presentation should normally skip frames instead of blocking the functional graph. Treat authoritative recording and loss-intolerant consumers as a distinct, documented backpressure/failure profile. Test stalled viewers and producer progress with a tiny pool.

### F3 — HIGH — Do not byte-copy owning array handles
**Confirmed code constraint.** `s7core/include/s7/element.hpp` (ports and typed binding), `s7core/include/s7/runtime.hpp` (32-byte staging/snapshot), `s7core/src/runtime.cpp` (memcpy of native outputs) and `s7core/src/compiler.cpp` (input pointer binding) are optimized for scalar storage.

**Fix:** Add separate handle-aware array slots, input bindings and rollback/staging. Inputs bind to **stable source slots**, not movable payload addresses; leases are never `memcpy`-snapshotted. Prove source -> pass-through -> dual-consumer compilation and late-fault recovery before public vector registration. Preserve the scalar hot path.

### F4 — HIGH — Set a plugin ABI/API policy before invasive headers
**Confirmed code constraint; future compatibility risk.** `s7core/include/s7/element_plugin.hpp` declares plugin API v1; `s7runtime/src/plugin_loader.cpp` checks it. C++ object layout/virtual interface changes to `Element`, `Port` or metadata can invalidate existing compiled plugins even if scalar source code still builds. Project is pre-MVP, so a deliberate breaking change is permitted, but must be explicit.

**Fix:** Choose either demonstrable ABI preservation or a version increment with clear old-binary rejection and installed SDK/standalone-plugin tests. Do not confuse source compatibility and binary compatibility.

### F5 — HIGH — Audit existing FFT implementations before AV10
**Confirmed source finding; duplication risk.** `s7spectral/include/s7spectral/analysis.hpp` provides a portable in-place radix-2 C64 `Transform`; `s7spectral/src/fft_element.cpp` provides a scalar-input FFTW-backed peak-only `WindowedFFT`; `s7core/src/convolution.cpp` includes another portable transform; `s7studio/src/analysis/fft.cpp` has a separate FFTW interface. `s7spectral` is deliberately built as an out-of-tree SDK plugin.

**Fix:** Determine reuse/linkage/packaging up front. AV10's minimum C32 input/output cannot call the existing C64 transform without a conversion/scratch policy or a native C32 specialization. Preserve scalar WindowedFFT; do not introduce an unnecessary additional FFT implementation. Test impulse, DC, signed-bin complex tones, inverse convention, normalization and a separate independent DFT oracle.

### F6 — HIGH — Scope nested schemas as flattened vs standalone
**Confirmed distinction.** [ADR-024](../../doc/S7_Architectural_Decisions.md) and `s7core/src/flatten.cpp`/`compiler.cpp` flatten nested schemas and eliminate SchemaPort execution. Standalone `Schema::step()` still has F64/C64 exposed-state/proxy constraints in `s7core/src/schema.cpp` (around 54–65 and 229–238).

**Fix:** Require shape-preserving exposed ports through **flattened compiled schemas** in 0.2.0; separately declare standalone array-valued Schema execution unsupported/deferred unless an explicit proxy/storage contract is approved. Test inline vs nested output equivalence, port aliases, mismatches and feedback through nested memory.

### F7 — HIGH — Validate array observer port types and shapes
**Confirmed code constraint.** `s7studio/src/diagram/connection_planner.cpp:86–109` bypasses type checking for observer inputs. New viewers cannot accept arbitrary signal types: Waterfall requires 1D spectrum/PSD with metadata, Video Player requires supported U8 rank-2 or rank-3 image shape/layout.

**Fix:** Introduce descriptor-aware observer connection checks and authoritative runtime validation. Update `element_ports.cpp`, observer metadata, type labels and the catalog. Invalid viewer wiring should fail in Studio, not only on first rendered frame.

### F8 — HIGH — Limit initial shape inference to concrete equality
**Confirmed current limitation; planning risk.** `s7core/src/type_inference.cpp:126–151` resolves scalar numeric variables and defaults some to F64/C64; `element_metadata.hpp` and `element_catalog.cpp` currently have no shape descriptor path.

**Fix:** In 0.2.0 resolve concrete element-configured shapes and require exact rank/extent/type compatibility; reject unsupported ambiguity, symbolic dimensions, broadcasting or reshaping. Include the full metadata JSON -> registry -> catalog -> schema -> compiler -> Studio pipeline. Preserve existing scalar metadata and old saved diagrams.

### F9 — HIGH — Specify data cadence and sample provenance
**Architectural risk.** [ADR-022](../../doc/S7_Architectural_Decisions.md) gives rate domains a rational model-time schedule. A C32[N] block is a single logical array evaluation, not N graph steps. Physical sample rate, block duration, first sample index, frame sequence, video PTS and graph model time are separate.

**Fix:** Define a small optional provenance envelope (where meaningful: sample-rate rational, first sample index, frame/row sequence, discontinuity, optional source time) and propagation/reset behavior. Do not infer physical timestamps or media frame rate from `step_count()`; do not silently reuse scalar Resampler as a block resampler. Validate Waterfall row gaps and Video Player frame IDs.

### F10 — MEDIUM — Avoid persisting incidental physical layout as signal identity
**Specification ambiguity.** `ARRAY_SIGNALS_ZERO_COPY_DESIGN.md` distinguishes logical shape and physical layout, then proposes a versioned serialized shape/layout contract.

**Fix:** Persist logical type/rank/extents plus required format/capabilities, not every incidental CPU pointer offset/padding choice. Exact row stride or memory domain is a checked storage capability; a conversion/materialization is explicit when needed. Start with contiguous DSP and narrowly supported image layout.

### F11 — MEDIUM — Reuse transport pool ideas, not its API unchanged
**Confirmed code opportunity.** `s7transport/include/s7/frame_transport.hpp` has immutable shared leases and budgets; `s7transport/src/frame_transport.cpp:166–176` acquires storage by copying bytes. Its transport-oriented API is not a writable array producer API.

**Fix:** Reuse tested accounting/lifetime concepts, add a headless writable-unpublished candidate acquisition path. Verify capacity, stale-generation and retained-lifetime behavior with sanitizers.

### F12 — MEDIUM — Keep scalar and array observation delivery separate
**Confirmed code constraint.** `s7observe/include/s7/observe.hpp` samples up to 32 bytes of scalar storage; `s7studio/src/observation/observation_source.hpp` records doubles; `s7studio/src/local_schema_runner.cpp:295–320` converts samples to scalar display values.

**Fix:** Add an independent, bounded, lease-backed array observation service/mailbox with committed-snapshot consistency and explicit viewer skips. Retain current scalar ObservationService, Scope and journal unchanged. Measure observation retention and cross-thread handoffs.

## Other inventory additions

- Add `s7core/src/metadata_json.cpp`, `s7core/src/element_catalog.cpp`, catalog clients and installed SDK/plugin ABI checks explicitly to the migration checklist.
- Check `s7core/include/s7/validation.hpp`'s `SchemaPort` placeholder storage: a shape-aware logical port must forward a compiled slot without treating the placeholder scalar bytes as an actual array.
- Use existing deterministic image patterns and comparisons from `s7tests/video_stream_spike.cpp`, **without** conflating its old per-pixel stepping with the new whole-frame execution.
- Verify packaging of the out-of-tree spectral plugin, observer registry/metadata, saved examples and platform-specific optional dependencies.
- Test at least one element with a **mixed scalar status port and array output port**.
- Provide a source-by-source copy/allocation ledger: wire/fan-out/forwarding/handle rollback may be zero payload-copy; FFT/Gain/PSD generate output; Qt presentation, PDF, transport and hardware adapters can copy or upload.
- Define explicit unsupported cases for array-valued implicit/coupled numerical trials before execution.

## Codex implementation plan review (AV00–AV18)

**Five hours per session is a hard timebox, not an assurance that each existing card fits.** Split at tested, reviewable boundaries; no partially wired public feature should be advertised as working.

| Existing card | Recommendation |
| --- | --- |
| AV00 | Keep; freeze exact commit, source/test baselines, publication model, pool admission, ABI decision, scalar performance comparison. |
| AV01 | Keep; validated shape/layout, overflow/bounds and initial supported contiguous/padded profiles. |
| AV02 | Expand to metadata JSON, registry, catalog output, serialized shape version and ABI policy. |
| AV03 | Narrow to concrete type/rank/extents matching, clear negative tests; no symbolic inference. |
| AV04 | May run after AV01 concurrently with metadata/validation work; add producer-vs-viewer exhaustion policy tests. |
| AV05 | **Split:** stable slot/binding API; then actual compiled headless source -> pass-through -> consumer/fan-out. |
| AV06 | **Split:** direct candidate/commit machinery; then late-fault/retained-observer rollback tests. |
| AV07 | **Split:** causal-memory handle staging; then VectorDelay public element/feedback fixtures. |
| AV08 | **Split:** flattened exposed-port forwarding; then saved-diagram/schema round-trip and standalone exclusion. |
| AV09 | Introduce a minimal internal fixture early; add public Constant/PassThrough/Gain and canonical diagrams after runtime safety gates. |
| AV10 | Reuse/SDK/backend decision first, independent FFT oracle and C32 scratch/copy accounting. |
| AV11 | Specify PSD/window/normalization/bin order/provenance before coding. |
| AV12 | Move earlier after array runtime + fixture support; **does not depend on Waterfall**. |
| AV13 | **Split:** headless retained-array observation; then bounded Studio mailbox/routing with slow-viewer test. |
| AV14 | **Split:** Studio descriptor-aware pin/connection validation; then viewer registry/bindings lifecycle. |
| AV15 | **Split:** bounded deterministic waterfall data->image mapping; then controls, printing integration, GUI visual verification. |
| AV16 | **Split:** exact grayscale/RGB renderer; then presentation controls, lifecycle, skipped-frame accounting and GUI visual verification. |
| AV17 | **Split:** P08/P09 coherent snapshots, package/install qualification, release notes; do not absorb existing P01–P07 implementation into one card. |
| AV18 | **Split:** candidate-specific headless/numerical, memory/performance, Studio/visual, SDK/package test matrices. Do not claim G1–G4 closed solely from AV tasks. |

**Suggested dependency order:**

1. AV00 -> AV01.
2. AV02/AV03 (metadata/shape) and AV04 (pool/leases) can progress independently once AV01 is stable.
3. Both converge at AV05 compiled binding -> AV06 direct commit/rollback. **Do not expose public vector elements before this safety gate.**
4. AV07 causal memory and AV08a flattened exposed ports; then AV09 minimal public elements and verified saved diagrams.
5. AV10/AV11 FFT/PSD and AV12 U8 frame source can proceed in separate branches after a stable array runtime.
6. AV13a headless observation may begin after AV06; AV13b/AV14 Studio routing follows headless observation and metadata.
7. AV15 Waterfall requires FFT/PSD + Studio array observation. AV16 Video Player requires U8 fixture + Studio array observation. They are **parallel branches**, not Waterfall -> Video dependency.
8. AV17 printing/packaging follows both viewers, then AV18 candidate-specific qualification.

Keep changes to shared core headers sequential or coordinated; do not merge ABI or publication half-features that compile but violate runtime semantics. Fix the existing workplan's visually misleading placement of AV12 after AV15.

## Revised 0.2.0 array slice

**Mandatory:** fixed-shape F32/C32 rank-1 and U8 rank-2/3 CPU arrays; explicit validated descriptors; stable compiled slots and lease-backed read-only fan-out; safe direct publication and whole-array VectorDelay; minimal source/PassThrough/Gain/FFT/PSD; flattened nested ports and persistence; bounded committed-array observation; basic Waterfall and grayscale/RGB Video Player; saved diagrams with independent numerical/pixel references; PDF/print for new viewers (P08/P09), scalar regressions, failure tests and installed-package evidence.

**Defer:** symbolic/dynamic shapes and broadcasting; generic tensor math; array coupled/implicit numerical solvers; broad array filters and block resampling; production media/hardware adapters, codecs, FFmpeg/GStreamer, network array transport; GPU/DMA-BUF and multi-plane media; full 2D FFT/field/PDE operations; automatic in-place/COW optimization; standalone array-valued Schema execution unless separately accepted.

**0.2.0 integration:** [RELEASE_0.2.0_WORKPLAN.md](RELEASE_0.2.0_WORKPLAN.md) includes independent G1–G4, educational, distributed and printing gates. Existing P01–P07 are a separate G4/P workstream, not tasks silently bundled into AV17. All release evidence must be tied to one candidate commit and include actual commands/results or explicit gaps.

## Test additions needed before claiming correctness

1. Shape/type/rank/stride/size-overflow and explicit unsupported array numerical-trial negatives.
2. Exact fan-out payload identity and **instrumented payload copy counts** at each claimed zero-copy boundary.
3. Old lease retained through producer advancement, delay and rollback; memory safety under stale generation/reuse, runtime destruction, cancellation and failed retry.
4. Fault injection at late direct stage, first/last memory stage, consecutive retries and multi-rate boundary; verify committed snapshots and prior staging. Never assume private-state rollback.
5. Tiny-pool producer with slow/paused/closed observers; explicit non-invasive retention policy, bounded bytes, skip counters, no data tearing or use-after-free.
6. Independent DFT and known FFT/PSD spectra (DC, impulse, signed frequency, two tones, windowed signal, zero-input/dB floor, bin order, normalization and power conservation with stated tolerances). Comparing two calls to the same FFT backend is not independent evidence.
7. Exact pixel oracle for grayscale/RGB and padded-stride cases when supported; deterministic GUI snapshot review for color, aspect, axes, sequence discontinuities and pause/close/reopen/rebind.
8. Actual scalar benchmark baselines on matching build/host configurations; steady-state allocation counts in the declared reference profile; sanitizer/TSan checks for lifetime and mailbox.
9. Plugin API rejection/SDK compatibility and installed/relocated package checks; actual PDF content and rendering review, not only nonempty files.

## Decisions required before implementation

**D1. Plugin ABI:** retain binary ABI by design or increment version and reject incompatible plugins?  
**D2. Publication state machine:** exactly when do direct candidate, committed output and causal staged handles change; what rolls back per domain?  
**D3. Pool/observer policy:** what memory is execution-reserved, what may observers retain, and what is the failure/drop policy?  
**D4. Shape/format:** persist logical shape and compatible capabilities; which CPU layouts are mandatory?  
**D5. FFT/PSD:** backend, C32 vs C64, scratch/copy policy, normalization, bin order and metadata.  
**D6. Provenance:** block index, sample rate, sample position, gaps, optional PTS and propagation.  
**D7. Standalone Schema:** defer its array exposure, or explicitly implement a separate proxy contract?

**Recommended next action:** discuss and accept these decisions, then revise `ARRAY_SIGNALS_ZERO_COPY_DESIGN.md`, `ARRAY_VECTOR_MIGRATION_INVENTORY.md`, `ARRAY_VECTOR_CODEX_WORKPLAN.md`, `RELEASE_0.2.0_WORKPLAN.md` and related release/printing language in a separate documentation pass. **Do not begin AV01 or claim the review's proposed remedies are already accepted.**
