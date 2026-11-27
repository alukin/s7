# AV00 scalar baseline and array decisions

Source `65d4d366bddba4c941719c8f7821e19d0e3f999c`, matching `origin/main` at the start of the session. This records baseline evidence and the first descriptor proposal. The [design register](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#open-decision-register--explicit-maintainer-approval-required) records the accepted D1 and revised D4.

## Baseline measurements

Measured on 2026-11-26 with Fedora Linux 45 x86-64, AMD Ryzen 7 4700U, GCC 16.2.1, CMake 4.3.0 and Ninja. The benchmark and headless smoke tests used a fresh Release build with `S7_BUILD_STUDIO=OFF`. The rebuilt Studio diagram test used the local Release build and Qt 6.11.2.

| Scalar graph | Median ns/step | Run range, ns/step | Output checksum | C++ allocations over 20,000 warmed steps |
| --- | ---: | ---: | ---: | ---: |
| Chain of 512 Gains | 1,780.17 | 1,775.86–1,787.52 | 1.0051331038670792 | 0 |
| 512 Gains sharing one source | 1,433.44 | 1,430.96–1,453.10 | 1.0000100000000001 | 0 |
| Scalar feedback | 28.50 | 28.36–31.25 | 1 | 0 |
| Flattened nested feedback | 207.59 | 207.11–211.86 | 1 | 0 |

Timing uses the average of one million calls to `NodeRuntime::step()` in each of five runs per graph, after 1,000 warm-up steps. Processes were pinned to logical CPU 4. Reuse the [indexed benchmark](../s7core/benchmarks/indexed_execution.cpp) and the same build and host profile for comparisons. The table is a reference measurement. A performance acceptance threshold is still open.

The separate allocation probe used the same four graph constructors and intercepted scalar, array and aligned C++ `operator new` calls on the stepping thread. It counted 2,866 calls during a deliberately measured graph construction/compilation positive control. The zero counts apply to warmed, successful scalar steps in these graphs. Direct C allocator calls, other threads, compile/init costs and exceptional steps are outside that measurement.

## Smoke tests and source checks

The selected headless suite passed 14 of 14 tests. The separate saved-diagram smoke test also passed.

| Area | Result | Source and evidence |
| --- | --- | --- |
| Scalar execution and validation | Pass | Core, indexed execution, metadata JSON, numeric type, strict type and type inference CTest cases. [Test registration](../s7core/CMakeLists.txt) |
| Rate, rollback and nested schemas | Pass on the existing smoke cases | Design rate, rate inference, step rollback, coupled integration and nested flattening. [Rollback tests](../s7core/tests/step_rollback_smoke.cpp), [ADR-021/022/024/029](../../doc/S7_Architectural_Decisions.md#adr-021) |
| Scalar observation | Pass | Observation smoke, including the stepping-thread contract. [Observation tests](../s7observe/tests/observe_smoke.cpp) |
| Current SDK and repository plugins | Pass | Installed SDK, rebuilt `s7spectral` and `s7wavelet`, and runtime plugin loader. [SDK smoke workflow](../tools/test_third_party_plugin.sh) |
| Saved SP06 scalar diagram | Pass | Static wire-tree consistency and execution of the saved diagram against the independent FIR oracle. [Diagram](../education/labs/SP06-three-tap-fir/sp06_three_tap_fir.s7diagram), [oracle](../s7tests/education_sp06_smoke.cpp) |
| Studio wiring and scene persistence suite | Baseline failure | The suite reported one failing assertion, “element families and nested schemas use distinct soft fills,” including under Fusion. Gain and Delay metadata provide the same fallback color and their categories have no entries in the family palette. [Assertion](../s7studio/tests/diagram_test.cpp#L391), [palette](../s7studio/src/diagram/items.cpp#L81), [metadata](../s7core/resources/element_metadata.json) |

Two focused probes ran on the fresh baseline:

- An independent 100 Hz scalar integrator and a 50 Hz faulting element ran without an outer model clock. The later-domain failure left the outer step at 1 and the earlier-domain output at `0.01`. The retry completed outer step 2 with output `0.02`, versus `0.01` in the unfaulted reference. The unclocked scheduler path repeats an already committed domain on retry. Record this as a D2 regression case alongside the existing linked-domain tests. Also check the ordering of restoration and fault reporting because `record_fault` can allocate before rollback. [Scheduler and fault path](../s7core/src/runtime.cpp#L495)
- Boost.JSON classified `"api_version": 1` as signed `int64`. The manifest version check only enters its unsigned branch, and a named manifest skips the query fallback. If the shared C++ boundary remains, verify the ABI version before the registration callback. Treat this as a current-interface check under D1. [Manifest version check](../s7runtime/src/plugin_loader.cpp#L63), [query and registration path](../s7runtime/src/plugin_loader.cpp#L269)

### Commands

Run from `s7l2/`. The session used `B=/tmp/s7-av00.i0vQ8r/build`. The build directory can be recreated at any writable location.

```bash
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DS7_BUILD_STUDIO=OFF
cmake --build "$B" --parallel 4 --target all s7core_indexed_benchmark \
  s7core_smoke s7core_indexed_execution_smoke s7core_step_rollback_smoke \
  s7core_design_rate_smoke s7core_nested_flattening_smoke s7core_signal_types_smoke \
  s7core_signal_types_strict_smoke s7core_type_inference_smoke s7core_rate_inference_smoke \
  s7core_metadata_json_smoke s7core_coupled_integration_smoke s7observe_smoke \
  s7runtime_plugin_loader_smoke s7runtime_plugin_sdk_smoke
ctest --test-dir "$B" \
  -R '^(s7core_(smoke|(indexed_execution|step_rollback|design_rate|nested_flattening|signal_types|signal_types_strict|type_inference|rate_inference|metadata_json|coupled_integration)_smoke)|s7observe_smoke|s7runtime_plugin_loader_smoke|s7spectral_sdk_smoke)$' \
  --output-on-failure
cmake --build "$B" --parallel 2 --target s7tests_education_sp06
ctest --test-dir "$B" -R '^s7tests_education_sp06$' --output-on-failure
python3 ../tools/ai/validate_diagram.py \
  --diagram education/labs/SP06-three-tap-fir/sp06_three_tap_fir.s7diagram
cmake --build build-release --parallel 3 --target s7studio_diagram_test
ctest --test-dir build-release -R '^s7studio_diagram$' --output-on-failure
QT_STYLE_OVERRIDE=Fusion ctest --test-dir build-release -R '^s7studio_diagram$' --output-on-failure
for case_name in gain512 wide512 feedback nested_feedback; do
    for repetition in 1 2 3 4 5; do
        taskset -c 4 "$B/s7core/s7core_indexed_benchmark" "$case_name" 1000000
    done
done
```

Local evidence is in `/tmp/s7-av00.i0vQ8r`, including the environment fingerprint, timing and allocation CSVs, CTest logs and the three probe sources. This session used no physical I/O and did not run sanitizer, cross-platform, screenshot or print qualification.

Re-run the local probes using the same fresh Release library. Here `A` is the artifact directory and `B` is its `build` subdirectory.

```bash
c++ -std=c++26 -O3 -rdynamic -I s7core/include -I s7core/benchmarks \
  '-DS7_PENDULUM_DIAGRAM="examples/double_pendulum_full.s7diagram"' \
  "$A/allocations.cpp" -L "$B/s7core" "-Wl,-rpath,$B/s7core" -ls7core -lboost_json -o "$A/allocations"
"$A/allocations"
c++ -std=c++26 -O3 -I s7core/include "$A/unclocked_retry.cpp" \
  -L "$B/s7core" "-Wl,-rpath,$B/s7core" -ls7core -o "$A/unclocked_retry"
"$A/unclocked_retry"
c++ -std=c++26 "$A/manifest_kind.cpp" -lboost_json -o "$A/manifest_kind"
"$A/manifest_kind"
```

## D4 proposal for AV01

**Historical proposal.** The maintainer subsequently approved [revised D4](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#d4--fixed-shape-cpu-layout-and-domain-semantics), which generalizes the shape descriptor to spatial arrays and keeps domain semantics separate. The proposal below records the initial release profiles considered in AV00.

| Profile | Logical type and shape | Initial CPU layout |
| --- | --- | --- |
| Scalar | Existing numeric type, rank 0 | Existing native storage |
| DSP vector | F32 or C32, `[N]` | Contiguous and aligned for the element type |
| Grayscale | U8, `[H,W]`, explicit grayscale format | Compact row-major |
| Color | U8, `[H,W,3]`, explicit interleaved RGB format | Compact row-major, RGB channel order |

Extent values are positive and fixed. Require the exact permitted rank/type/format combination, including three channels for RGB. Check element-count and byte-count arithmetic before allocation. For a view, validate alignment, dense strides and bounds using the accessible byte range and byte offset. Include the total storage budget at graph/pool admission. Initially reject negative strides, padding, transposed views and unsupported formats. Shape compatibility requires numeric type, extents and declared semantic format/capability. Physical offsets and addresses belong to the runtime view.

Use one canonical schema representation in AV02. Persist the logical descriptor and a layout capability where required. Metadata that omits shape can use the canonical rank-zero default. Studio's diagram layout version is a separate choice. Update project-owned metadata and diagrams together when their representation changes.

AV01 should cover valid scalar, F32, C32, grayscale and RGB profiles, and reject zero or negative dimensions, invalid RGB channel width, unsupported rank/type/format, overflow, misalignment, and views that exceed their backing range. Include exact-boundary successes and one-byte-short failures. Runtime slots, C++ leases, plugin-facing ports and persisted metadata are later cards.

## Remaining decisions and next dependency

| Decision | AV00 disposition |
| --- | --- |
| D1 · Compatibility | **Accepted.** Design the current interfaces and formats freely. Verify repository-owned plugins and diagrams against the chosen interfaces. |
| D2 · Publication | Open. Define a nonthrowing per-domain commit, the retained prior state on a failure, and the independent-domain retry case above. Preserve ADR-029's distinction between output rollback and private or external effects. |
| D3 · Ownership | Subsequently accepted in the [design register](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#d3--pool-admission-and-optional-retention). |
| D4 · Descriptors | Proposed in AV00 and later accepted in [revised form](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#d4--fixed-shape-cpu-layout-and-domain-semantics). |
| D5 · FFT and PSD | Open. Audit the existing backends in AV10a, then decide the backend, bin convention, windowing and normalization before the corresponding implementation. |
| D6 · Provenance | Open. Define source identity forwarding before whole-array Delay. Define physical rate, sample/frame indices, optional timestamps, and source gaps separately from graph cadence and viewer skips. |
| D7 · Nested schemas | Open. Propose supporting array slots through compiled flattened aliases, with a clear error for array-valued exposed ports on standalone `Schema::step()`. |

AV01 follows the revised D4 contract. [Binding work plan](ARRAY_VECTOR_CODEX_WORKPLAN.md#mandatory-per-family-task-split-and-evidence-review-driven-superseding-execution-cards)
