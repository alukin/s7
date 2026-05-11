# s7io

`s7io` is the I/O element library for S7.

## Current module

- `s7fileio` (implemented)
  - `RealFileSink`
  - `ComplexFileSink`
  - `RealFileSource`
  - `ComplexFileSource`
- `s7memio` (first slice implemented)
  - `RealMemSink`
  - `RealMemSource`
  - `ComplexMemSink`
  - `ComplexMemSource`
- `s7netio` (prototype slice implemented)
  - `RealNetSink`
  - `RealNetSource`
  - `ComplexNetSink`
  - `ComplexNetSource`

## Design goals

- Separate OOP classes for real and complex streams
- Pluggable scalar encodings for SDR-compatible formats
- Optional JSON sidecar headers (`same-name.json`)
- Binary payload with deterministic interleaved layout
- Forward path for `s7memio` and `s7netio`

## Shared-memory layout (`s7memio`, slice v1)

- POSIX shared memory segment (`shm_open` + `mmap`)
- SPSC ring (`write_seq`/`read_seq` atomics in shared header)
- real + complex domains
- scalar transport encoding: `f32`, `s16`, `s8`, `u8`
- degraded behavior:
  - ring full at sink: drop oldest block, increment overflow counter
  - ring empty at source: zero outputs when `zero_on_underrun=true`

## Network layout (`s7netio`, slice v0)

- endpoint-oriented channel config (`ws://host:port/path` style string)
- per-endpoint bounded queue with overflow/drop accounting
- real + complex stream families
- current backend uses Beast websocket server/client sockets locally
- API is transport-stable and prepared for a future remote backend

## Scalar encodings

- `f64`
- `f32`
- `s32`
- `s16`
- `s8`
- `u8`

For complex streams, each channel sample stores `I` and `Q` as two encoded scalars.

## File layout (`s7.io.file.v1`)

- binary header (`magic=S7IO`, version, domain, encoding, channel count, sample rate, count, metadata marker)
- optional inline metadata JSON
- interleaved payload bytes

If `write_json_sidecar=true`, metadata is emitted to sidecar (`.json`) and omitted from the binary header.

## Quick try

```bash
cd /home/al/projects/s7/s7pp/cmake-build-debug
cmake --build .
./s7main/s7main
```

