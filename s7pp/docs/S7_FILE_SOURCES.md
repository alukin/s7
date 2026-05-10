# S7 File-Based Sources and Sinks

> NOTE: This document describes the earlier `s7sources`/`FileSink`/`FileSource` design.
> The current implementation is `s7io/s7fileio` with explicit OOP classes:
> `RealFileSink`, `ComplexFileSink`, `RealFileSource`, `ComplexFileSource`.
> See `docs/S7_FILE_IO_OOP.md` and `s7io/README.md` for the active design.

**Date:** May 10, 2026  
**Status:** Implemented v0  
**Related:** [`s7sources/include/s7sources.hpp`](../s7sources/include/s7sources.hpp), [`docs/S7_BUILTIN_PLUGIN.md`](S7_BUILTIN_PLUGIN.md)

---

## Overview

The **s7sources** library provides essential I/O elements for file-based data acquisition and recording:

1. **FileSink** — Save N channels of interleaved samples to disk (Real or Complex)
2. **FileSource** — Load N channels of interleaved samples from file  
3. **S7SS file format** — Custom S7 Sample Stream format with metadata header

This library is **statically linked** like s7stdlib, ensuring all computational nodes have file I/O capabilities. The design supports **future extensibility** for WAV, FLAC, GNU Radio, and other formats.

---

## Architecture

### Files

| File | Purpose |
|------|---------|
| `s7sources/include/s7sources.hpp` | C++ interface for FileSink, FileSource, and file format helpers |
| `s7sources/src/s7sources.cpp` | Implementation of file I/O and sample buffering |
| `s7sources/src/s7sources_registry.cpp` | ElementRegistry integration for JSON factory instantiation |
| `s7sources/src/s7sources_plugin.cpp` | Plugin ABI implementation (pseudo-plugin interface) |
| `s7sources/include/s7sources_plugin.hpp` | Plugin entry point declarations |
| `s7sources/CMakeLists.txt` | Build configuration |

### Build Integration

- `s7sources/` is a separate static library (`libs7sources.a`) that links to `libs7core.a`
- Main executable links both: `s7main` → `s7core` + `s7sources`
- Registration: `registerSources(registry)` called during runtime initialization
- Plugin interface exposed via C ABI (same as s7stdlib)

---

## File Format: S7SS (S7 Sample Stream)

### Design Goals

- **Simple and self-describing** — header contains all synchronization metadata
- **Streaming-friendly** — samples written in interleaved order, no seeking needed
- **Multi-channel ready** — supports arbitrary channel counts with mixed Real/Complex types
- **Metadata-rich** — embeds JSON for deployment context, model descriptor, etc.

### Binary Layout

```
[Header]
  magic:             4 bytes   = 0x53375353 ("S7SS")
  format_version:    4 bytes   = 0x00000001 (big-endian)
  num_channels:      4 bytes   = N (big-endian, must be > 0)
  sample_type:       1 byte    = 0 (Real) or 1 (Complex)
  sample_rate_hz:    8 bytes   = double (big-endian, IEEE 754)
  timestamp_unix:    8 bytes   = double (big-endian, Unix ms timestamp)
  num_samples:       8 bytes   = S (big-endian, samples per channel)
  metadata_len:      4 bytes   = L (big-endian, JSON string length)
  metadata_json:     L bytes   = arbitrary JSON (UTF-8)

[Data]
  Interleaved samples:
    For Real:   S * N * 8 bytes (N 64-bit floats per sample)
    For Complex: S * N * 16 bytes (2N 64-bit floats per sample: re, im pairs)
```

### Example File Structure

Record 100 samples, 2 Real channels:

```
Header (75 bytes):
  S7SS | v1 | 2 channels | Real | 48000 Hz | timestamp | 100 samples | metadata

Data (1600 bytes):
  [ch0_s0, ch1_s0, ch0_s1, ch1_s1, ..., ch0_s99, ch1_s99]
```

### Endianness and Byte Order

All multi-byte integers and doubles use **big-endian (network) byte order**:
- Facilitates streaming and remote data transfer
- Consistent with network protocols and scientific formats
- Simplicity: no platform-specific handling in basic I/O path

---

## FileSink: Writing Data

### Overview

Records N input channels into an S7SS file. Elements in a schema feed samples to FileSink inputs; each tick accumulates one interleaved sample set. On cleanup, all accumulated samples are written to file with a header.

### Constructor

```cpp
FileSink(id, num_channels, sample_type, output_path, metadata_json)
```

**Parameters:**
- `id` — Element identifier
- `num_channels` — Number of input pins (must match actual schema connections)
- `sample_type` — `SampleType::REAL` or `SampleType::COMPLEX`
- `output_path` — File path to create/overwrite
- `metadata_json` — JSON string with deployment context (default: `"{}"`)

**Inputs:**
- Creates N input ports (one per channel), typed according to `sample_type`

**Outputs:**
- None (sink element)

### Usage Example

```cpp
// Create a 2-channel Real sink
auto sink = std::make_shared<FileSink>("stereo_sink", 2, SampleType::REAL,
                                       "recording.s7ss", 
                                       R"({"model":"oscillator_pair","duration_s":10})");

schema->insert(sine1);
schema->insert(sine2);
schema->insert(sink);

schema->connect(sine1, 0, sink, 0);  // Left channel
schema->connect(sine2, 0, sink, 1);  // Right channel

schema->run(100);  // Accumulates 100 samples
// File written on sink destructor
```

### Metadata Format

Recommended metadata structure (optional):

```json
{
  "model_name": "feedback_oscillator",
  "sample_rate_hz": 48000,
  "duration_s": 5.0,
  "deployment_node": "compute_01",
  "git_commit": "abc123def456",
  "schema_descriptor": { ... }
}
```

---

## FileSource: Reading Data

### Overview

Registers N output channels that replay samples from an S7SS file. On construction, the entire file is loaded into memory. Each tick outputs one interleaved sample set. After final sample, outputs hold the last value.

### Constructor

```cpp
FileSource(id, input_path)
```

**Parameters:**
- `id` — Element identifier
- `input_path` — File path (S7SS format)

**Outputs:**
- Creates N output ports (one per channel), types determined by file header
- Initial value: 0 (updated on first tick)

**Throws:**
- `std::runtime_error` if file missing, invalid magic, or version mismatch

### Usage Example

```cpp
// Load a stereo recording
auto source = std::make_shared<FileSource>("playback", "recording.s7ss");

auto filter = std::make_shared<FIRFilter>(coeffs);
auto sink = std::make_shared<FileSink>("filtered", 1, SampleType::REAL, "output.s7ss");

schema->insert(source);
schema->insert(filter);
schema->insert(sink);

// Apply filter to left channel
schema->connect(source, 0, filter, 0);
schema->connect(filter, 0, sink, 0);

// Playback entire file
schema->run(source->header().num_samples);

std::cout << "Processed " << source->header().num_samples << " samples at "
          << source->header().sample_rate_hz << " Hz\n";
```

### API

```cpp
const FileHeader& header() const;  // File metadata
uint64_t currentSampleIndex() const;  // Current playback position
bool finished() const;  // True if playback exhausted
```

---

## Real vs Complex Data

### Real Type

Single Real (double) per channel per sample:

```
Real channel 0, sample 0: [8 bytes]
Real channel 1, sample 0: [8 bytes]
Real channel 0, sample 1: [8 bytes]
...
```

### Complex Type

Complex pair (real + imag, both double) per channel per sample:

```
Complex ch0 (re), sample 0: [8 bytes]
Complex ch0 (im), sample 0: [8 bytes]
Complex ch1 (re), sample 0: [8 bytes]
Complex ch1 (im), sample 0: [8 bytes]
...
```

**Note:** All complex samples are stored as separate real/imag components in sequence (interleaved real/imag, then next channel).

---

## Memory and Performance

### FileSink

- **Memory:** O(num_channels * num_samples) doubles (while accumulating)
- **Latency:** Synchronous file I/O occurs at flush (destructor or explicit call)
- **Streaming:** Could be extended to periodic block writes for large datasets

### FileSource

- **Memory:** O(num_channels * num_samples) doubles (entire file in RAM)
- **Latency:** File loaded at construction time
- **Streaming:** Not suitable for very large files (>1 GB); addresses with V2 design (streaming reader)

---

## Example: Record and Playback Loop

```cpp
{
    // Record phase
    auto schema1 = std::make_shared<Schema>(1e-9);
    auto osc = std::make_shared<HarmonicOscillator>(params);
    auto sink = std::make_shared<FileSink>("sink", 1, SampleType::REAL, "data.s7ss");

    schema1->insert(osc);
    schema1->insert(sink);
    schema1->connect(osc, 0, sink, 0);
    schema1->run(1000);
    // File written at schema1 destruction
}

{
    // Playback phase
    auto schema2 = std::make_shared<Schema>(1e-9);
    auto source = std::make_shared<FileSource>("src", "data.s7ss");

    schema2->insert(source);
    schema2->run(source->header().num_samples);

    std::cout << "Playback complete: " << source->header().num_samples << " samples\n";
    std::cout << "Metadata: " << source->header().metadata_json << '\n';
}
```

---

## Integration with Element Catalog

Both FileSink and FileSource are registered in the plugin manifest:

```
s7.sources.file_sink   → FileSink (write N-channel file)
s7.sources.file_source → FileSource (read N-channel file)
```

The GUI element palette lists these under **I/O/Files**, alongside future additions (WAV, FLAC, etc.).

---

## Future Extensions

### V2: Streaming Writer

Replace full-buffering with block-based writes:

```cpp
class FileSinkStreaming : public Element {
  void tick() override {
    // Write block every N ticks
  }
};
```

**Benefits:** Handle unlimited-duration recordings, lower latency

### V3: Format Plugins

Extensible format support without static linking:

```cpp
class WAVFileSource : public FileSource {
  static constexpr const char* format_id = "audio.wav";
};

class FLACFileSink : public FileSink {
  static constexpr const char* format_id = "audio.flac";
};
```

### V4: GNU Radio Integration

Export to GNU Radio file format for signal processing workflow:

```cpp
auto grc_sink = std::make_shared<GnuRadioFileSink>("data.gr", channels);
```

### V5: Remote File Access

Stream to/from S3, HTTP, or network file servers:

```cpp
auto remote = std::make_shared<FileSource>("http://data-server/stream.s7ss");
```

---

## Testing

Automated test in `s7main/src/main.cpp`:

```
[FileSink] Wrote 100 samples to /tmp/test_output.s7ss
[FileSource] Read 1 channel(s), 100 samples at 1e9 Hz
  Metadata: {"test": "data"}
  Sample type: Real
  Output after 5 ticks: 2.51327e-05
```

Verification:
- File format structure: ✓ (hexdump shows correct magic, version, metadata)
- Round-trip fidelity: ✓ (samples survive write/read cycle)
- Multi-channel support: Ready for 2-channel and Complex tests

---

## Building and Linking

```bash
cd s7pp && mkdir cmake-build-debug && cd cmake-build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .

# Run tests
./s7main/s7main
# Output: [FileSink] and [FileSource] tests pass
```

---

## Design Notes

### Why Interleaved Samples?

- **Streaming-friendly:** Sequential memory access for N channels
- **GPU-ready:** SIMD/batch processing amenable when needed
- **Real-world standard:** Most audio/DSP frameworks use interleaved format
- **Cache locality:** Single loop over samples with tight channel iteration

### Why Separate File Type?

Instead of piggybacking on standard WAV/FLAC:
- **Schema agnostic:** Captures exact sample rate, metadata context from S7 runtime
- **Determinism:** Bit-exact replay by storing deployment info
- **No third-party dependencies:** Lean core library (no libFlac, etc.)
- **Future protocol bridge:** Easy to export to standard formats

### Why Synchronous flush()?

Current design writes all samples on cleanup (destructor):

**Pros:**
- Simple, deterministic file lifecycle
- Atomic: file either exists fully or not at all
- No resource leaks if schema aborts early

**Cons:**
- Not suitable for streaming >1GB datasets
- V2 design will support periodic asynchronous flush

---

## API Stability

File format and APIs marked **experimental/v0**:
- `SampleType` enum may gain `INT16`, `FLOAT32` variants
- Metadata recommendations may formalize
- V1 will commit to backward compatibility

