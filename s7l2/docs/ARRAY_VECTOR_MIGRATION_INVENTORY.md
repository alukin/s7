# Vector-valued signals: source inventory and migration impact

**Status: implementation inventory, 2026-11-23. No implementation or qualification is claimed by this document.**
Maintainer decisions: **keep existing scalar elements**; implement **new vector/array elements** where necessary. Introduce one logical signal/port model with distinct optimized scalar and lease-backed array storage. The initial goal is **0.2.0 G1/V + G4/W + G4/V**, not a universal multimedia or tensor framework.

Read with [array signals and zero-copy design](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md), the [bounded Codex work plan](ARRAY_VECTOR_CODEX_WORKPLAN.md), [0.2.0 contract](RELEASE_0.2.0.md) and [live work plan](RELEASE_0.2.0_WORKPLAN.md). The C++ implementation is the local reference; Python/Java and remote array wire profiles do not automatically acquire parity.

## Priority legend and rules

- **P0 (0.2.0 mandatory):** required for a real saved-diagram vector path and the two new observers, including tests/packaging.
- **P1 (next useful vector library):** useful extension but **not a 0.2.0 release gate**.
- **P2 (0.3.0/later):** hardware/video/audio, remote transport, spatial numerical methods and broader tensors.
- **Preserve / verify:** the existing scalar implementation stays. Add compatibility/regression tests, but do not rewrite simply to claim "vector support".
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
| [schema.hpp](../s7core/include/s7/schema.hpp), [schema.cpp](../s7core/src/schema.cpp), [flatten.cpp](../s7core/src/flatten.cpp) | Exposed ports in standalone Schema are restricted to F64/C64; flattened wiring follows scalar descriptors | Array-aware exposed ports, input/output forwarding, flat/nested equivalence, stable underlying storage/leases | P0 |
| [element_catalog.hpp](../s7core/include/s7/element_catalog.hpp) and element SDK/plugin loading | Tooling needs to know which elements accept array ports; plugins may rely on old layout | Advertise supported port shape/capabilities; add SDK regression and version policy without introducing a general slowdown | P0 |
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
| [scene_serializer.cpp](../s7studio/src/scene_serializer.cpp), schema tabs/runtime JSON | Persistence must retain port shape and observer settings | Round-trip array descriptors, viewer settings and old scalar diagrams | P0 |
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
