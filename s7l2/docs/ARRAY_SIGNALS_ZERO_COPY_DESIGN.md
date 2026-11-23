# S7 array-valued signals, zero-copy bindings and minimal viewers

**Design notes — maintainer-approved scope; 2026-11-23 independent-review reconciliation. D1–D7 remain OPEN for maintainer approval, not implemented or release-qualified.**
The [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) is the live release acceptance record; these notes define the reusable architecture and the narrow first slice. The [source-based migration inventory](ARRAY_VECTOR_MIGRATION_INVENTORY.md) and [five-hour Codex session plan](ARRAY_VECTOR_CODEX_WORKPLAN.md) describe what must change and a bounded delivery order. **Existing scalar elements stay intact; vector behavior uses new elements.** No code, tests or hardware qualifications are claimed by adding these documents.

## 1. Motivation and boundaries

The same logical array signal must support IQ/SDR sample blocks and FFT spectra, audio blocks and convolution, whole video frames, and future spatial simulation fields with differential operators. S7 must not introduce an SDR-only vector wire or a video-only second runtime.

S7 already has 27 scalar numeric signal types, scalar native-storage Port bindings, a scalar WindowedFFT plugin that reports peak values, independent Studio spectral analysis, frame network transport that batches scalar frames, reusable C64 FirConvolution/FftConvolution processors, and whole-frame video/FFmpeg experiments. These are starting points, **not** a present implementation of general array-valued ports. See [element guide](ELEMENT_DEV_GUIDE.md), [frame elements](FRAME_ELEMENTS.md), [convolution](CONVOLUTION.md), [Studio spectrum](STUDIO_SPECTRUM.md), and [video design](VIDEO_IO_FRAME_DESIGN.md).

Invariants:
- **One causal model:** a connection expresses a dependency; the scheduler enforces it. One Element::step() transforms one set of logical input values, scalar or shaped. A sample block is not secretly thousands of graph steps. Physical/model dt, block cadence, underlying sample positions, video PTS and spatial grid spacing are different quantities.
- **Scalar compatibility:** native rank-zero scalar storage and optimized scalar evaluation remain intact; do not widen every existing numeric port to a dynamic heap object.
- **Zero-copy along local wires:** bindings/fan-out pass views and stable buffer handles, not entire payload copies. A transform may need separate preallocated output memory. "Zero-copy" is claimed only across proven compatible boundaries.
- **Observation is external:** viewers consume immutable published values or snapshots; no rendering/FFT for visualization participates in the functional graph or changes causal execution. A true computational FFT may be an element; display-only spectrum processing may be an observation processor.
- **Fault honesty:** no partial array publication, no invented frames/sample positions, no implicit pixel-format conversion, resampling, graph delay or hidden device activation.
- **Bounded resources:** preallocate/admit storage and queue capacity before running; record copy/transfer sites, memory budget, retained leases, skips and faults.

## 2. Logical signal type versus storage

Treat the logical signal contract as (numeric element type, rank, extents, optional domain interpretation), where **rank zero is the current scalar**. Shapes are part of the declared/compiled port contract. For a first implementation use concrete positive extents; symbolic dimension unification and bounded dynamic extents follow after independent tests.

Examples:

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

Do not add pseudo-array entries to existing numeric wire SignalType IDs or put shared_ptr/complex C++ objects into the current memcpy snapshots. Introduce a typed handle-aware publication/snapshot path. Existing scalar API and ABI are migration constraints and need focused regression tests; a new plugin/serialized schema capability must be versioned and advertised explicitly.

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

- Add optional shape rules alongside existing numeric PortSpec/type inference. A scalar has shape []; a shaped output carries concrete extents in the 0.2.0 slice. Reject overflow, rank/extent mismatch, invalid strides/planes and unsupported element types before running. Do not silently broadcast or flatten.
- Serialize versioned logical type/rank/extents and required format/capabilities in diagram/schema metadata, not incidental host padding, offsets, pointers or storage identity. Preserve old diagrams and scalar wire IDs; do not claim remote wire compatibility merely by adding a local shape. Local array connections are the 0.2.0 target.
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

## 6. Forward-compatible roadmap and tests

- **0.3.0:** audio block convolution (existing C64 processor refactoring where appropriate), real hardware/audio/SDR/whole-frame video and production GStreamer adapters, optional FFmpeg adapter, basic image transforms, broader buffer/plane/stride support, performance and clock provenance, use/reuse 0.2.0 viewers. Keep video and audio sample clocks explicit.
- **Later:** FFT2D/3D, general strided tensor transforms/convolution, tiled processing, accelerated CPU/GPU memory domains, spatial grid geometry and differential operators (eventual spatial S7 goal). A vector field is not automatically an RGB frame or a collocated Yee-grid field.
- **Distributed:** existing NetFrameSource/Sink and packet wire profiles are not equivalent to a general tensor transport. Specify schema/shape/layout/capability, partial progress, clock, buffer ownership, backpressure and boundaries separately before claiming remote array support.
- **Testing principle:** compare saved diagrams against independent references, examine numerical and presentation results, inject failures and resource pressure, and record the actual candidate SHA, tested profiles, exclusions and limits.

## 7. Independent-review architectural reconciliation (2026-11-23)

The [independent review](ARRAY_VECTOR_INDEPENDENT_REVIEW_2026-11-23.md) documents confirmed code constraints separately from proposed remedies. This section is **planning guidance, not an accepted ADR or implemented API**. Preserve [ADR-021](../../doc/S7_Architectural_Decisions.md#adr-021), [ADR-022](../../doc/S7_Architectural_Decisions.md#adr-022), [ADR-024](../../doc/S7_Architectural_Decisions.md#adr-024) and the optimized scalar path.

### Publication, rollback and ownership (F1–F4, F11–F12)

Array slots must distinguish **previous externally committed handles**, **current direct-feedthrough candidates** visible only to downstream graph evaluation, **previous valid causal-memory staging** eligible for the present evaluation, and **next staged causal-memory candidates** for a later evaluation. Compiled array inputs bind to **stable producer slots**, not movable payload addresses; RAII leases must never be `memcpy`-snapshotted in scalar `MemoryOutput`, `CausalOutput`, `TrialRegion` or observation storage. Acquire writable **unpublished** storage without modifying a shared/published/rollback-protected payload. Handle swaps, fan-out, pass-through, staging and rollback should incur no payload-byte copies; FFT, Gain, UI rasterization, transport or layout materialization can compute/copy into separate storage. Maintain a per-boundary copy/allocation ledger.

Before evaluation, retain the previous external snapshot and valid memory staging. Direct elements can consume their current candidates in causal order; causal-memory elements stage a candidate for the *next* publication boundary. On success publish a coherent set of output handles and advance the domain's committed position/staging; on fault restore the previous externally visible handles and valid staging and do **not** advance that domain's step. Retained observers still see the old immutable bytes. The exact no-throw commit/resource-reservation algorithm requires **D2 approval** and tests for late direct, early/late memory, multiple-output, mixed scalar/array, consecutive retry and retained observer faults.

**The atomic boundary is one rate-domain evaluation, not an entire multi-rate run.** If another domain already completed its step, it may remain advanced when a later domain fails (ADR-022). Neither private element state nor irrevocable I/O or network output is rolled back by ADR-021. When replay is unsafe, latch/report an explicit fault. Array participation in implicit/coupled numerical trials is rejected **before execution**, pending a separate reviewed trial contract.

A bounded producer pool needs an **execution-critical reserve independent of optional viewers**. A slow/paused Waterfall or Video Player cannot hold the last available producer candidate. Define maximum retained bytes/frames, eviction/drop and discontinuity counters, admission, cancellation and recovery **before AV04** (D3). Optional observation is newest/bounded presentation with skips, never graph backpressure; authoritative/lossless recording is a separately admitted backpressure/failure profile. Synchronous borrowed views are compile-lifetime bounded; asynchronous clients and printing retain explicit charged immutable leases. Existing `s7transport` lease accounting can inform implementation, but `FrameTransport` copies incoming bytes and is not a writable-array producer API. Add a distinct committed-array Observation source/mailbox: do not feed arrays through scalar `ObservationSample::storage[32]`, scalar `ObservationSource::Sample::double`, Scope or journal.

### Shapes, nested schemas, Studio and provenance (F5–F10)

Require **exact numeric type, concrete positive rank/extents and required semantic format** at compilation; reject symbolic/dynamic ambiguity, scalar-vector coercion, broadcast, implicit reshape/conversion, overflowed byte calculations, invalid offset/strides/bounds and unsupported layouts. The first profile is F32[N]/C32[N] and U8[H,W] grayscale or U8[H,W,3] interleaved RGB (the last dimension alone does not confer RGB meaning). Resolve and validate metadata through `metadata_json.cpp`, registry, `element_catalog.cpp`, schema serialization, type inference, compiler and Studio; old scalar metadata/files must survive unchanged. Persist **logical descriptors and required storage capabilities**, not incidental CPU padding or addresses. DSP is contiguous; U8 row padding is supported only if explicitly declared and independently tested. Explicit adapters and measured copies are required for layout conversion.

**Flattened compiled nested schemas** must forward shape-preserving slots under ADR-024, not treat `SchemaPort` scalar placeholders as array payloads. **Standalone array-valued `Schema::step()` is deferred/unsupported pending D7**; its F64/C64 proxy limitations must not be silently claimed fixed. Test inline/nested equivalence, feedback through array memory, exposed-port aliases and saved-diagram round-trip. Studio's `connection_planner.cpp` currently bypasses observer-input type checks: add descriptor-aware editor checking and authoritative runtime rejection. Waterfall accepts only a supported rank-1 spectrum/PSD format with appropriate axis metadata; Video Player accepts only supported U8 grayscale/RGB shape/layout. Include viewer registry, labels, settings, print and installed resources.

One C32[N] value is **one causal `step()`**, not N graph steps. Distinguish graph **rational model-time rate-domain cadence** (ADR-022), physical sample rate, valid sample count and block duration, first physical sample index, block/frame/row sequence, optional source clock/timestamp and video PTS. Define optional provenance fields, absence, forwarding, reset, discontinuity and delay behavior **per producer/transform** before AV11/AV12 (D6). Never derive physical samples/PTS from graph `step_count()` or use scalar `Resampler` as a hidden in-block resampler. Missing source sample rate means unknown physical frequency axis; Waterfall gaps and Video Player skipped-frame IDs must be visible.

Audit `s7spectral/include/s7spectral/analysis.hpp` (portable C64), the FFTW scalar peak-only `WindowedFFT`, `s7core/src/convolution.cpp` (other transform) and Studio's FFTW analysis before selecting a C32 FFT. D5 covers backend reuse, C32-native vs C64 scratch/copies, out-of-tree SDK/linkage, forward/inverse convention, bin orientation, window/PSD normalization and dB floor. Validate against an **independent DFT oracle**; comparing the chosen backend to itself is insufficient.

### Plugin ABI is a maintainer decision (F4)

Plugin API v1 exposes C symbols but shares **C++** `ElementRegistry`/`ElementDescriptor` and element layouts ([plugin interface](PLUGIN_INTERFACE.md)). D1 must choose **proved old-binary ABI preservation** or **an intentional plugin API bump with clear early rejection** plus rebuilt installed-SDK/out-of-tree `s7spectral` tests. Recompiling a plugin proves source compatibility, **not** binary compatibility. Do not mutate ABI-sensitive `Element`/`Port`/metadata layouts without the D1 decision.

### Open decision register — explicit maintainer approval required

| ID | Unresolved selection (do not infer acceptance) | Gate |
| --- | --- | --- |
| D1 | Preserve tested old plugin binary ABI **or** version bump, reject old binaries and requalify installed SDK/plugin. | AV02 invasive headers, AV05, release SDK |
| D2 | Approve candidate/staging/committed transition table and fallible resource reservation before nonthrowing **per-domain** commit. | AV05b/AV06/AV07 |
| D3 | Approve producer reserve, optional viewer retention limits, drops/counters, cancellation and separate lossless recording profile. | AV04/AV13 |
| D4 | Approve logical descriptor/schema version, fixed ranks/format, stride capabilities and validation bounds. | AV01/AV02/AV12 |
| D5 | Approve FFT backend/C32 conversion, PSD/window normalization, bin order, axis and package dependencies. | AV10/AV11/AV15 |
| D6 | Approve source rate/sample index, frame identity, gaps/PTS and metadata forwarding/reset/absence semantics. | AV11/AV12/AV15/AV16 |
| D7 | Approve explicit standalone array Schema deferral **or** separately scoped proxy/storage implementation. | AV08 |

**All D1–D7 are OPEN.** Record approved answers in an ADR/decision log before executing blocked cards. This proposal is not approval of invasive semantic or ABI changes. See the [revised AV work plan](ARRAY_VECTOR_CODEX_WORKPLAN.md) and [live release gates](RELEASE_0.2.0_WORKPLAN.md).

## 8. Earlier design questions (superseded by D1–D7 above)


1. Shape rule representation and serialization version; which symbolic shape variables become mandatory after 0.2.0.
2. Slot/handle API preserving current scalar plugin ABI and compile-time type inference, without broad virtual dispatch in the inner loop.
3. Pool/lease mechanics for synchronous borrow versus asynchronous observation and the exact point of direct-feedthrough commit.
4. Definition of the first FFT numeric/window/PSD metadata contract; explicit discontinuity/reset behavior.
5. Minimal persisted video fixture/envelope and how to test non-contiguous row-stride and presentation-only loss.
6. Qualification of zero-copy claims per boundary, including materialization for printers/GUI and future FFmpeg/GStreamer hardware surfaces.

These decisions should be captured in a separate accepted ADR once implementation contracts are proven; these notes are not a retroactive claim of acceptance for unspecified API details.
