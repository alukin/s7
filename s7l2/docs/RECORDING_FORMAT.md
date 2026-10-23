# S7 recording formats

`RecordingSink` and `RecordedSource` in `s7io` share the formats below.
Both are registered in Studio, `s7runtime` and `s7ctl`. Compact binary is the
default for both elements; Gnuplot text is available for inspection and plotting.
Every file starts with a mandatory, versioned header. There is no headerless mode.

A recording is one uniformly sampled signal, starting at model sample zero.
The header stores the signal type, exact positive sample rate as an unsigned
64-bit numerator/denominator pair (samples per second), and a short UTF-8
description. The description field is always present, may be empty, and is
limited to **1024 encoded bytes**. Use it for signal meaning, units and experiment
context, for example `Output voltage [V]; 12 V PID; changing RLC load`.
It is user-supplied text, not interpreted configuration.

The model time of sample n is `n * rate_denominator / rate_numerator` seconds.
There is no wall-clock timestamp or implicit resampling. Supported signal types
are F64 (IEEE-754 binary64) and C64 (two binary64 components).

## Compact binary, version 3

All integers and floating-point bit patterns use **little-endian** byte order,
regardless of host architecture. There is no native-struct padding. The first
40 bytes are fixed, followed by the description and then packed samples.

| Byte offset | Bytes | Field | Required value / meaning |
|---|---:|---|---|
| 0 | 4 | Magic | ASCII `S7B1` (`53 37 42 31` hex) |
| 4 | 1 | Format version | `3` |
| 5 | 1 | Flags | `0`; nonzero values are rejected |
| 6 | 2 | Signal type ID | F64: `0x000F`; C64: `0x0103` |
| 8 | 8 | Total header bytes | `40 + description_bytes` |
| 16 | 8 | Rate numerator | Positive uint64 |
| 24 | 8 | Rate denominator | Positive uint64 |
| 32 | 8 | Description bytes | uint64, 0–1024 |
| 40 | Description bytes | Description | UTF-8, no terminator or padding |
| Total header bytes | Remaining bytes | Samples | Packed layout below |

F64 samples occupy **8 bytes each**, containing one little-endian binary64
value. C64 samples occupy **16 bytes each**, containing the real component
first, then the imaginary component. There are no per-sample indices,
separators, block headers, compression or checksums. The sample index is its
zero-based position in the payload.

There is no sample-count field because writers stream samples without knowing
the final count. For a completed file:

```
sample_count = (file_bytes - total_header_bytes) / sample_bytes
```

A nonintegral result indicates a truncated sample and is rejected. Replay also
rejects empty payloads, nonfinite values, invalid UTF-8, unsupported versions,
flags/types, zero rates, inconsistent lengths and truncated headers.
Previous binary versions are not accepted; this project does not provide a
compatibility reader.

An independent byte fixture, used in the regression test, is a 49-byte F64 file
with rate 8/1, description `V` and one sample with value 1.25:

```text
53 37 42 31 03 00 0f 00  # magic, version, flags, F64
29 00 00 00 00 00 00 00  # 41 header bytes
08 00 00 00 00 00 00 00  # 8 samples/second numerator
01 00 00 00 00 00 00 00  # denominator 1
01 00 00 00 00 00 00 00  # 1 description byte
56                       # UTF-8 "V"
00 00 00 00 00 00 f4 3f  # binary64 1.25
```

Comments and whitespace in this illustration are not file bytes.

## Gnuplot text, version 1

Readers accept LF and Windows CRLF line endings in headers and sample rows.
JSON escapes inside descriptions retain their content; newline normalization
applies only to the physical line delimiter.

The file starts with these **seven header lines, in this order**:

```text
# s7 gnuplot sample file
# format=gnuplot-text
# version=1
# signal=F64
# rate=1000/1
# description="Output voltage [V]; PID regulator"
# columns=sample_index value
0 1.25
1 -3.5
```

For C64, the signal line is `# signal=C64`, the columns line is
`# columns=sample_index real imag`, and a data row might be `0 1.25 -0.5`.

The description is a **JSON string**, including its quotation marks. Quotes,
backslashes and control characters use JSON escaping, so a description cannot
inject extra header lines. Other UTF-8 characters are preserved. Both LF and
CRLF line endings are accepted. Writers use the classic locale, decimal `.` and
17 significant digits to preserve binary64 values on round trip.

Data rows contain an unsigned sample index followed by the real value, or real
and imaginary components. Replay requires contiguous indices starting at zero.
Blank lines and comment lines in the data section are ignored. Invalid headers,
extra data columns, missing values and malformed data are errors. Older text
files without the complete header must be regenerated.

## Elements and lifecycle

`RecordingSink` parameters:

| Parameter | Default | Meaning |
|---|---|---|
| `file_path` | Required | File on the executing node |
| `format` | `compact-binary` | `compact-binary` or `gnuplot-text` |
| `sample_rate_hz` | 1000 | Model sample rate, stored as S7's resolved rate fraction |
| `description` | Empty | Short UTF-8 description, at most 1024 bytes |
| `type` | Inferred, otherwise F64 | F64 or C64 |

`RecordedSource` uses the same `file_path`, `format` and `type` parameters.
Its `sample_rate_hz` defaults to **0**, which uses the file's rate. A positive
value checks that the configured rate equals the header's rate; a mismatch is
an error. `end_behavior` is `error` (default), `hold` or `repeat`.

Example sink element:

```json
{"id": "voltage_record", "type": "RecordingSink", "params": {
  "file_path": "/tmp/voltage.s7bin",
  "sample_rate_hz": 100000,
  "description": "Output voltage [V]; 12 V PID; changing RLC load"
}}
```

Connect the voltage output to sink input 0. To replay the completed recording:

```json
{"id": "voltage_replay", "type": "RecordedSource", "params": {
  "file_path": "/tmp/voltage.s7bin"
}}
```

The sink declares its configured rate. Connected elements must agree with it.
Validation and compilation **do not create or truncate the file**. The first
execution step opens/truncates it, writes the header and first sample. Subsequent
samples are buffered; every 1024 samples the sink flushes and checks stream
errors. C++ callers can also call `flush()` explicitly. Destruction closes the
stream and flushes its buffer. A flush is not an `fsync` durability guarantee.
Finish recording before loading it for replay.

Replay preloads and validates the entire file during construction. It then
emits one stored sample per model step without file I/O. Its memory consumption
scales with sample count. The recording remains a file on the executing node;
control deployment does not transfer it automatically.

Low-level `CompactBinaryFileSink<T>` and `GnuplotFileSink<T>` use the same
formats; their optional C++ `Rate` argument defaults to 1000/1. For custom
descriptions use `RecordingSink<T>`. The C++ real/complex aliases are
`s7::io::RealRecordingSink`, `ComplexRecordingSink`, `RealRecordedSource` and
`ComplexRecordedSource`.

## Verification

```bash
cmake --build build --target s7io_smoke s7io_replay_smoke s7runtime_control_smoke
ctest --test-dir build -R '^(s7io_smoke|s7io_replay_smoke|s7runtime_control_smoke)$' --output-on-failure
```

Tests cover both signal types and formats, rate/description round trips,
independent binary header/payload bytes, every truncation point of the fixture,
corrupted header fields, malformed text, EOF modes, sink factory/type inference,
validation preserving existing files, and runtime recording/replay.
