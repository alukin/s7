# Vector-valued signals: source inventory and migration impact

**Status: implementation inventory, 2026-11-23. No implementation or qualification is claimed by this document.**
Maintainer decisions: **keep existing scalar elements**; implement **new vector/array elements** where necessary. Introduce one logical signal/port model with distinct optimized scalar and lease-backed array storage. The initial goal is **0.2.0 G1/V + G4/W + G4/V**, not a universal multimedia or tensor framework.

Read with [array signals and zero-copy design](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md), the [bounded Codex work plan](ARRAY_VECTOR_CODEX_WORKPLAN.md), [0.2.0 contract](RELEASE_0.2.0.md) and [live work plan](RELEASE_0.2.0_WORKPLAN.md). The C++ implementation is the local reference; Python/Java and remote array wire profiles do not automatically acquire parity.

## Priority legend and rules

- **P0 (0.2.0 mandatory):** required for a real saved-diagram vector path and the two new observers, including tests/packaging.
- **P1 (next useful vector library):** useful extension but **not a 0.2.0 release gate**.
- **P2 (0.3.0/later):** hardware/video/audio, remote transport, spatial numerical methods and broader tensors.
- **Preserve / verify:** measure scalar numerical behavior and dispatch cost. Earlier versions and third-party plugins carry no compatibility obligations under [D1](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#plugin-and-format-compatibility-policy-d1).
- **New:** not a legacy element modification. Names in this inventory are illustrative until metadata/registry API and naming are reviewed.

The inventory is based on code and repository documents, not merely proposed features. Presence of block-oriented hardware buffers, scalar frame transport, FFmpeg experiments or independent DSP helpers is **not evidence of existing array graph ports**.

## A. Core and compilation: unavoidable rework

| Area / code landmarks | Current constraint / finding | Minimal change | Priority |
| --- | --- | --- | --- |
| [signal_types.hpp](../s7core/include/s7/signal_types.hpp) | 27 numeric wire types; element storage size and wire type ID tied to numeric SignalType | Keep numeric IDs; add a separate rank/shape descriptor and scalar-versus-array storage class. No fake "vector numeric type" enum IDs | P0 |
| [element.hpp](../s7core/include/s7/element.hpp) | Port carries index/name/type/\`void* value_ptr\`; \`read_input<T>()\` dereferences a scalar binding | Introduce explicit array port registration and typed read-only views / writable output lease acquisition. Keep old scalar entry points and hot path | P0 |
| **New** buffer/view/slot module under \`s7core\` | No generic graph array owner; borrowed spans can outlive storage | Validated shape/extents/strides and bounds; stable array slot; CPU buffer lease/pool; immutable published read access; bounded retention and allocation | P0 |
| [element_metadata.hpp](../s7core/include/s7/element_metadata.hpp), [element_registry.cpp](../s7core/src/element_registry.cpp), [element_metadata.json](../s7core/resources/element_metadata.json) | PortSpec/type rules and factory parameters are numeric-scalar oriented | Versioned array-capable metadata, fixed-size shape parameters/constraints, registry support; make scalar metadata remain valid | P0 |
| [type_inference.hpp](../s7core/include/s7/type_inference.hpp), [type_inference.cpp](../s7core/src/type_inference.cpp) | TypeResolver propagates numeric types, defaults often to F64/C64 | Resolve numeric kind **and** fixed array shape; explicit shape-rule errors, no implicit broadcast, flatten or convert | P0 |
| [validation.hpp](../s7core/include/s7/validation.hpp), [validation.cpp](../s7core/src/validation.cpp) | Connection check compares numeric types; feedback/coupled regions assume scalar state in several paths | Shape/rank and storage validation; reject unsupported array numerical-trial regions, invalid feedthrough cycles and lifetime violations | P0 |
| [compiler.cpp](../s7core/src/compiler.cpp), [graph.hpp](../s7core/include/s7/graph.hpp) | Inputs compile to raw scalar-value pointers | Resolve stable array slots/bindings and execution capabilities once, not per sample. Preserve indexed scalar dispatch | P0 |
| [runtime.hpp](../s7core/include/s7/runtime.hpp), [runtime.cpp](../s7core/src/runtime.cpp) | \`MemoryOutput\`, \`CausalOutput\`, \`TrialRegion\` use small byte snapshots / \`memcpy\`; staged causal-memory publication | Separate handle-aware array candidates, commit/rollback, immutable fan-out, double-buffered causal memory; never byte-copy a C++ lease object | P0 |
| [schema.hpp](../s7core/include/s7/schema.hpp), [schema.cpp](../s7core/src/schema.cpp), [flatten.cpp](../s7core/src/flatten.cpp) | **Compiled flattened** nested SchemaPort aliases differ from **standalone** Schema::step() F64/C64 proxies | P0: shape-preserving **flattened slot aliases**, inline/nested equivalence and persistence; **standalone array Schema deferred/unsupported unless D7 approves separate work** | P0 flattened; P2 standalone |
| [element_catalog.hpp](../s7core/include/s7/element_catalog.hpp), **[element_catalog.cpp](../s7core/src/element_catalog.cpp)**, **[metadata_json.cpp](../s7core/src/metadata_json.cpp)** and element SDK/plugin loading | Metadata JSON parser/catalog output currently scalar-oriented; plugin API v1 passes C++ layouts across nominal C entry points | Full JSON -> registry -> catalog -> schema -> compiler -> Studio shape/format pipeline, rebuilt repository plugin smoke against the installed SDK, and scalar dispatch measurements | P0 |
| Existing coupled and implicit solver classes | Trial replay assumes scalar-sized output snapshots and variable maps | **Explicitly disallow arrays** in these regions initially; extend only with a separately reviewed numerical contract | Preserve (P0 negative tests), implementation P2 |

**Causal semantics are not negotiable:** direct-feedthrough downstream elements may consume a candidate in the same logical \`step()\`; external observers see only successfully committed output. \`Feedthrough::None\` stages memory for the next publication. Retain the old published lease for rollback, prohibit mutation of shared published payloads, and distinguish a block delay from a single-sample delay. Private element state and physical side effects retain their existing limited rollback guarantees (ADR-021). \`dt\` is physical model/rate discretization, not the definition of causality.

## B. Existing numerical elements: do not rewrite all scalar templates

Existing scalar implementations in [elements.hpp](../s7core/include/s7/elements.hpp), [control_elements.hpp](../s7core/include/s7/control_elements.hpp), [sources.hpp](../s7core/include/s7/sources.hpp), [convert.hpp](../s7core/include/s7/convert.hpp) and [resampler.hpp](../s7core/include/s7/resampler.hpp) stay as-is unless a *specific regression fix* is needed.

| Scalar family already present | Separate proposed vector implementation / semantics | Priority |
| --- | --- | --- |
| \`Constant\` | **New** fixed-shape ArrayConstant / deterministic fixture source; no extra scalar-port overload | P0 |
| \`Gain\` | **New** VectorGain: array elements scaled by scalar parameter; preallocated output, no implicit broadcasting | P0 |
| \`Delay\`, \`PreviousValue\` | **New** VectorDelay / VectorPreviousValue: delayed **whole array** (handle publication), not a sample delay | P0 |
| \`Adder\`, \`Multiply\` | **New** elementwise VectorAdd/VectorMultiply with exact shape equality | P1 |
| \`Convert\` | **New** VectorConvert with explicit type conversion and overflow policy | P1 |
| \`ComplexToMagnitudePhase\`, \`ReImToComplex\`, \`ComplexToReIm\` | **New** VectorMagnitude/Power and complex split/merge; zero-copy for compatible views only | P0 for array magnitude/power needed by waterfall; rest P1 |
| \`Divider\`, \`Power\`, \`MathFunction\`, \`Trig\`, \`Saturation\`, \`Sign\`, \`Comparator\`, \`MinMax\` | **New** pointwise vector variants when a use case requires them; reduction/peak are separate definitions | P1 |
| \`Accumulator\`, \`ControlledIntegrator\`, \`EulerIntegrator\`, \`BackwardEulerIntegrator\`, \`TrapezoidIntegrator\`, \`GaussLegendreIntegrator\`, \`SymplecticIntegrator\` | Distinguish elementwise state updates, reductions, staggered fields, and coupled numerical trials; do not reinterpret scalar integrators automatically | P2 |
| \`FirFilter\`, \`IirFilter\` | **New** block filters with persistent intra-block/inter-block history; keep scalar filters | P1 |
| \`Resampler\` | **New** block-sample resampler; distinguish array evaluation cadence from constituent sample rate and valid sample count | P2 |
| \`Switch\`, \`HysteresisRelay\`, \`FrequencyMeter\`, \`PhaseWrap\`, \`PWM\` | Define explicit control scalar vs component mask and state/validity; adapt only where justified | P2 |
| Scalar oscillator/chirp/PRBS/pulse/triangle/impulse and noise generators | **New** deterministic block generator variant with exact sample positions, phase and stream continuity, not N hidden graph steps | P1 |
| \`GridScan\` | Future array field/grid sources and physical coordinate metadata | P2 |

Do not use names alone as evidence of a registered API. New element registration must include implementation, metadata, schema construction, headless execution and independent tests.

## C. DSP, SDR, audio and video elements

| Component / source | Present baseline | New vector work | Priority |
| --- | --- | --- | --- |
| [s7spectral/fft_element.hpp](../s7spectral/include/s7spectral/fft_element.hpp) \`WindowedFFT\` | Scalar F64 input; scalar peak-frequency/peak-amplitude output, not full spectrum | **New** frame FFT / PSD array outputs with explicit bin ordering, window, normalization, sample-rate/index contract; preserve scalar WindowedFFT | P0 |
| [Studio spectrum](../s7studio/src/analysis/spectrum.cpp) / FFTW analysis | Existing view-local FFT for scalar histories | Reuse/test compatible numeric conventions; view-local FFT is separate from graph FFT | P0 review |
| [convolution.hpp](../s7core/include/s7/convolution.hpp) \`FirConvolution\`, \`FftConvolution\` | Reusable allocation-free C64 frame processors, not registered graph elements | **New** vector graph wrappers, history/reset/tail contracts and preallocated outputs | P1 |
| \`FourierCoefficient\` and \`WaveletCoefficient\` plugins | Scalar coefficient streams | Separate whole-vector spectral/wavelet transforms where justified | P1/P2 |
| [s7radio/src/radio.cpp](../s7radio/src/radio.cpp) | Per-sample FM/AM/stereo elements | Block counterparts preserving phase/filter state at block boundaries | P2 |
| [s7sdr/src/soapy.cpp](../s7sdr/src/soapy.cpp) | Device buffers, channel layout and sample position already exist, but graph sources are not generic array ports | Direct IQ-block array source/sink and ownership/metadata bridge, verify actual adapter copies | P2 |
| Generic audio convolution, effects | Block DSP functions exist, hardware audio path separate | Vector effects and measured low-latency graph chains | P1/P2 |
| Generic video processors | Whole-frame FFmpeg experimental path, not production array graph processors | **New** crop, grayscale, resize, frame difference, 2D FFT/convolution with correct strides/plane contracts | P2 |
| FFmpeg / GStreamer | FFmpeg experiments; production GStreamer is a 0.3.0 gate | Optional separate adapters returning retained S7 frame leases; do not require codecs or cameras for 0.2 viewers | P2 |

## D. Observation and Studio: substantial mandatory changes

| Area / files | Existing limitation | Required change | Priority |
| --- | --- | --- | --- |
| [s7observe/observe.hpp](../s7observe/include/s7/observe.hpp), [observe.cpp](../s7observe/src/observe.cpp) | \`ObservationSample\` is one numeric value in 32 bytes; \`sample_output()\` copies scalar bytes | **New** typed array observation/retained lease API, step-consistent snapshots and bounded retention; leave scalar API valid | P0 |
| [local_schema_runner.cpp](../s7studio/src/local_schema_runner.cpp) | Scalar \`probeValues_\`, \`to_display_double()\`, Scope mailboxes | Separate array snapshot/delivery path between stepping thread and GUI; no array-to-double coercion | P0 |
| [observation_source.hpp](../s7studio/src/observation/observation_source.hpp) | Scalar \`Sample{channel,step,time,value:double}\` and scalar journal | Bounded array/frame history or a distinct array observation source, with explicit overwrite/gap policy | P0 |
| [viewer_manager.cpp](../s7studio/src/viewers/viewer_manager.cpp), [viewer_registry.cpp](../s7studio/src/viewers/viewer_registry.cpp) | Routes viewer inputs through scalar-channel sources | Route new array-valued observation sources and register two types; attach/detach/read-only lifetime | P0 |
| [viewer_registry.cpp](../s7studio/src/viewers/viewer_registry.cpp), [observer_metadata.json](../s7studio/resources/observer_metadata.json) | Waterfall and Video Player not registered as 0.2.0 shipped viewers | **New** Waterfall and Video Player presentations with metadata, settings, help and persistent bindings | P0 |
| [wire_type_labels.cpp](../s7studio/src/wire_type_labels.cpp) | Shows only numeric \`SignalType\` labels | Display shape, e.g. \`C32[4096]\`, \`U8[240,320,3]\`; resolve names from compiled/validated descriptors | P0 |
| [connection_planner.cpp](../s7studio/src/diagram/connection_planner.cpp), [element_ports.cpp](../s7studio/src/diagram/element_ports.cpp), diagram items | Wiring logic uses numeric type validation/metadata ports | Show array-capable ports, clear rank/shape mismatch feedback, keep one wire per whole array | P0 |
| [scene_serializer.cpp](../s7studio/src/scene_serializer.cpp), schema tabs/runtime JSON | Persistence must retain port shape and observer settings | Round-trip array descriptors, viewer settings and scalar diagrams in the current format | P0 |
| [element_picker_panel.cpp](../s7studio/src/element_picker_panel.cpp), viewer help/metadata | New types not searchable/discoverable | Include vector elements and two viewers in picker, tooltips and documentation | P0 |
| Printing/PDF, [printing matrix](STUDIO_PRINTING_0.2.0.md) | Viewer print coverage predates new types | **P08/P09:** exact Waterfall axes/legend and immutable Video Player frame snapshot, without pausing computation | P0 |
| Scope, Phase Portrait, Surface, Time-Frequency, Spectrum | Present scalar-history visualizations | **Preserve** scalar behavior; optional later array adaptations are separate features | Preserve |

**Important:** do not send 1920x1080 video payload through \`ObservationSample::storage\`, \`ObservationSource::Sample::double\` or the scalar Scope mailbox. Use separate bounded frame leases and a presentation-only skip policy.

## E. File, hardware, network, cross-language and deployment boundaries

| Component / files | Impact | Priority |
| --- | --- | --- |
| [s7io/file_io.hpp](../s7io/include/s7/file_io.hpp), [recorded_source.hpp](../s7io/include/s7/recorded_source.hpp) | Scalar per-sample file I/O; add separate array/block record/replay if needed; 0.2.0 can start with deterministic in-memory/generator fixtures | P1 |
| [s7hardware/hardware.cpp](../s7hardware/src/hardware.cpp), [hardware.hpp](../s7hardware/include/s7/hardware.hpp) | Hardware buffers batch multiple scalar graph steps; add vector boundaries later, preserve device clock provenance and fault handling | P2 |
| [s7net/net.hpp](../s7net/include/s7/net.hpp), [net_frames.cpp](../s7net/src/net_frames.cpp) | Existing \`NetFrameSource/Sink\` packs scalar channel values into transport frames; **not** generic vector wires | P2 |
| \`s7transport\` frame and stream contracts / [wire protocol](../../doc/S7_Network_Wire_Protocol_v1.md) | Require versioned shape/format, receiver bounds, backpressure, sequence/capability negotiation; local 0.2.0 vector wires need no network protocol change | P2 |
| [s7runtime/runtime_service.cpp](../s7runtime/src/runtime_service.cpp), \`s7ctl\` schema/deployment/inspection | Local JSON schema loading and array-capable descriptor validation in 0.2.0; remote profile advertisement and deployment validation later | P0 local; P2 remote |
| \`s7py\`, \`s7java\` | Document unsupported array interop until new common-subset protocol and conformance fixtures exist | P2 |
| Plugin SDK, CMake, packages, Studio tests, source fixtures | Build/install new array types/viewers, preserve scalar plugin interoperability and required platform exclusions, run independent image/numeric tests | P0 |

## F. 0.2.0 acceptance and ownership boundaries

**Mandatory implementation:**
1. Core array port/shape/lease/slot/validation/compiled bindings, handle-safe publication and rollback, whole-array \`VectorDelay\`, correct scalar coexistence.
2. Small **new** vector library: deterministic source/constant, pass-through, Gain, magnitude/power and full FFT/PSD array outputs. Minimum F32, C32, U8; fixed rank-1 for DSP and rank-2/3 U8 for simple video fixture.
3. Array observation delivery; Waterfall and basic Video Player from saved diagrams. Test bounded history/drop behavior, lifecycle, print/PDF and packaging.
4. Tests: incompatible shapes/types, fan-out buffer identity, zero payload-copy local connections, rollback after direct/memory faults, nested exposed ports, scalar numerics and scalar performance.

**Deferred unless a separate maintainer decision changes scope:** scalar element rewrites, broad elementwise library, generic array integrators, coupled-trial array solvers, full shape broadcasting, multi-plane/GPU/DMA-BUF, video codecs/camera hardware, GStreamer/FFmpeg production media adapters, packet-array transport, 2D FFT, spatial topology and full field PDE solvers.

## G. Current testing evidence and uncertainties

This is a **targeted source audit**, not a complete symbol-by-symbol audit of every file and platform. Before editing, Codex must verify paths, live API changes, actual constructors, existing CTest names, UI metadata conventions, availability of an independent FFT oracle and representation of nested exposed-port shapes. Treat mismatches between docs and code as explicit findings, not silent substitutions.

For bounded sessions, issue ordering and exact handoff records see [ARRAY_VECTOR_CODEX_WORKPLAN.md](ARRAY_VECTOR_CODEX_WORKPLAN.md). Before a release gate closes, record candidate SHA, fixture/reference results, GUI image evidence, commands and exclusions in the [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md).

## H. Independent-review migration checkpoints and acceptance boundaries

The [2026-11-23 independent review](ARRAY_VECTOR_INDEPENDENT_REVIEW_2026-11-23.md) records source constraints and proposed remedies. The [design register](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#open-decision-register--explicit-maintainer-approval-required) records accepted decisions D1 and D4 and open decisions D2, D3 and D5–D7. The [AV session DAG](ARRAY_VECTOR_CODEX_WORKPLAN.md) defines execution boundaries.

| Source / interface to re-audit | Required migration and negative evidence | Work sessions |
| --- | --- | --- |
| `s7core/src/runtime.cpp`, `runtime.hpp`, `element.hpp`, `compiler.cpp` | Stable handle slot separate from 32-byte native scalar staging. Test mixed scalar/array output, direct vs external visibility, **per-domain** commit, early/late memory and late direct failures, repeated retry and earlier-domain committed prefix. No private-state/I/O rollback claim | AV05a/b, AV06a/b, AV07a/b |
| `s7core/src/metadata_json.cpp`, `element_catalog.cpp`, `element_registry.cpp`, `type_inference.cpp`, `validation.cpp` | JSON -> registry -> catalog -> schema -> compiler -> Studio concrete shape, exact type/rank/extents/required format; overflow/stride/range/unknown-shape rejection, scalar legacy round-trip; arrays in coupled/implicit numerical trials rejected before execution | AV01–AV03, AV08b, AV14a |
| `s7core/include/s7/validation.hpp` `SchemaPort`, `flatten.cpp`, standalone `schema.cpp` | Flattened array exposed-port alias points at stable slot; placeholder scalar bytes are **never** array storage. Inline/nested equivalence with VectorDelay, mismatched aliases and persistence. Standalone array Schema is explicitly unsupported pending D7 | AV08a/b |
| `s7core/include/s7/element_plugin.hpp`, `s7runtime/src/plugin_loader.cpp`, out-of-tree `s7spectral`, installed CMake SDK | Qualify current repository plugins rebuilt against the installed SDK. If the loader retains an ABI boundary, verify a matching current version before registration. See [AV00](ARRAY_VECTOR_AV00_BASELINE.md) for the manifest check finding. | AV00, AV02, AV05a, AV18d |
| `s7transport/include/s7/frame_transport.hpp` and `src/frame_transport.cpp` | Pool/retention accounting may be reused; existing transport acquisition copies input bytes and is **not** the writable array producer API. Reserve execution-critical storage independently from optional observers; tiny-pool/slow-viewer progress, cancellation and counters | AV04, AV13a/b, AV18b |
| `s7observe/include/s7/observe.hpp`, Studio `local_schema_runner.cpp`, `observation_source.hpp` | New bounded immutable **committed-array** observation, distinct from scalar 32-byte samples/double journal/Scope; coherent within domain, disclose cross-domain evaluation positions, viewer skips do not backpressure graph | AV13a/b, AV18c |
| Studio `diagram/connection_planner.cpp`, `element_ports.cpp`, `wire_type_labels.cpp`, viewer registry/metadata and saved scene | Observer pins currently bypass numeric type checking; reject invalid array spectrum/image shape/format in Studio **and** headlessly. Register/viewer settings, layout, reopen/rebind, visual/GUI and installed resources | AV14a/b–AV16b |
| `s7spectral/include/s7spectral/analysis.hpp`, `s7spectral/src/fft_element.cpp`, `s7core/src/convolution.cpp`, `s7studio/src/analysis/fft.cpp` | Audit C64 portable vs FFTW scalar peak-only and Studio transforms before a new C32 FFT; explicit scratch/copy, bin orientation/normalization, independent DFT/PSD oracles and SDK tests | AV10a/b, AV11a/b |
| `s7tests/video_stream_spike.cpp` and U8 source/viewer tests | Reuse deterministic pixel patterns/oracles, **not** old per-pixel stepping as whole-frame semantics; grayscale/RGB shape, stride if supported, sequence/PTS absence, skips and exact displayed pixels | AV12, AV16a/b |
| Studio printing and packaging | Preserve independent **P01–P07** workstream; AV17a/b owns **P08/P09** immutable committed snapshots and installed viewer artifacts. PDF/image content must be inspected, not merely nonempty | AV17a/b, AV18c/d |

**Rate and provenance boundary:** graph rational rate domains (ADR-022), physical IQ/audio sample rate, first sample index/valid count, frame ID, optional source timestamp/PTS and row discontinuity are separate. A C32[N] array is one causal graph evaluation, **not N calls to step()**; scalar `Resampler` is not an in-array sample converter. Check actual metadata forwarding and explicit missing provenance; never invent PTS or frequencies.

**Required cost ledger:** report allocations and **payload bytes copied** separately for input binding, local fan-out, pass-through, handle staging/rollback (expected zero), FFT/Gain/PSD outputs (new data), image upload/Qt conversion, PDF/printing, packaging adapters and future transport/hardware (potential materialization). Source->pass-through->two-consumer identity and tiny pool with retained slow viewer are mandatory. Preserve and measure scalar dispatch/allocations on a named build/host.

**Release scope:** P0 is local fixed-shape F32/C32 rank-1 and U8 rank-2/3 CPU arrays, flattened nested ports, direct/memory rollback, minimal new source/PassThrough/Gain/FFT/PSD, committed-array observation, Waterfall, Video Player, saved diagrams, G4/P P08/P09 and installed SDK/plugin qualification. P1/P2 remain broader vector filters, standalone array Schema, array coupled numerical solvers, network array profiles, codecs/FFmpeg/GStreamer production adapters, GPU/multiplane, 2D FFT/spatial fields. All existing scalar elements and scalar observation stay intact. No item is marked implemented or qualified by this document.
