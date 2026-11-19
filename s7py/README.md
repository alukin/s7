# s7py — minimal S7 headless node in Python

A headless S7 runtime node written in Python. It speaks the S7 control protocol (`../doc/S7_Control_Protocol_MVP.md`) the same way the C++ `s7runtime` does, so Studio can discover it and Studio or `s7ctl` can deploy diagrams to it, and it runs its part of a diagram for a subset of the built-in elements.

It needs only the Python standard library (3.10+). TLS mode also needs the `cryptography` package (`pip install cryptography`), which it uses to read certificate SAN and Extended Key Usage.

## What it does

- **Control protocol v1**: `HELLO`, `GET_NODE_INFO`, `GET_PEERS`, `PEER_ANNOUNCE`, `LIST_DEPLOYMENTS`, `GET_NETWORK_CONFIGURATION`, `GET_STREAM_STATUS`, `PREPARE_DEPLOYMENT` / `COMMIT_DEPLOYMENT` / `ABORT_DEPLOYMENT` (with `activate_at_ns`), and chunked configuration transfer. Errors, idempotent repeats, limits and expiry follow `s7runtime/src/control_plane.cpp`.
- **Security**: `wss://` with mutual TLS. Mutations are accepted only from the pinned Studio certificate carrying the control-node key purpose, and never on the strength of JSON fields. Plain `ws://` serves read-only requests on loopback.
- **Digests**: RFC 8785 canonical JSON and the `sha256:` configuration digest, checked against the fixture digests computed by `tools/gen_control_fixtures.py`.
- **Execution**: a node's part of a diagram is validated as by `s7core` (parameters against each element's declaration, port indices, one driver per input, every input connected, cycles through causal memory, and one rate per connected set of elements: `../doc/S7_Sample_Rate_Model.md`), compiled into causal order and stepped. Causal memory publishes what it computed at the start of the next step, so every value read after a step belongs to that step; a step that fails leaves every published output as it was. A diagram with `dt` runs on a UTC grid from its activation time and catches up when behind; others run at `execution.step_rate_hz`.
- **Rates**: an element with a time step (`EulerIntegrator`, `TrapezoidIntegrator` `dt`, `AnalogOscillator`, `StepGenerator` and `PWM` `sample_rate_hz`) pins the rate of everything connected to it. Elements that pin none take the deployed diagram's `dt`, or, in the startup graph, its own `dt`, else `execution.step_rate_hz`; a graph with neither is refused. In a deployed diagram every element's own rate must also equal its `dt`. A rate is the exact rational nearest to the configured binary64 value (so 0.0010000001 s and 0.0010000002 s differ, and 44.1 kHz stays 44.1 kHz), as `s7::Rate::from_seconds`.
- **Elements** (F64 only): `Constant`, `Gain`, `Adder`, `Multiply`, `Delay`, `PreviousValue`, `Accumulator`, `EulerIntegrator`, `TrapezoidIntegrator` (implicit by default, all its states solved together each step, or `"mode": "sampled"`; `../s7l2/docs/TRAPEZOID_INTEGRATION.md`), `AnalogOscillator`, `StepGenerator`, `PWM`; stateless math and control: `Trig`, `Divider`, `MathFunction`, `Power`, `Saturation`, `MinMax`, `Sign`, `Comparator`, `Switch`, `PhaseWrap`; `HysteresisRelay`. Math is computed with the same libm functions as `s7core`, so results agree to the bit; where libm returns NaN or an infinity (`sin(inf)`, `pow(-0.0, -1)`, `exp(1000)`), so does s7py, but the sign of a NaN is not reproduced. Domain errors that `s7core` reports as execution errors (a zero `Divider` denominator, `sqrt` of a negative number, a non-finite `Switch` control, `PhaseWrap` or `HysteresisRelay` input, or `PWM` duty) stop execution likewise.
- **Data streams** (`../doc/S7_Network_Wire_Protocol_v1.md`, `../doc/S7_Net_Element_Contract.md`): a connection between elements on different nodes is a stream, carried by a `NetSink` on the producer node and a `NetSource` on each consumer node, which the node adds to its part of the diagram as `s7net` does. The wire format (every type of section 8, 12-bit packing, `TIMESTAMPS` and `SEND_TIME` extensions, `ACK` / `RESUME`), batching, the send log with backpressure, cumulative acknowledgements, resume and replay, sequence-range reservations (`<state-dir>/streams/<stream_id>.seq`, the C++ node's format), timestamp profiles, end-to-end latency, `GET_STREAM_STATUS` and `RECOVER_STREAM` follow `s7transport`. A consumer node connects to each producer node's `/data` endpoint (`runtime_nodes[].data_endpoint`, else `endpoint`) over mTLS, announcing itself in the `S7-Node` handshake header, and RESUMEs its streams after every (re)connect; each stream is accepted only from its producer node and served only to its consumer nodes. The graph steps in a worker thread, so a `NetSource` can wait for data and a `NetSink` for room in the send log while the node keeps serving; a stream fault stops stepping until `RECOVER_STREAM`. s7py graphs carry F64 streams; the codec reads and writes every wire type. Not supported: a separate data listener and `trusted_plaintext` data (ADR-020, `../s7l2/docs/SPLIT_TRANSPORT.md`); this node refuses a deployment that needs them.
- **State directory**: holds the node UUID (taken from the certificate's `spiffe://s7/node/<uuid>` SAN in TLS mode), the peer cache, and the active deployment, which is restored on restart. The directory is locked against a second process.
- **Clock**: synchronization is read from the kernel (`ntp_adjtime`) on Linux. Use `"clock": {"assume_synchronized": true}` elsewhere or in tests.

## Frame transport service

The opt-in [`s7node.frames`](s7node/frames.py) service matches the new C++ frame
transport: raw numeric frames, optional independent LZ4/Zstandard compression,
immutable bounded leases, consumption ACKs/replay and ordered source faults.
All 25 whole-byte numeric types preserve their bits. This service is separate
from the deployed F64 graph path and is not advertised in node capabilities.
See [cross-language APIs, dependencies and conformance](../doc/S7_Frame_Interoperability.md).
For optional compression, install `requirements-frames.txt`; raw framing and
the existing node remain usable without those providers.

## Not implemented

- A separate data listener and the `trusted_plaintext` data profile.
- Signal types other than F64, nested schemas, C++ plugins and schema libraries.
- Outbound peer probing: seed peers and announcements are only listed.

## Run

From this directory:

```bash
# plain ws://127.0.0.1:9012, steps a counter at 10 Hz and prints it once per second
python3 -m s7node --schema examples/node_plain.json --state-dir /tmp/s7py-a --watch count

# mTLS on wss://127.0.0.1:9012 as node 33333333-… (s7l2 test certificate), the test Studio pinned
python3 -m s7node --schema examples/node_tls.json --state-dir /tmp/s7py-b --watch position --watch velocity
```

Then deploy to it from `../s7l2`:

```bash
TLS="--cert s7runtime/examples/certs/s7_studio.cert.pem --key s7runtime/examples/certs/s7_studio.key.pem --ca s7runtime/examples/certs/s7_root_ca.cert.pem"
./build/s7ctl/s7ctl deploy ../s7py/examples/damped_oscillator.s7diagram $TLS
./build/s7ctl/s7ctl info wss://127.0.0.1:9012 $TLS
```

The node schema file uses the `s7runtime` format: `websocket.server` (`port`, `bind_address`, `tls` with `cert_file` / `key_file` / `ca_file`), `control` (`studio_key_purpose_oid`, `studio_certificate_sha256`, `max_configuration_bytes`), `clock`, `execution.step_rate_hz`, an optional `node_name`, and an optional startup graph (`dt`, `elements`, `connections`). `examples/node_tls.json` pins the fingerprint of the current test Studio certificate. Regenerating the s7l2 test certificates changes that fingerprint, so update it with `openssl x509 -in s7_studio.cert.pem -noout -fingerprint -sha256`.

`--lib mylib.py` (repeatable; a `.py` file or a module name) loads an element library: its
`register(registry)` is called, or else every public `s7core.Element` subclass it defines is
registered. The node runs and advertises those types with the built-in ones. A library runs with
the node's privileges, so load only code you trust.

Options: `python3 -m s7node --help`.

## Writing an element

An element type is a subclass of `s7core.Element` that declares its ports and parameters and
computes its outputs in `step()`; [`examples/elements/smoothing.py`](examples/elements/smoothing.py)
has two:

```python
from s7core import Element, Param, Port

class Offset(Element):
    description = "Adds a constant to its input"
    input_ports = (Port("in", "Input"),)          # output_ports defaults to one "out"
    parameters = (Param("offset", "real", 0.5, "Added to the input"),)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.offset = self.read(params, "offset")  # the declared default if omitted

    def step(self, inputs):
        self.outputs[0] = inputs[0] + self.offset
```

- **Causality.** By default an element has direct feedthrough: `step()` sees this step's inputs and
  its outputs are read by later elements in the same step. Set `memory = True` for causal memory:
  `step()` still receives this step's inputs, but what it writes to `self.outputs` is published at the
  start of the next step, so the element can close a feedback loop; a loop without one is refused.
  Set `stateful = True` if the element keeps anything between steps.
- **Failures.** Raise `SchemaError` from `__init__` for bad parameters, any other exception from
  `step()` for an execution error. A failed step leaves every published output as it was and is
  retried; the element's own state is not rolled back, so change it only once nothing can fail.
- **Parameters** are checked against the declaration (an unknown name, a wrong type, an `int` out of
  `min`..`max`, an `enum` value not listed); types are `value`/`real`, `int`, `bool`, `enum`, `array`.
  A generic element declares `type_variables = (T,)`, `GENERIC` parameters and ports with
  `type_rule="T"`, as the built-in elements do; s7py runs it as F64.
- **Registration** checks the declaration (`DeclarationError`): `Registry(LIBRARY).register(Offset)`,
  or `--lib` on the command line. `Offset.describe()` is the catalog entry tools see.

Run a schema locally, without a node or network; each row is the outputs after a step, bit for bit:

```bash
python3 -m s7node.trace examples/smoothing.json --lib examples/elements/smoothing.py --steps 8 --order
python3 -m s7node.trace --describe MovingAverage --lib examples/elements/smoothing.py
```

`--order` prints the causal order and the causal memory, `--watch ID[:PORT]` picks outputs, and an
`.s7diagram` runs at its `dt` as when deployed. Then serve the library from a node with
`python3 -m s7node --schema … --lib examples/elements/smoothing.py`: its startup graph may use the
library's types and `GET_NODE_INFO` lists them. Deploying a diagram that uses them with `s7ctl` or
Studio does not work yet: both check element types against the C++ registry, and the node's catalog
carries type names only.

## Test

```bash
python3 -m unittest discover -s tests
```

`tools/check_control_fixtures.py` runs the control protocol conformance script (`../s7l2/s7runtime/tests/fixtures/control_v1.json`) against any node. `--skip-streams` leaves out the steps that need a data stream: they expect stream `5d4c3b2a-…`, which only the C++ fixture harness creates, so a live node (C++ or s7py) runs the script with it. The test suite runs it against this node over mTLS. `tests/test_wire.py` checks the codec against the shared wire fixtures (`../s7l2/s7transport/tests/fixtures/wire_v1.json`), `tests/test_stream.py` the delivery rules with the scenarios of `s7transport`'s `stream_protocol_smoke.cpp`, and `tests/test_transport.py` two endpoints over loopback WebSocket connections, including a dropped connection.

## Layout

Three packages, each depending only on the ones before it: `s7core` (no element library, no network) ← `s7lib` ← `s7node`.

| File | Content |
|---|---|
| `s7core/element.py` | The element contract: declarations, flags, `read()`, `step()`, `describe()` |
| `s7core/spec.py` | Declarations: `Port`, `Param`, `TypeVariable`, `Variadic` (as s7core's `ElementMetadata`) |
| `s7core/registry.py` | `Registry`: element types by name, declarations checked on registration, `describe()` |
| `s7core/wiring.py` | Elements built from the given types with checked parameters; one driver per input |
| `s7core/schedule.py` | Causal order; a cycle must pass through causal memory |
| `s7core/rate_groups.py` | One rate per connected set of elements; the model-time rate check |
| `s7core/trapezoid.py` | The `TrapezoidIntegration` interface and the coupled implicit-trapezoid stage |
| `s7core/graph.py` | `Graph`: builds the above and steps them (publish, evaluate, stage, roll back on failure) |
| `s7core/rate.py`, `params.py`, `errors.py` | Exact rates, parameter value checks, `SchemaError`, `DeclarationError` |
| `s7lib/` | The built-in elements (`basic`, `integrators`, `sources`, `mathops`, `control`) and their `LIBRARY` registry |
| `s7node/net.py` | `NetSink` / `NetSource`, the stream ends, given the node's stream table when a graph is built |
| `s7node/elements.py` | The registry a node serves: the library and the stream ends |
| `s7node/library.py` | Element libraries loaded with `--lib` |
| `s7node/trace.py` | Local run of a schema, printing outputs step by step |
| `s7node/graph.py` | Compatibility: the former single-file module's names |
| `s7node/control.py` | Control protocol messages, authorization, deployments, transfers, persistence |
| `s7node/runtime.py` | Executor: grid / rate pacing, scheduled activation |
| `s7node/diagram.py` | A node's part of a deployed diagram, with its stream ends |
| `s7node/wire.py` | Data stream wire format v1 |
| `s7node/stream.py` | Stream descriptors, lossless delivery (producer, consumer, sequence reservations) |
| `s7node/transport.py` | Stream table and `/data` connections between nodes |
| `s7node/jcs.py` | RFC 8785 canonical JSON, configuration digest |
| `s7node/websocket.py` | Minimal RFC 6455 server and client on asyncio |
| `s7node/tls.py` | TLS contexts, certificate identity (fingerprint, SAN, EKU) |
| `s7node/clock.py` | UTC clock synchronization state |
| `s7node/node.py` | Configuration, state directory, server, CLI |

## License

GPL-3.0-or-later; the examples are CC0-1.0. See [`../LICENSING.md`](../LICENSING.md).
