# Local Headless Nodes Demo

Run two local headless `s7runtime` nodes and discover them in `s7studio`. Commands are run from the `s7l2` directory.

## 1) Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

## 2) Start two local nodes (plain WebSocket)

Each node needs its own state directory (`--state-dir`). The runtime locks the directory, so a second runtime pointed at the same directory exits with an error instead of sharing the identity and peer cache.

Node A (`schema_minimal.json`, server port 9002) produces a stream through a `NetSink`; node B (`schema_minimal_node2.json`, server port 9003) connects to A as a client and consumes that stream through a `NetSource` (matched by `stream_id`). Start A first: a client exits if its first connection attempt fails.

Terminal A:

```bash
./build/s7runtime/s7runtime \
  --schema s7runtime/examples/schema_minimal.json \
  --state-dir /tmp/s7-node-a \
  --start-net \
  --seed-peer ws://127.0.0.1:9003 \
  --serve-forever
```

Terminal B:

```bash
./build/s7runtime/s7runtime \
  --schema s7runtime/examples/schema_minimal_node2.json \
  --state-dir /tmp/s7-node-b \
  --start-net \
  --seed-peer ws://127.0.0.1:9002 \
  --serve-forever
```

Both schemas set `"execution": { "step_rate_hz": 10 }`, so each node steps its startup graph 10 times per second while serving (a deployed diagram with a model-time step `dt` runs in real time, one step every `dt`); `--step-rate <hz>` overrides it (`0` serves the control plane without stepping). The stream is lossless: A keeps what it sent until B acknowledges it, so B, started later, first receives everything A produced before it connected, and a dropped connection is re-established and resumed. A's `NetSink` blocks when its send log is full (B gone for long), and B's `NetSource` waits for data; either side faults after its configured limit (2 s by default). A faulted step stops stepping (`runtime.execution.stopped=error`) while the node keeps serving; `GET_STREAM_STATUS` shows the fault and `RECOVER_STREAM` (mTLS Studio only) clears it and resumes stepping.

Ctrl+C stops a node and prints `runtime.execution.steps`, `runtime.execution.achieved_rate_hz`, the transport counters (`runtime.transport.*`) and one `runtime.stream.<stream_id>` line per stream (role, type, state, fault, packets, sequence).

## 3) Open Studio and discover nodes

```bash
./build/s7studio/s7studio
```

In Studio:

1. Open the `Headless Nodes` dock.
2. Add seed `ws://127.0.0.1:9002` (node B is found through its peer list) or add both seeds.
3. Click `Refresh` (or enable auto refresh).
4. Check the `Trusted nodes` / `Untrusted or unknown nodes` lists. A node is trusted only if its certificate's identity (`spiffe://s7/node/<uuid>`) is the `node_uuid` it reports; plain `ws://` nodes are always untrusted, a mismatch shows as `identity_mismatch`, and trust reported by other nodes is only a claim.

Plain `ws://` nodes listen on 127.0.0.1 only. To reach one from another host, set `"bind_address": "0.0.0.0"` in `websocket.server` (prefer mTLS instead).
5. Double-click a node to open or update its tab.

## 4) Optional mTLS run

Test certificates are in `s7runtime/examples/certs` (development fixtures only). To regenerate them:

```bash
tools/generate_test_runtime_certs.sh
```

Enable `websocket.server.tls` in the schema with `cert_file`, `key_file`, and `ca_file`. For `wss://` seeds in Studio, fill the CA/cert/key fields in the `Headless Nodes` dock.

Deployments (`PREPARE_DEPLOYMENT` / `COMMIT_DEPLOYMENT` / `ABORT_DEPLOYMENT`) are accepted only over mTLS from the Studio certificate pinned in the node's `control` block; see `s7runtime/examples/certs/README.md` and `S7_Control_Protocol_MVP.md` section 10.1. Plain `ws://` nodes refuse them.

## 5) Deploy one diagram across two nodes (mTLS)

Deployments need mTLS and the pinned Studio certificate. `tools/test_two_node_deployment.sh build .` runs the whole sequence: it starts two nodes with `s7_runtime_node` and `s7_runtime_node_b` (startup schemas with `websocket.server.tls` and a `control` block pinning `s7_studio`), deploys a diagram whose elements are placed on both, and redeploys a change. By hand:

```bash
TLS="--cert s7runtime/examples/certs/s7_studio.cert.pem --key s7runtime/examples/certs/s7_studio.key.pem --ca s7runtime/examples/certs/s7_root_ca.cert.pem"
./build/s7ctl/s7ctl check  my_diagram.s7diagram             # streams and each node's part, offline
./build/s7ctl/s7ctl deploy my_diagram.s7diagram $TLS --write my_diagram.deployed.s7diagram
./build/s7ctl/s7ctl streams wss://127.0.0.1:9002 $TLS     # GET_STREAM_STATUS
./build/s7ctl/s7ctl config  wss://127.0.0.1:9003 $TLS     # the deployed diagram and the node's part
./build/s7ctl/s7ctl info    wss://127.0.0.1:9003 $TLS     # NODE_INFO: clock synchronization, pacing (state.timing)
```

In the diagram every schema element has `"node": "<node_uuid>"`, and `runtime_nodes` gives each node's endpoint. `s7ctl` infers stream types with the element types it knows: the built-in ones, plugins found where `s7runtime` looks for them, and those given with `--plugin <file>`, `--plugin-dir <dir>` or `--library <file>`. Deploy again from the `--write` output, so the streams keep their ids and continue. In Studio: place elements with the Node field of the element properties (nodes from the Headless Nodes dock), set the Studio certificate in the dock, then use Run > Deploy to Nodes...; Run > Open Deployed Diagram... shows what a node runs.

## 6) Stop nodes

Press `Ctrl+C` in each runtime terminal.
