# S7 Shared Memory I/O (`s7memio`)

**Status:** Active  
**Purpose:** Shared-memory I/O architecture and SPSC ring buffer design  
**Last Updated:** May 13, 2026

---

**Date:** May 10, 2026  
**Status:** v1 implemented (`RealMem*`, `ComplexMem*`, scalar encodings)

## Scope of this implementation

- Linux baseline (`shm_open`, `mmap`)
- Single-producer/single-consumer ring
- Real + complex streams
- `f32`, `s16`, `s8`, `u8` transport encodings
- Block-based transfer (`block_size` samples/channel per slot)

## Classes

- `RealMemSink` / `RealMemSource`
  - N real channels
  - blockized shared-memory transfer
- `ComplexMemSink` / `ComplexMemSource`
  - N complex IQ channels
  - interleaved IQ scalar pairs per channel

## Ring behavior

- Header in shared memory stores shape + counters:
  - `num_channels`, `block_size`, `capacity_blocks`, `sample_rate_hz`
  - `write_seq`, `read_seq`
  - `overflow_count`, `underrun_count`
- Payload is contiguous encoded scalar bytes

### Overflow policy

If producer finds ring full, it drops oldest block (advances `read_seq`) and continues writing.

### Underrun policy

If consumer finds ring empty:
- increments underrun counter
- outputs zeros if configured (`zero_on_underrun=true`)

## Current constraints

- No multi-producer or multi-consumer support
- No eventfd signaling yet (poll-by-tick model)
- Complex memio included

## Planned next steps

1. Add optional eventfd wakeups
2. Add reconnect/epoch semantics for producer restarts
3. Add multi-producer or router process pattern (if needed)
4. Align header/frame envelope with network transport envelope

## Demo

`main.cpp` includes a tiny in-process demo:
- writes two blocks with `RealMemSink`
- reads back with `RealMemSource`
- prints block counters and sample outputs


