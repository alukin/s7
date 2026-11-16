# Headless runtime examples

These JSON files are schema or library inputs for `s7runtime`; they are separate
from the [Studio diagram gallery](../../examples/README.md). Run commands below
from the repository root after building the runtime. Use a separate `--state-dir`
for each serving node. The certificates under [certs](certs/README.md) are test
material for local experiments.

| File | Purpose |
|---|---|
| [schema_minimal.json](schema_minimal.json) | A 10 Hz constant source connected to a network sink; starter service/node configuration. |
| [schema_minimal_node2.json](schema_minimal_node2.json) | Companion node: network source followed by a gain of two. Use with the minimal source in a two-node experiment. |
| [schema_frame_sink.json](schema_frame_sink.json) | Two constant channels packed into frames for a network sink at 48 kHz. |
| [schema_frame_source.json](schema_frame_source.json) | Companion frame source and left-channel gain at 48 kHz. The frame pair demonstrates an explicit network boundary. |
| [schema_oscillator_gnuplot.json](schema_oscillator_gnuplot.json) | A semi-implicit, two-state digital oscillator with a `Delay` closing the causal loop; writes its signal and an analog sine reference to gnuplot files. Its simulation block runs 20 periods at 1 kHz. |
| [schema_nested_schema.json](schema_nested_schema.json) | A nested `Schema` block exposes input and output ports between a constant source and a gnuplot file sink. Its simulation block runs one step. |
| [schema_library_demo.json](schema_library_demo.json) | A reusable schema library passed with `--library`; it supplies element definitions, not a standalone simulation. |
| [schema_fourier_coefficient.json](schema_fourier_coefficient.json) | A 32 Hz sine analyzed by the optional Fourier coefficient plugin, with a C64-to-real/imaginary adapter. The simulation block runs 1024 steps. |
| [schema_wavelet_coefficient.json](schema_wavelet_coefficient.json) | The corresponding optional causal wavelet coefficient graph, also split into real and imaginary parts. The simulation block runs 1024 steps. |

For a finite local run:

```sh
./build/s7runtime/s7runtime --schema s7runtime/examples/schema_oscillator_gnuplot.json
```

For a serving node:

```sh
./build/s7runtime/s7runtime --schema s7runtime/examples/schema_minimal.json --state-dir /tmp/s7-node-a --serve-forever
```

The analysis schemas need the optional analysis plugins; see the
[in-graph analysis guide](../../examples/analysis_elements/README.md).
The runtime [README](../../README.md) explains service stepping and network
startup; the [local headless-node demo](../../docs/LOCAL_HEADLESS_NODES_DEMO.md)
shows a two-node setup.
