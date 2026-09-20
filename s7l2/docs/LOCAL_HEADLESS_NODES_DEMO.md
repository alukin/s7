# Local Headless Nodes Demo

This guide shows how to run two local headless `s7runtime` nodes and discover them in `s7studio`.

## 1) Build

```bash
cmake -S /home/al/projects/s7/s7l2 -B /home/al/projects/s7/s7l2/build
cmake --build /home/al/projects/s7/s7l2/build -j2
```

## 2) Start two local nodes (plain WebSocket)

Terminal A:

```bash
/home/al/projects/s7/s7l2/build/s7runtime/s7runtime \
  --schema /home/al/projects/s7/s7l2/s7runtime/examples/schema_minimal.json \
  --start-net \
  --seed-peer ws://127.0.0.1:9003 \
  --serve-forever
```

Terminal B (copy schema and change server port to 9003, then run):

```bash
/home/al/projects/s7/s7l2/build/s7runtime/s7runtime \
  --schema /home/al/projects/s7/s7l2/s7runtime/examples/schema_minimal_node2.json \
  --start-net \
  --seed-peer ws://127.0.0.1:9002 \
  --serve-forever
```

Note: create `schema_minimal_node2.json` from `schema_minimal.json` and set `websocket.server.port` to `9003`.

## 3) Open Studio and discover nodes

```bash
/home/al/projects/s7/s7l2/build/s7studio/s7studio
```

In Studio:

1. Open the `Headless Nodes` dock.
2. Add seed `ws://127.0.0.1:9002` and `ws://127.0.0.1:9003`.
3. Click `Refresh` (or enable auto refresh).
4. Check `Trusted nodes` / `Untrusted or unknown nodes` lists.
5. Double-click a node to open/update a node-specific tab in the panel.

## 4) Optional mTLS run

Test certs are available in:

- `/home/al/projects/s7/s7l2/s7runtime/examples/certs`

To regenerate:

```bash
/home/al/projects/s7/s7l2/tools/generate_test_runtime_certs.sh
```

For `wss://` peers in Studio, fill CA/cert/key fields in the `Headless Nodes` dock.

## 5) Stop nodes

Press `Ctrl+C` in each runtime terminal.

