# S7 File I/O OOP Design (s7io/s7fileio)

**Status:** Active  
**Purpose:** Object-oriented file I/O architecture and codec design  
**Last Updated:** May 13, 2026

---

**Date:** May 10, 2026  
**Status:** Implemented prototype

## Why this redesign

`FileSink`/`FileSource` were not expressive enough for SDR-oriented workflows.

We now separate:
- real and complex pipelines into dedicated classes
- scalar data representation (`f64`, `f32`, `s32`, `s16`, `s8`, `u8`)
- metadata placement (inline in binary header or sidecar `.json`)

## Class model

- `IScalarCodec` (strategy interface)
  - encode/decode one numeric scalar
  - concrete codecs for floating and integer widths
- `FileSinkBase` / `FileSourceBase`
  - common file-header and payload logic
  - common tick lifecycle
- `RealFileSink` / `ComplexFileSink`
  - domain-specific input handling and interleaving
- `RealFileSource` / `ComplexFileSource`
  - domain-specific output decoding
- No compatibility wrappers
  - only explicit classes are supported: `RealFileSink`, `ComplexFileSink`, `RealFileSource`, `ComplexFileSource`

## File model (`s7.io.file.v1`)

Binary header contains:
- magic/version
- domain (`real` or `complex`)
- scalar encoding
- channel count
- sample rate
- sample count
- metadata placement flag

Payload is interleaved by channel and sample.

Complex streams store IQ as two encoded scalars per channel per sample.

## Sidecar metadata

`FileIOOptions` supports writing metadata to a separate file with the same base name and different extension (default `.json`).

Example:
- data: `/tmp/test_real.s7io`
- header sidecar: `/tmp/test_real.json`

## Current demo coverage

`main.cpp` demonstrates:
- `RealFileSink` with `s16` encoding + sidecar metadata
- `RealFileSource` decode path
- `ComplexFileSink` with `s8` IQ encoding
- `ComplexFileSource` decode path

## Next extension points

- `s7memio`: shared-memory sink/source with same codec contracts
- `s7netio`: network frames using same domain/encoding metadata
- container adapters for WAV/GR/etc. + decode/encode filter elements

