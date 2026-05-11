# s7ctl

`s7ctl` is a lightweight command-line client for the S7 runtime control channel.

It sends JSON-RPC requests over a websocket connection to `s7main --server`.

## Build

```bash
cd /home/al/projects/s7/s7pp/tmp/no-rsocket-build
cmake -S /home/al/projects/s7/s7pp -B /home/al/projects/s7/s7pp/tmp/no-rsocket-build
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
/home/al/projects/s7/s7pp/build_verify/s7ctl/s7ctl events --topics schema,run --count 3
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
- `events --topics <csv> [--count <n>]`

Options:
- `--host` (default: `127.0.0.1`)
- `--port` (default: `5577`)
- `--path` (default: `/`)

## End-to-end smoke

Terminal 1:

```bash
cd /home/al/projects/s7/s7pp/tmp/no-rsocket-build
./s7main/s7main --server --port 5577
```

Terminal 2:

```bash
cd /home/al/projects/s7/s7pp/tmp/no-rsocket-build
./s7ctl/s7ctl hello
./s7ctl/s7ctl types

# Or run the full smoke script (uses tmp/no-rsocket-build by default):
S7_BUILD_DIR=/home/al/projects/s7/s7pp/tmp/no-rsocket-build ./s7ctl/smoke.sh
```

