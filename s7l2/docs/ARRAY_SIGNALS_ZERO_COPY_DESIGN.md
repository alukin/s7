# S7 array-valued signals, zero-copy bindings and minimal viewers

**Design notes — maintainer direction 2026-11-23. Proposed, not implemented or release-qualified.**
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
- Serialize a stable, versioned shape/layout contract in diagram/schema metadata. Preserve old diagrams and scalar wire IDs; do not claim remote wire compatibility merely by adding a local shape. Local array connections are the 0.2.0 target.
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

## 7. Open design decisions (implementation review)

1. Shape rule representation and serialization version; which symbolic shape variables become mandatory after 0.2.0.
2. Slot/handle API preserving current scalar plugin ABI and compile-time type inference, without broad virtual dispatch in the inner loop.
3. Pool/lease mechanics for synchronous borrow versus asynchronous observation and the exact point of direct-feedthrough commit.
4. Definition of the first FFT numeric/window/PSD metadata contract; explicit discontinuity/reset behavior.
5. Minimal persisted video fixture/envelope and how to test non-contiguous row-stride and presentation-only loss.
6. Qualification of zero-copy claims per boundary, including materialization for printers/GUI and future FFmpeg/GStreamer hardware surfaces.

These decisions should be captured in a separate accepted ADR once implementation contracts are proven; these notes are not a retroactive claim of acceptance for unspecified API details.
