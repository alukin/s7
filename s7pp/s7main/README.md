# s7main (Computation Node CLI)

` s7main ` is the computation-node executable for S7.

## Current responsibilities

- register node element libraries (`s7stdlib`, `s7fileio`, `s7memio`, `s7netio`)
- load netlist schema JSON
- validate schema build graph
- execute schema for ticks or seconds
- start websocket control server for JSON-RPC runtime commands

## CLI usage

```bash
s7main --help
s7main --list-types
s7main --server --port 5577
s7main --schema <path> --validate-only
s7main --schema <path> --ticks <n>
s7main --schema <path> --seconds <s>
```

## Example

```bash
./s7main/s7main --schema ../s7main/examples/classical_oscillator_2integrators.json --ticks 1000
```

I/O preparation examples are in `s7main/examples/` and can be run with:

```bash
./s7main/examples/run_io_examples.sh
```

## Next planned responsibilities

- HTTP REST control plane
- WebSocket event streaming
- command handling for deploy/start/stop/query
- runtime status/event stream publication

