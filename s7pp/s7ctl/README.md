# s7ctl

`s7ctl` is a lightweight command-line client for the S7 runtime control channel.

It sends JSON requests over the RSocket request-response path.

## Build

```bash
cd /home/al/projects/s7/s7pp/build_verify
cmake -Wno-dev -DCMAKE_BUILD_TYPE=Release -DRSOCKET_INSTALL_DEPS=ON ..
cmake --build . -j4
```

## Usage

```bash
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl hello
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl types
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl status
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl load-file --schema-path /home/al/projects/s7/s7pp/s7main/examples/io_file_real_sink.json
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl validate --schema-handle schema-1
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl run --schema-handle schema-1 --ticks 16
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl pause --run-id run-1
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl resume --run-id run-1
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl stop --run-id run-1
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl reset --schema-handle schema-1
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl unload --schema-handle schema-1
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl raw --method runtime.status --params '{}'
```

Supported commands:

- `hello`
- `types`
- `status`
- `load-file --schema-path <path>`
- `load-inline --schema-json <json>`
- `validate --schema-handle <id>`
- `run --schema-handle <id> [--ticks <n>]`
- `pause --run-id <id>`
- `resume --run-id <id>`
- `stop --run-id <id>`
- `reset --schema-handle <id>`
- `unload --schema-handle <id>`
- `raw --method <name> [--params <json>]`

Options:
- `--host` (default: `127.0.0.1`)
- `--port` (default: `5577`)

## End-to-end smoke

Terminal 1:

```bash
cd /home/al/projects/s7/s7pp/build_verify
./s7main/s7main --server --port 5577
```

Terminal 2:

```bash
cd /home/al/projects/s7/s7pp/build_verify
./s7ctl/s7ctl hello
./s7ctl/s7ctl types
```

