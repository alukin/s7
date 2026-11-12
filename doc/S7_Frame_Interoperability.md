# Sample-frame transport across C++, Python and Java

Implemented and checked 2026-11-12. All three languages now provide the opt-in
frame service described by [ADR-025 / the frame contract](S7_Sample_Stream_Profile.md)
and the [encoding contract](../s7l2/docs/FRAME_TRANSPORT.md#initial-encoding-contract).
This is separate from each node's existing scalar runtime transport. No node
advertises these profiles or deploys frame boundaries automatically yet.

## Common behavior

- `sample_frames_v1`: critical SAMPLE_FRAMES `0x8002`, whole interleaved frames,
  explicit segment/position/revisions, sparse clock anchors and ordered faults.
- `sample_frames_encoded_v1`: additional FRAME_ENCODING `0x8003`, independent
  LZ4 blocks or Zstandard frames, explicit raw fallback and decoded CRC-32.
  Python LZ4 disables its default size prefix (`store_size=False`).
- Native numeric **bits** are preserved, including CI12 and floating-point NaN
  payloads/signed zero. All 25 whole-byte wire types are supported. Scalar
  I12/U12 remain outside this profile. No conversion, resampling or concealment.
- Receipt and consumption have separate positions. Only consumption advances
  ACKs; RESUME preserves the consumption ledger. Disconnected peers retain replay
  positions; slow consumers prevent trimming. Sealed message bytes are replayed.
- Whole-frame partial publication/consumption, rebased events, immutable retained
  leases, packet/frame/pool bounds, a reserved terminal slot, source/sink faults,
  known versus unknown loss, authorized recovery boundaries and 16 retained faults.
- Malformed profiles, unnegotiated/unavailable codecs, decoded-size expansion,
  checksums, truncated data, unknown critical fields and invalid transitions are
  rejected. Validation precedes decoded pool admission. Legacy codecs reject
  the new critical extensions rather than interpreting frames as scalar samples.

The socket owner authenticates the producer/consumer, agrees on descriptors and
aliases, bounds connection queues/messages, carries ACK/RESUME and authorizes
recovery. These local service APIs do not replace transport authentication.

## APIs and resource ownership

| Language | Entry point | Payload API |
| --- | --- | --- |
| C++ | `s7/frame_transport.hpp`, namespace `s7::frames` | `publish_copy` / `receive_copy` use native typed storage; `publish_shared` / leases use portable numeric bytes |
| Python | `s7node.frames` | `publish_copy` / `receive_copy` use portable numeric bytes; `publish_elements` accepts the existing wire component-bit tuples |
| Java | `s7.frames.FrameFormat`, `FrameBuffers`, `FrameSink`, `FrameSource` | `publishCopy` / `receiveCopy` use portable numeric bytes; `publishElements` accepts existing wire component-bit arrays |

Python/Java byte APIs deliberately avoid converting floating values through the
host number type. Java unsigned 64-bit fields use `long` bit patterns and unsigned
comparisons/formatting; nullable `Long` means an unknown count. Python uses bounded
integers with `None` for unknown. The fixture JSON uses decimal **strings** for
unsigned 64-bit values; this is test interchange, not a new control protocol.

Use `front_packet` / `frontPacket` to inspect frame metadata before processing,
then explicitly consume. Python leases support context managers and `close`;
Java leases/packets implement `AutoCloseable`. Each retained/sliced handle owns
a separate reference. Closing one handle cannot invalidate another. Managed
`bytes()` exports an independent caller-owned copy, so exported data cannot refer
to a slot that has been recycled. Close services before pools; stop/join external
workers before destroying their owners.

Per-stream queues and preallocated pool slots remain bounded. Managed runtimes
allocate objects/copies during operation; their buffer budget counts pool capacity,
not Python/JVM heap overhead, temporary copies or native codec workspaces. They
do **not** claim C++'s preallocated codec workspace behavior or a process RSS bound.
Closed managed services release their owned leases; already exported handles stay
valid. Closed pools release their reservation when the last live lease closes;
GC cleanup is a fallback, not the recommended lifetime mechanism.

## Optional compression providers

Raw framing and existing nodes still work without optional codec dependencies.
An unavailable codec is rejected explicitly, even if a particular encoded packet
would have used raw fallback. Existing node paths do not select these new APIs
automatically.

Python (3.10+), from `s7py`:

```sh
python3 -m venv --system-site-packages /tmp/s7-frames-venv
/tmp/s7-frames-venv/bin/python -m pip install -r requirements-frames.txt
/tmp/s7-frames-venv/bin/python -m unittest discover -s tests
```

The optional pins tested here are `lz4==4.4.5` and `zstandard==0.25.0`.
Native source builds need Python development headers when no matching wheel exists.

Java stays standard-library-only at compilation (`--release 21 -Xlint:all -Werror`).
Optional compression is loaded reflectively from the application classpath:

| Provider tested | Maven coordinate |
| --- | --- |
| LZ4 | `at.yawk.lz4:lz4-java:1.14.0` |
| Zstandard | `com.github.luben:zstd-jni:1.5.7-26` |

The original LZ4 coordinate now has an upstream
[relocation POM](https://repo.maven.apache.org/maven2/org/lz4/lz4-java/1.8.1/lz4-java-1.8.1.pom)
to `at.yawk.lz4`. Put the jars in a local directory, then from `s7java`:

```sh
S7_FRAME_CODEC_CLASSPATH='/tmp/s7-frame-java-codecs/*' ./build.sh --test
java --enable-native-access=ALL-UNNAMED -cp 'build:/tmp/s7-frame-java-codecs/*' s7.frames.FrameCheck
```

No dependency is downloaded automatically by the build or transport. Dependencies
used for validation live outside the repository. Java's native-access switch is
for the optional JNI provider, not a requirement for raw framing.

## Shared conformance and cross-language checks

[`frames_v1.json`](../s7l2/s7transport/tests/fixtures/frames_v1.json) contains
46 valid and 42 invalid cases: every whole-byte type, compressed/fallback units,
faults, timing anchors, optional headers, unsigned boundaries, malformed lengths,
reserved fields, overflow, corruption and trailers. The independent generator
uses explicit struct packing, not any service codec under test. Compression
regeneration needs the optional Python providers; compressed byte choices are
not required to match across implementations. Raw/fault canonical bytes do match.

From `s7l2`, after building C++'s fixture bridge and Java:

```sh
cmake --build /tmp/s7-sdr-audit --target s7transport_frame_interop --parallel 4
python3 tools/check_frame_fixtures.py --require-codecs \
  --cpp /tmp/s7-sdr-audit/s7transport/s7transport_frame_interop \
  --python /tmp/s7-frames-venv/bin/python \
  --java-classpath '../s7java/build:/tmp/s7-frame-java-codecs/*'
```

This runs every shared case in every implementation, checks canonical raw/fault
bytes, then sends every producer's encoded valid cases to all three decoders:
all nine directed pairs. Test bridges exchange JSON lines over subprocess pipes;
these checks qualify wire compatibility, not cross-host speed or networking QoS.
Service-level tests separately cover replay/consumption, partial blocks, retained
leases, cancellation, concurrency and recovery. Existing scalar tests include
real loopback TLS; C++ also has real socket tests for the new frame service.

Retained test results and dependency hashes:
[2026-11-12 conformance report](../s7l2/docs/performance/transport-2026-11-12/FRAME_CONFORMANCE.md).
Runtime deployment/admission integration, graph commit hooks and OS devices remain
follow-up work; codec conformance alone does not authorize capability advertisement.
