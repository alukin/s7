# S7 array-valued signals, zero-copy bindings and minimal viewers

**Design notes — maintainer-approved scope. D1 compatibility policy, D2 publication, D3 buffer ownership and D4 descriptor foundation, D6 provenance semantics and D7 nested array scope accepted. D5 defines the initial FFT and PSD.**
The [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) tracks release acceptance. These notes define the reusable architecture and the first release slice. The migration inventory and session plan describe the affected code and delivery order. Existing scalar elements stay intact. Vector behavior uses new elements.

## 1. Motivation and boundaries

The same logical array signal must support IQ/SDR sample blocks and FFT spectra, audio blocks and convolution, whole video frames, and future spatial simulation fields with differential operators. S7 must not introduce an SDR-only vector wire or a video-only second runtime.

S7 already has 27 scalar numeric signal types, scalar native-storage Port bindings, a scalar WindowedFFT plugin that reports peak values, independent Studio spectral analysis, frame network transport that batches scalar frames, reusable C64 FirConvolution/FftConvolution processors, and whole-frame video/FFmpeg experiments. These are starting points, **not** a present implementation of general array-valued ports. See [element guide](ELEMENT_DEV_GUIDE.md), [frame elements](FRAME_ELEMENTS.md), [convolution](CONVOLUTION.md), [Studio spectrum](STUDIO_SPECTRUM.md), and [video design](VIDEO_IO_FRAME_DESIGN.md).

Invariants:
- **One causal model:** a connection expresses a dependency; the scheduler enforces it. One Element::step() transforms one set of logical input values, scalar or shaped. A sample block is not secretly thousands of graph steps. Physical/model dt, block cadence, underlying sample positions, video PTS and spatial grid spacing are different quantities.
- **Scalar execution:** retain native rank-zero storage and optimized scalar evaluation. Scalar numerical behavior and dispatch cost are regression targets.
- **Zero-copy along local wires:** bindings/fan-out pass views and stable buffer handles, not entire payload copies. A transform may need separate preallocated output memory. "Zero-copy" is claimed only across proven compatible boundaries.
- **Observation is external:** viewers consume immutable published values or snapshots; no rendering/FFT for visualization participates in the functional graph or changes causal execution. A true computational FFT may be an element; display-only spectrum processing may be an observation processor.
- **Fault honesty:** no partial array publication, no invented frames/sample positions, no implicit pixel-format conversion, resampling, graph delay or hidden device activation.
- **Bounded resources:** preallocate/admit storage and queue capacity before running; record copy/transfer sites, memory budget, retained leases, skips and faults.

## 2. Logical signal type versus storage

Treat the logical signal contract as (numeric element type, rank, extents, optional domain interpretation), where **rank zero is the current scalar**. Shapes are part of the declared/compiled port contract. For a first implementation use concrete positive extents; symbolic dimension unification and bounded dynamic extents follow after independent tests.

### D4 — Fixed shape, CPU layout and domain semantics

The maintainer approved a rank-generic numeric descriptor on 2026-11-26. The first API, [`ArrayDescriptor` and `DenseCpuArrayView`](../s7core/include/s7/array_descriptor.hpp), separates the numeric type and positive extents from the physical backing range. The descriptor accepts all S7 numeric types, checks element and byte counts before allocating extent metadata, and represents rank zero as one native value. The borrowed, read-only CPU view covers rank one and above. It validates the actual address alignment, offset and byte range and derives compact row-major strides from the extents. Its descriptor and storage must remain valid for the lifetime of the view. Owning storage and publication are later cards.

DSP, images and spatial fields share this substrate. Port metadata will express typed domain requirements separately, such as RGB channel ordering or a spatial grid and sample placement. Match those requirements when compiling a connection. Equal extents alone are insufficient for comparing fields on distinct grids. A cell-centered field and two face-centered components can use separate ports over the same grid with distinct shapes. Grid or mesh topology and degree-of-freedom mapping belong to a later field contract. Padding, halo interiors and other storage layouts can use additional validated views while retaining the same logical shape.

AV01 exercises spatial F64 rank-two and rank-four arrays alongside DSP and video. After whole-array causal memory works, validate a small two-dimensional heat-equation graph against an independent discrete recurrence. Give the spatial operator an explicit boundary rule and let the field integrator stage the next state using the model's `dt`. Test incompatible grid placement when typed spatial ports are available.

### Example shapes

| Value | Numeric type | Logical shape |
| --- | --- | --- |
| Scalar gain | F64 | [] |
| SDR block | C32 | [4096] |
| Stereo audio block | F32 | [1024, 2] |
| Spectrum | F32 | [4096] |
| Grayscale frame | U8 | [height, width] |
| Interleaved RGB frame | U8 | [height, width, 3] |
| 2D field | F64 | [ny, nx] |
| Vector field | F64 | [nz, ny, nx, 3] |

A 3-component dimension is *not* intrinsically RGB or a spatial vector. Do not encode media or spatial semantics into SignalType. Type conversions remain explicit. Future shape rules must distinguish elementwise, broadcasting, reduction, reshape, FFT and grid-staggered operations and reject unsupported combinations at compilation.

The logical shape is **not** the physical layout. A separate view descriptor includes dimensions, strides, offset, alignment, accessible bounds and memory domain. Its validation must be overflow-safe. Video may need multiple differently shaped planes and color/transfer/range metadata; spatial grids may need staggered components and halo topology. Build the descriptor/lifetime API so these are possible without redesign, but do not require all formats in 0.2.0.

A transport packet, a sample frame and a graph array are different concepts. Keep sample/frame sequence, continuity, clock and provenance metadata in an optional envelope that does not force a wall-clock timestamp onto numerical arrays.

## 3. Core representation and execution strategy

Expose **one logical port and wire system** but keep **two storage classes** internally:

1. Scalars keep existing fixed native storage, typed compile-time bindings, fast scalar execution and the current snapshots.
2. Arrays use a stable port slot/descriptor holding an immutable published view plus a buffer lease/owner. Compiled input bindings point to slots, not to potentially movable payload bytes. The slot is stable across a swap. Provide typed, bounded read-only ArrayView access and an explicit writable output acquisition path.

Sketch only (not a promised C++ API):

~~~cpp
struct Shape { /* rank and extents, validated at graph compile */ };
struct ArrayLayout { /* offset, strides, planes/memory domain as supported */ };
struct BufferLease { /* owner, lifetime, access/synchronization */ };
struct ArrayValue { /* numeric type, shape, layout, lease, metadata */ };
struct SignalDescriptor { /* scalar or array, numeric type, dimensions */ };
~~~

Keep numeric type independent of rank and shape. Use a typed handle-aware publication and snapshot path for owning C++ objects. Measure scalar correctness and dispatch cost during the transition. The D1 policy permits replacing the current API, ABI and formats.

### Publication, direct feedthrough and causal memory

- A direct-feedthrough element may expose its **candidate** array to downstream direct elements in the *same* step. This is not early external publication: observers and durable output records see only successfully committed results.
- At the beginning of an evaluation, retain the prior published handles. A failed step restores the prior published descriptors/leases without copying the data payload; candidate buffers are returned only when no reader still owns them.
- Causal-memory elements calculate a candidate for the **next** publication boundary. Preserve the present semantics for Delay/integrators, including initialized memory and feedback-cycle validation. Swapping two preallocated handles is not a new implicit causal delay.
- Do not mutate shared/published input data in place. In-place updates require a proven exclusive writable lease, no observer/branch/rollback reader and no numerical-old-state dependency; otherwise write to a separate preallocated candidate. A 2D stencil normally needs ping-pong state buffers.
- The current implicit/coupled numerical-trial machinery has scalar byte snapshots and replay rules. Initially **reject array-valued participation in coupled/trial regions** unless a separate tested handle-aware trial/rollback contract is implemented. This is not permission to weaken scalar solvers or ADR-021.
- Only committed results may be submitted to observation. A GUI skip/drop is presentation loss and never a silent graph-frame loss.

### Buffer lifetime and local zero-copy

Prepare/admit fixed-size pools or preallocated element outputs before execution. Synchronous downstream inputs can borrow a view through compiled lifetime guarantees without an atomic reference increment on every connection. A retained asynchronous observer/recorder or hardware/media adapter needs an explicit lease and pool charge.

Pass-through, fan-out and compatible crop/slice/transpose should not copy payload bytes. FFT, convolution, color conversion and most field updates **compute new output data**; that is not a wire-copy. Contiguous/strided CPU view and native memory-domain transitions must be capability checked, not presumed. DMA-BUF, GPU, AVFrame, GstBuffer, synchronization fences, multi-plane layouts and transfers are future adapter integrations requiring individual measured claims.

Avoid per-step heap allocation and unbounded retained observer frames. Make pool exhaustion, stale generations, cancellation and an unavailable memory mapping explicit.

## 4. Shape, schema and Studio semantics

AV02 uses a `shape` array of positive integer extents alongside the fixed numeric `type` in element port metadata, catalog entries and schema `exposed_inputs`/`exposed_outputs`. For example, an exposed field can use `{"name":"field","type":"F64","element":"source","port":0,"shape":[20,30]}`. Omit `shape` for a native scalar. An empty array, unknown extent, numeric type rule on a shaped port, or overflowing native byte size is an error. Array declarations are available for inspection. Execution and domain-specific formats are covered by later cards.

AV03 validates fixed-count element declarations using their port metadata. Array connections require the same numeric type, rank and extents, and scalar pins on mixed elements use ordinary type inference. Declaration-only elements are direct feedthrough, so they cannot close a feedback cycle. A numerical trial cannot replay a declaration with array ports, even when its derivative signal is scalar. The compiler and deployment preflight reject declaration-only arrays before binding. Later cards cover executable array slots, array causal memory, semantic formats and shaped nested boundaries.

AV05a introduces [stable array port slots](../s7core/include/s7/array_port.hpp) for internal elements. Array ports use the same logical input and output positions as scalar ports, with a descriptor and a null scalar pointer. Use `read_input<T>()` only for scalar positions and the typed array binding for array positions. An input binds to its producer's slot after an exact descriptor check. A synchronous view borrows the slot's current lease until replacement. An asynchronous reader can hold an admitted immutable lease across replacement. Slot exchange transfers ownership without copying payload bytes. AV05b binds direct array ports through the experimental C++ compiler entry point. Normal compilation and scalar observation reject array execution.

- Add optional shape rules alongside existing numeric PortSpec/type inference. A scalar has shape []; a shaped output carries concrete extents in the 0.2.0 slice. Reject overflow, rank/extent mismatch, invalid strides/planes and unsupported element types before running. Do not silently broadcast or flatten.
- Serialize logical type, rank, extents and required format/capabilities in the canonical schema representation. Keep incidental padding, offsets, pointers and storage identity in runtime views. Update bundled diagrams as the format changes. Local array connections are the 0.2.0 target.
- Show one wire per **whole array**, with a label such as C32[1024] or U8[240,320,3], and validation details in Studio. It is neither 1024 scalar wires nor an implicit network packet bundle.
- Keep execution cadence distinct from underlying sample rate and frame PTS. The existing scalar Resampler does not automatically become a block rate converter. A Delay of array-valued signals delays an entire array value, not one sample inside it.
- Observation and viewer configuration is read-only diagram presentation metadata, not executable elements in the functional graph.

## 5. Mandatory **0.2.0 minimal vertical slice** (new scope)

This is intentionally smaller than the general architecture. Implement and qualify:

**V1 — Local array ports/wires and runtime:** concrete fixed shapes, rank 1 as the primary vector case, and minimal rank 2/3 U8 frames for viewer verification; support at least F32, C32 and U8. F64/C64 are welcome if covered by tests, not a reason to block the minimal slice. CPU memory, bounded preallocated buffers, read-only fan-out, a pass-through element, deterministic synthetic/recorded array source, one numeric transform, type/shape validation, headless execution, nested-schema/exposed-port regression, and handle-aware commit/rollback. Existing scalar paths must remain performant and correct.

**V2 — Spectrum/waterfall path:** one FFT/PSD path producing a **whole array of bins**, not just peak scalar results. Reuse existing spectral code where correct; require complex-IQ synthetic/recorded reference fixtures, exact frequency ordering (shifted/unshifted choice), FFT window/normalization, bin numbers and sample/frame provenance. A basic Studio **Waterfall** viewer consumes successive committed 1D spectra, maps magnitude/power to a time-frequency image, has frequency and history axes, configurable range/palette, a bounded history and visible missing-frame indicators. For a paused or finished run, the retained source can be inspected without affecting execution. Do not make real SDR hardware mandatory for this gate.

**V3 — Basic Video Player viewer:** a Studio observation client for **whole decoded U8 grayscale and interleaved RGB** frames from deterministic generated or recorded array-valued graph output. Display changing frames, fit-to-window, pause/resume presentation, latest frame/step/sequence and resolution/format indicator, bounded queue with explicit skipped presentations, clear reopen/close/rebind lifecycle. Verify pixel exactness on synthetic frames, including a nontrivial row stride when supported. The **viewer does not decode files, run codecs, own a camera or form a functional graph element**. If no production video source exists, provide a deterministic frame producer/fixture in the normal headless/Studio graph path. Frame-by-frame replay/seek is optional unless supported by that source; playback UI must not claim arbitrary seeking of uncaptured live data.

**V4 — Observation/print/package:** both new viewers use the existing independent Observation subsystem, can be attached/detached without changing outputs, survive save/reopen, appear in Studio help and supported packages, and provide coherent PDF/printer snapshot output under [G4/P](STUDIO_PRINTING_0.2.0.md). GUI tests need actual reference-pixel/image/axis verification, not merely a nonblank screenshot.

Acceptance includes synthetic and recorded input (no auto hardware), independent numerical FFT/PSD oracle, at least one checked end-to-end saved S7 diagram for each viewer, mismatched shape/type rejection, frame ordering and discontinuity, repeated fan-out with the **same buffer identity** and zero payload-copy instrumentation, fault-injected late direct element and causal-memory restoration, retained-view lifetime, small bounded pool exhaustion, zero steady-state allocations in selected reference paths, Studio controls and image comparisons, and full scalar regression. Print snapshots must show coherent selected frames and waterfall scales.

**Out of 0.2.0:** production VideoSource/VideoSink, device/camera activation, FFmpeg/GStreamer integration, codec playback, remote array-profile negotiation/transmission, generic dynamically reshaped tensors, multi-plane video, GPU/DMA-BUF zero-copy, full 2D FFT/convolution, audio block effects, spatial grid solvers and zero-copy across every external boundary. These stay in [0.3.0](RELEASE_0.3.0_WORKPLAN.md) or later and must not be represented as delivered by the basic viewers.

The initial implementation and qualification is a **mandatory new 0.2.0 scope amendment**, not an assertion that it exists. Any variation of the slice must be recorded in the release work plan.

### Registered vector elements

Saved diagrams can use these rank-one vector elements (metadata in
`s7core/resources/element_metadata.json`, registration in
`s7core/src/builtin_array_registry.cpp`):

| Element | Behavior |
|---|---|
| `VectorConstant` | Publishes a fixed vector (any numeric type; complex values as `[re, im]`) |
| `VectorPassThrough` | Forwards a vector and its source provenance |
| `VectorGain` | Multiplies each component by a native gain (F32, F64, C32, C64) |
| `VectorAdd`, `VectorMultiply` | Elementwise sum or product of two vectors of one type and length (F32, F64, C32, C64); no source provenance is claimed |
| `VectorDelay` | Whole-vector causal memory: `initial_values` (zeros when empty) first, then the previous step's input with its provenance; closes vector feedback loops |
| `PackVector`, `UnpackVector` | Scalars to one vector and back, in the same step |
| `TestPatternFrame` | Deterministic U8 video frames |
| `VectorFFT`, `VectorPSD` (s7spectral) | Complex vector FFT and power spectral density |

An output held by a `VectorDelay` stays leased for one more step, so the
producing element needs three execution buffers (the default for `VectorAdd`
and `VectorMultiply`). Not implemented: vector integrators and an element
that frames a scalar sample stream into blocks (a rate change that needs its
own design).

## 6. Forward-compatible roadmap and tests

- **0.3.0:** audio block convolution (existing C64 processor refactoring where appropriate), real hardware/audio/SDR/whole-frame video and production GStreamer adapters, optional FFmpeg adapter, basic image transforms, broader buffer/plane/stride support, performance and clock provenance, use/reuse 0.2.0 viewers. Keep video and audio sample clocks explicit.
- **Later:** FFT2D/3D, general strided tensor transforms/convolution, tiled processing, accelerated CPU/GPU memory domains, spatial grid geometry and differential operators (eventual spatial S7 goal). A vector field is not automatically an RGB frame or a collocated Yee-grid field.
- **Distributed:** existing NetFrameSource/Sink and packet wire profiles are not equivalent to a general tensor transport. Specify schema/shape/layout/capability, partial progress, clock, buffer ownership, backpressure and boundaries separately before claiming remote array support.
- **Testing principle:** compare saved diagrams against independent references, examine numerical and presentation results, inject failures and resource pressure, and record the actual candidate SHA, tested profiles, exclusions and limits.

## 7. Independent-review architectural reconciliation (2026-11-23)

The independent review documents confirmed code constraints separately from proposed remedies. This section records the publication and ownership contracts alongside implementation guidance. Preserve [ADR-021](../../doc/S7_Architectural_Decisions.md#adr-021), [ADR-022](../../doc/S7_Architectural_Decisions.md#adr-022), [ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024) and the optimized scalar path.

### Publication, rollback and ownership (F1–F4, F11–F12)

Array slots must distinguish **previous externally committed handles**, **current direct-feedthrough candidates** visible only to downstream graph evaluation, **previous valid causal-memory staging** eligible for the present evaluation, and **next staged causal-memory candidates** for a later evaluation. Compiled array inputs bind to **stable producer slots**, not movable payload addresses; RAII leases must never be `memcpy`-snapshotted in scalar `MemoryOutput`, `CausalOutput`, `TrialRegion` or observation storage. Acquire writable **unpublished** storage without modifying a shared/published/rollback-protected payload. Handle swaps, fan-out, pass-through, staging and rollback should incur no payload-byte copies; FFT, Gain, UI rasterization, transport or layout materialization can compute/copy into separate storage. Maintain a per-boundary copy/allocation ledger.

Before evaluation, retain the previous external snapshot and valid memory staging. Direct elements can consume their current candidates in causal order; causal-memory elements stage a candidate for the *next* publication boundary. On success publish a coherent set of output handles and advance the domain's committed position/staging; on fault restore the previous externally visible handles and valid staging and do **not** advance that domain's step. Retained observers still see the old immutable bytes. The D2 commit and resource reservation contract requires tests for late direct, early/late memory, multiple-output, mixed scalar/array, consecutive retry and retained observer faults.

**The atomic boundary is one rate-domain evaluation, not an entire multi-rate run.** If another domain already completed its step, it may remain advanced when a later domain fails (ADR-022). Neither private element state nor irrevocable I/O or network output is rolled back by ADR-021. When replay is unsafe, latch/report an explicit fault. Array participation in implicit/coupled numerical trials is rejected **before execution**, pending a separate reviewed trial contract.

### D2 — Per-domain publication and retry

**Accepted.** The unit of publication is one successful evaluation of one rate domain. Compiled inputs bind to stable source slots. Each array output has separate graph-visible and committed state so a graph read and an external observation use the appropriate lease. All graph slots stay on the evaluation thread. Optional asynchronous readers acquire an admitted immutable lease from a committed result under D3.

| Phase | Graph-visible values | Committed state |
| --- | --- | --- |
| Begin an attempt | Make the prior valid causal-memory staging available to the graph. On the first attempt, use the element's initialized memory value. | Keep the last successful outputs and position. |
| Evaluate direct elements | In causal order, downstream elements see sealed candidates from this attempt. A producer can forward an existing immutable lease without copying. An explicit hold uses its preceding value if initialized. | Keep the last successful outputs and position. |
| Evaluate causal memory | Memory elements read the same current memory values and the direct results. Each writes separate staging for the next evaluation. | Keep the prior valid staging and last successful outputs. |
| Commit | Promote current direct results and the memory values used in this evaluation. Save successful next staging for the following attempt. | Expose the complete domain result and advance its position once. |
| Fail before commit | Restore graph slots to their last successful values and discard this attempt's candidates and next staging. | Preserve prior valid staging, committed outputs and position. Retained reads still point to their original bytes. |

Preallocate bookkeeping and admit the execution reserve before running. Acquire required candidate capacity and validate results before the commit point. Commit uses handle transfers and preallocated state, with no fallible allocation, output validation or consumer callback. Observer delivery happens after commit. A rejected optional admission counts as a presentation skip. It cannot undo or delay a domain commit. On failure, restore domain state before reporting the error or collecting diagnostic details so reporting cannot mask incomplete rollback.

A retry of an unfinished outer step resumes from each domain's committed position. In both clocked and independent unclocked graphs, a domain that already met this outer step's target is skipped on retry. The outer step advances after every domain meets its target. A snapshot that combines domains must identify each contributing domain position. Private element state and irreversible I/O retain the ADR-029 retry limits. A failure while delivering a scalar sample after an upstream commit latches the original fault because a receiver may already have changed state. Subsequent step calls report that fault, and resuming execution requires a fresh graph. Array wires in this slice stay local and within a single rate domain. Rate conversion and cross-domain array links need their own contract.

AV05b exercises an internal direct source, pass-through and two consumers through the compiler and runtime. AV06a adds candidate and committed leases to direct slots. The runtime prepares the per-domain slot list before stepping. Each producer acquires and validates its candidate before commit. Experimental `ObservationService::try_retain_array_output` samples a committed lease with its rate and domain position. Admission runs on the stepping thread, and the retained lease can be handed to another thread. AV06b qualifies the experimental compiler entry point for internal direct-array fixtures. AV07a/AV07b add and verify array causal-memory staging and delay.

A bounded producer pool needs an **execution-critical reserve independent of optional viewers**. A slow/paused Waterfall or Video Player cannot hold the last available producer candidate. Define maximum retained bytes/frames, eviction/drop and discontinuity counters, admission, cancellation and recovery **before AV04** (D3). Optional observation is newest/bounded presentation with skips, never graph backpressure; authoritative/lossless recording is a separately admitted backpressure/failure profile. Synchronous borrowed views are compile-lifetime bounded; asynchronous clients and printing retain explicit charged immutable leases. Existing `s7transport` lease accounting can inform implementation, but `FrameTransport` copies incoming bytes and is not a writable-array producer API. Add a distinct committed-array Observation source/mailbox: do not feed arrays through scalar `ObservationSample::storage[32]`, scalar `ObservationSource::Sample::double`, Scope or journal.

### D3 — Pool admission and optional retention

**Accepted.** A producer pool preallocates aligned, fixed-capacity CPU slots. Each pool declares an execution reserve and optional retention headroom. The configured slot count must cover both, and allocation charges the full slot capacity to the run's memory budget. The execution reserve covers every slot the graph may hold simultaneously, including writable candidates, published and staged values, rollback handles and downstream aliases. AV04 accepts the declared bound. The compiler derives or verifies bounds when it introduces graph bindings and publication.

A producer acquires exclusive writable storage without waiting for a slot. Sealing consumes that writable lease and returns an immutable read lease for the same bytes. Abandoning a candidate releases it. Synchronous graph readers borrow views from live graph leases. Optional asynchronous readers retain a buffer only after admission against explicit slot and byte caps. Charge the whole backing slot, including for a subview. Copies of a retained handle share its admission until the last copy releases it. The aggregate optional cap for each pool fits within its headroom. Failure to acquire execution storage is a resource fault. Failure to admit optional retention is a presentation outcome.

Live presentation keeps one pending frame per subscription and replaces it when newer data is admitted. Count admission failures and replaced frames separately from source discontinuities. A frame already handed to a reader stays valid until released. Printing uses a separate bounded admission and reports when it cannot retain the requested snapshot. Cancel or close stops new work and releases handles still owned by the queue or job. Outstanding reader handles remain readable; storage is recycled only after its final lease ends. An expired token cannot resolve to a newer occupant of the same slot.

Lossless array recording is a separately admitted opt-in policy. It uses bounded capacity and applies backpressure only at a safe runtime boundary. Exhaustion that cannot be resolved within the recording policy faults the recording or run explicitly. AV04 covers the core pool, leases and admission; integration with recording and runtime publication follows their own contracts.

The AV04 regression uses a tiny pool, deliberately holds an optional read lease, and repeatedly acquires and seals producer candidates. It checks bounded optional skips, immutable retained bytes, lease release and close behavior, explicit execution exhaustion, and zero payload copies between the writable candidate and retained readers.

[The AV04 CPU pool](../s7core/include/s7/array_pool.hpp) admits a shared payload budget plus separate execution and optional slot counts. The producer supplies every payload value before sealing. Execution exhaustion can signal an underestimated graph reserve even while optional capacity remains free. Optional admissions reserve their configured headroom until the admission and all its retained handles are released. Observer and printing integration hands out admitted handles. Graph leases stay inside execution so a presentation reader cannot retain a slot outside its budget.

### Shapes, nested schemas, Studio and provenance (F5–F10)

Require **exact numeric type, concrete positive rank/extents and required domain semantics** at compilation. Reject symbolic/dynamic ambiguity, scalar-vector coercion, broadcast, implicit reshape/conversion, overflowed byte calculations, invalid offset/strides/bounds and unsupported layouts. Initial release elements use F32[N]/C32[N], U8[H,W] grayscale and U8[H,W,3] interleaved RGB with explicit image format on the generic D4 foundation. Resolve and validate metadata through `metadata_json.cpp`, registry, `element_catalog.cpp`, schema serialization, type inference, compiler and Studio. Use one canonical format for current scalar and array ports. Persist logical descriptors and required storage capabilities. DSP is contiguous. U8 row padding requires a declared capability and independent tests. Layout conversion uses explicit adapters with measured copies.

**Flattened compiled nested schemas** must forward shape-preserving slots under ADR-024, not treat `SchemaPort` scalar placeholders as array payloads. **D7 defers standalone array-valued `Schema::step()`**; its F64/C64 proxy limitations must not be silently claimed fixed. Test inline/nested equivalence, feedback through array memory, exposed-port aliases and saved-diagram round-trip. Studio's `connection_planner.cpp` currently bypasses observer-input type checks: add descriptor-aware editor checking and authoritative runtime rejection. Waterfall accepts only a supported rank-1 spectrum/PSD format with appropriate axis metadata; Video Player accepts only supported U8 grayscale/RGB shape/layout. Include viewer registry, labels, settings, print and installed resources.

One C32[N] value is **one causal `step()`**, not N graph steps. Distinguish graph **rational model-time rate-domain cadence** (ADR-022), physical sample rate, valid sample count and block duration, first physical sample index, block/frame/row sequence, optional source clock/timestamp and video PTS. Define optional provenance fields, absence, forwarding, reset, discontinuity and delay behavior **per producer/transform** before AV11/AV12 (D6). Never derive physical samples/PTS from graph `step_count()` or use scalar `Resampler` as a hidden in-block resampler. Missing source sample rate means unknown physical frequency axis; Waterfall gaps and Video Player skipped-frame IDs must be visible.

<a id="d6-proposal--array-provenance-and-whole-array-delay"></a>

### D6 — Array provenance and whole-array delay

**Accepted after independent review.** An array value carries an immutable payload lease and optional source provenance. Stage, publish, roll back and retain both as one value. Two source occurrences can share a payload buffer and still have distinct provenance. Keep provenance outside the array descriptor and storage layout. Report the receiving graph's rate and committed domain position separately. Numerical arrays and spatial fields may leave provenance absent.

- **Sample blocks.** A producer can report its physical sample rate and the source interval identified by first sample index and valid sample count. The count describes the source interval. A derived output such as an FFT may have a different array extent and describes its frequency axis separately.
- **Frames and continuity.** Frame sequence belongs to a source and continuity epoch. A producer starts a new epoch after a restart or seek that resets its indexing. A source-reported gap is distinct from frames skipped by a viewer. Optional presentation timestamps use the source's declared clock and time base. Unknown fields stay absent.
- **Forwarding.** Fan-out and pass-through preserve the lease and provenance. A whole-array `VectorDelay` publishes its initializer with absent source provenance on its first successful evaluation. Each subsequent commit publishes the preceding successful evaluation's input lease and provenance unchanged, with the new domain position. A failed evaluation preserves the committed value and valid staging. A transform preserves the source claims it can substantiate and leaves other fields absent.

A lease keeps its backing pool when forwarded. A Delay output may first point to its initializer pool and then to an upstream pool. Charge retention to the pool backing each value. An already retained initializer remains readable when the output switches pools. Subscription-level admission routing belongs to AV13a.

AV07b needs a fixed-size optional origin carrying a run-scoped source ID, continuity epoch and sequence. A producer must change epoch before reusing sequence numbers for that source. Test forwarding of different source occurrences backed by the same buffer and retention across the initializer-to-upstream pool switch. AV11 and AV12 define the representations and producer rules for sample intervals, sample rates, clocks and presentation timestamps before using them.

### D5 — Vector FFT and PSD

**Accepted.** Use the portable radix-2 transform from `s7spectral/include/s7spectral/analysis.hpp` as the baseline. Extend it to native `C32` and retain its current `C64` analysis callers. Keep the vector FFT in the out-of-tree spectral plugin. Qualify the installed SDK and package with their existing dependencies. Evaluate FFTW as a possible backend if representative benchmarks show a material benefit.

- **FFT.** Accept fixed power-of-two lengths in the bounded range supported by the transform. Publish all N bins in standard DFT order. The forward transform uses the negative exponent with no scale, and the inverse uses the positive exponent with scale `1/N`. Prepare twiddles and admit output storage before stepping. Initialize an unpublished output buffer from the immutable input once and transform it in place. Account for that initialization separately from wire copies.
- **PSD.** For complex IQ, use the two-sided periodogram with a periodic Hann window. With known physical sample rate `Fs`, use `|X[k]|² / (Fs × sum(w²))` in squared input units per Hz. An explicit power-per-bin mode uses `|X[k]|² / (N × sum(w²))`. A source with unknown rate has no physical Hz axis. Preserve the D6 source interval and identify window, units and bin ordering in the result. Apply the dB floor in presentation using an explicit reference and preserve linear power for numerical consumers.
- **Verification.** Compare against a direct DFT oracle for impulse, DC, positive and negative tones, two tones and inverse reconstruction. Compare integrated PSD against the independent windowed time-domain mean square. Waterfall may reorder bins for display without changing the graph value. AV10a records the backend audit, supported-length and memory limits, numeric tolerances and reproducible performance cases.


<a id="d7-proposal--array-ports-on-nested-schemas"></a>

### D7 — Array ports on nested schemas

**Accepted.** For 0.2.0, support array ports on nested schemas through the parent graph compiler. Treat exposed ports as typed aliases of the producing slot under ADR-024. Validate their descriptors at each boundary. Preserve the lease, source occurrence, rate-domain position and causal-memory timing across any number of nested boundaries. An exposed port contributes dependencies and type constraints during compilation and has no runtime storage or evaluation phase.

Defer standalone array-valued `Schema::bind_inputs()` and `Schema::step()`. Refuse that use before allocating scalar proxy storage or binding the graph. A future standalone API will need a contract for admitting external array leases, retaining outputs and scheduling the graph on behalf of its caller. Flattening covers reusable submodels, including future spatial blocks, with the same scheduling and publication semantics as an inlined graph.

AV08a will check exact shape and type through nested ports, output aliases, and array feedback through a nested delay against an equivalent inlined graph. AV08b will cover persistence and Studio round trips. A boundary that crosses rate domains still requires an explicit rate-conversion element with a compatible contract.

### Plugin and format compatibility policy (D1)

On 2026-11-26 the maintainer confirmed that S7 has no production releases and owes no compatibility to earlier versions or third-party plugins. Choose the API, ABI, schema and wire formats that best serve the design. Qualify the current SDK, repository-owned plugins and bundled diagrams together. If a shared C++ plugin boundary remains, require a valid ABI version before registration. See the [plugin interface](PLUGIN_INTERFACE.md) and AV00 baseline.

### Open decision register — explicit maintainer approval required

| ID | Decision | Gate |
| --- | --- | --- |
| D1 | **Accepted.** Earlier versions and third-party plugins carry no compatibility obligations. Qualify repository-owned consumers of the current interfaces and formats. | AV02, AV05, release SDK |
| D2 | **Accepted.** [Per-domain publication and retry](#d2--per-domain-publication-and-retry). | AV05b/AV06/AV07 |
| D3 | **Accepted.** [Pool admission and optional retention](#d3--pool-admission-and-optional-retention). | AV04/AV13 |
| D4 | **Accepted.** Generic fixed numeric shapes, compact CPU views and separate typed domain semantics. [Descriptor foundation](#d4--fixed-shape-cpu-layout-and-domain-semantics) | AV01/AV02/AV12 |
| D5 | **Accepted.** [Vector FFT and PSD](#d5--vector-fft-and-psd). | AV10/AV11/AV15 |
| D6 | **Accepted.** [Array provenance and whole-array delay](#d6--array-provenance-and-whole-array-delay). Producer-specific field contracts belong to AV11/AV12. | AV07b/AV11/AV12/AV15/AV16 |
| D7 | **Accepted.** [Array ports on nested schemas](#d7--array-ports-on-nested-schemas). Compiled slot aliases and standalone deferral. | AV08 |

**D1–D7 are accepted.** The work plan tracks implementation and verification. See the AV work plan and [live release gates](RELEASE_0.2.0_WORKPLAN.md).

## 8. Earlier design questions (superseded by D1–D7 above)


1. Shape rule representation and serialization version; which symbolic shape variables become mandatory after 0.2.0.
2. Slot/handle API preserving current scalar plugin ABI and compile-time type inference, without broad virtual dispatch in the inner loop.
3. Pool/lease mechanics for synchronous borrow versus asynchronous observation and the exact point of direct-feedthrough commit.
4. Definition of the first FFT numeric/window/PSD metadata contract; explicit discontinuity/reset behavior.
5. Minimal persisted video fixture/envelope and how to test non-contiguous row-stride and presentation-only loss.
6. Qualification of zero-copy claims per boundary, including materialization for printers/GUI and future FFmpeg/GStreamer hardware surfaces.

These decisions should be captured in a separate accepted ADR once implementation contracts are proven; these notes are not a retroactive claim of acceptance for unspecified API details.
