# Local Headless Nodes Demo

Run two local headless `s7runtime` nodes and discover them in `s7studio`. Commands are run from the `s7l2` directory.

## 1) Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

## 2) Start two local nodes (plain WebSocket)

Each node needs its own state directory (`--state-dir`). The runtime locks the directory, so a second runtime pointed at the same directory exits with an error instead of sharing the identity and peer cache.

Terminal A (server port 9002 from `schema_minimal.json`):

```bash
./build/s7runtime/s7runtime \
  --schema s7runtime/examples/schema_minimal.json \
  --state-dir /tmp/s7-node-a \
  --start-net \
  --seed-peer ws://127.0.0.1:9003 \
  --serve-forever
```

Terminal B (server port 9003 from `schema_minimal_node2.json`):

```bash
./build/s7runtime/s7runtime \
  --schema s7runtime/examples/schema_minimal_node2.json \
  --state-dir /tmp/s7-node-b \
  --start-net \
  --seed-peer ws://127.0.0.1:9002 \
  --serve-forever
```

Both example schemas set `"execution": { "step_rate_hz": 10 }`, so each node steps its graph 10 times per second while serving and its `NetSink` publishes to a connected peer; packets published while no peer is connected are dropped. Use `--step-rate <hz>` to override the rate (`0` serves the control plane without stepping). Ctrl+C stops the node and prints `runtime.execution.steps` and `runtime.execution.achieved_rate_hz`.

## 3) Open Studio and discover nodes

```bash
./build/s7studio/s7studio
```

In Studio:

1. Open the `Headless Nodes` dock.
2. Add seed `ws://127.0.0.1:9002` (node B is found through its peer list) or add both seeds.
3. Click `Refresh` (or enable auto refresh).
4. Check the `Trusted nodes` / `Untrusted or unknown nodes` lists. Plain `ws://` nodes are always untrusted.
5. Double-click a node to open or update its tab.

## 4) Optional mTLS run

Test certificates are in `s7runtime/examples/certs` (development fixtures only). To regenerate them:

```bash
tools/generate_test_runtime_certs.sh
```

Enable `websocket.server.tls` in the schema with `cert_file`, `key_file`, and `ca_file`. For `wss://` seeds in Studio, fill the CA/cert/key fields in the `Headless Nodes` dock.

Deployments (`PREPARE_DEPLOYMENT` / `COMMIT_DEPLOYMENT` / `ABORT_DEPLOYMENT`) are accepted only over mTLS from the Studio certificate pinned in the node's `control` block; see `s7runtime/examples/certs/README.md` and `S7_Control_Protocol_MVP.md` section 10.1. Plain `ws://` nodes refuse them.

## 5) Stop nodes

Press `Ctrl+C` in each runtime terminal.
