# S7 Phase 2 Prep: Running Models with `s7io` Modules

## Goal

Use the same `s7main` netlist execution path to run models that include:

- `s7fileio`
- `s7memio`
- `s7netio` (RSocket-oriented API, current in-process backend)

Dependency setup guide: `docs/S7_RSOCKET_DEPENDENCIES.md`

## Models

Located in `s7main/examples/`:

- `io_file_real_sink.json`
- `io_mem_real_sink.json`
- `io_net_real_loopback_to_file.json`

## Quick Run

```bash
cd /home/al/projects/s7/s7pp
./s7main/examples/run_io_examples.sh
```

## Manual Runs

```bash
cd /home/al/projects/s7/s7pp/build_verify
./s7main/s7main --list-types
./s7main/s7main --schema ../s7main/examples/io_file_real_sink.json --ticks 16
./s7main/s7main --schema ../s7main/examples/io_mem_real_sink.json --ticks 16
./s7main/s7main --schema ../s7main/examples/io_net_real_loopback_to_file.json --ticks 16
```

## Notes for `s7netio`

- Public API and element names are now wired into registry and `s7main` startup registration.
- Current backend is in-process endpoint routing (deterministic and test-friendly).
- Endpoint string is part of `NetIOConfig`; JSON factory currently uses numeric params only in v0.
- Planned Phase 2 step: swap broker backend to RSocket transport with same element API.

## `s7main` control modes (Phase 2 scaffold)

`s7main` now exposes control-plane entry points:

- `--control-stdio` for JSON-RPC over stdin/stdout (transport-agnostic dispatcher testing)
- `--server --port 5577` for RSocket server mode

Smoke command:

```bash
cd /home/al/projects/s7/s7pp
./s7main/examples/control_stdio_smoke.sh
```

