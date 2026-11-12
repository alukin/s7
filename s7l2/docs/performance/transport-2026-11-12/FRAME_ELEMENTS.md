# Frame element-library and Studio integration — 2026-11-12

`NetFrameSource` and `NetFrameSink` are now executable `s7net` elements and appear
in Studio's Network library. They support named scalar channel ports, inferred
whole-byte numeric types, exact logical frame rates, explicit raw/LZ4/Zstandard
selection and bounded endpoint batching. One graph evaluation remains one frame.

The runtime's failed-domain notification latches these new boundaries after an
exception. Retry cannot silently consume or publish another frame. Existing
causal-memory ordering, legacy element retry behavior and previously completed
rate-domain samples retain their semantics. Normal close wakes blocked frame
operations and reports a closed stream without inventing a malformed-packet fault.

## Validation

- Full fresh headless Release CTest: **59/59 passed**.
- The new graph-element test exercises raw/LZ4/Zstandard over in-memory,
  actual WebSocket and mutual-TLS endpoints; exact changing stereo values,
  downstream type inference, partial batches, stream counters and Delay causality.
- Failure tests cover downstream exceptions, retry refusal, invalid descriptors,
  unavailable resources, unauthorized consumers, blocked receive cancellation,
  healthy close status and refusal of legacy frame recovery.
- Studio's library/port/configuration/save workflow and documentation coverage:
  **4 Qt Test checks passed**, including initialization/cleanup. The tests verify
  default stereo counts, custom named connectors and diagram serialization.
- Shared C++/Python/Java conformance: **789 checks passed**, all nine directed
  pairs, with optional codecs present and no unavailable-codec skips.
- Both canonical frame schemas compile and start through the CLI over mTLS.
  That startup check disables stepping; actual changing frame values are checked
  by the socket graph test above.

Host: Linux x86-64, GCC 16.2.1, Release, Qt 6.11.2. GUI checks run in Xvfb. This
increment does not claim Windows qualification, hardware audio, a sustained
throughput envelope or automatic frame-route deployment/resource admission.
Python/Java conformance qualifies the service/wire formats, not new graph elements
in those runtimes. See the [element guide](../../FRAME_ELEMENTS.md) for the
explicit peer-connection and graph-lifecycle limits.

Reproduction (build directory names are arbitrary):

```sh
cmake -S . -B /tmp/s7-transport-resume -DS7_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/s7-transport-resume --target build-tests --parallel 3
ctest --test-dir /tmp/s7-transport-resume --output-on-failure -j 3

cmake --build build-release --target s7studio s7studio_gui_workflows_test s7studio_gui_capture --parallel 3
python3 tools/run_gui_test.py --artifacts /tmp/s7-frame-studio-final \
  --capture build-release/s7studio/s7studio_gui_capture -- \
  build-release/s7studio/s7studio_gui_workflows_test \
  frameElementsLibraryAndPorts elementDocumentationCoverage
```

The [conformance guide](../../../../doc/S7_Frame_Interoperability.md) gives the
optional provider setup and strict three-language checker command. Source hashes,
final CTest output, Qt Test XML and the rendered library capture are retained in
[frame-elements-validation.json](frame-elements-validation.json),
[frame-elements-headless.txt](frame-elements-headless.txt),
[frame-elements-gui.xml](frame-elements-gui.xml) and
[frame-elements-studio.png](frame-elements-studio.png).
