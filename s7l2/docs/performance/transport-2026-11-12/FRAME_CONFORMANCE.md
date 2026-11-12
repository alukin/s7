# C++ / Python / Java frame conformance — 2026-11-12

Python and Java now implement the same opt-in raw/encoded frame services as C++.
The shared wire corpus passes all three codecs, including independent LZ4 blocks,
Zstandard frames, raw fallback, CRC, clock anchors, source faults and unsigned
64-bit boundary values. Existing scalar runtime behavior is unchanged.

| Validation | Result |
| --- | --- |
| Shared valid/invalid fixtures | 46 valid + 42 invalid cases, checked in all three languages |
| Producer/consumer combinations | All nine directed pairs, 46 valid frames each |
| Combined fixture, canonical raw/fault encoding and pair checks | **789 passed**, no unavailable-codec cases skipped |
| Python complete suite, optional codecs installed | **80 tests passed** |
| Python complete suite, optional codecs absent | **80 tests passed**, compressed cases conditional on availability |
| Java frame checks, optional codec jars present | **591 passed**, no unavailable-codec fixtures skipped |
| Java frame checks, no codec jars | **511 passed**, 22 compressed-profile fixtures explicitly skipped; unavailable codecs rejected |
| Java legacy tests | 78 wire fixtures, 59 stream, 30 node, 14 net and 8 transport checks passed |
| Java compilation | `--release 21 -Xlint:all -Werror` passed without codec jars on the compiler classpath |
| C++ transport/net CTest | **9/9 passed**, including the new shared fixture bridge |

Wire interoperability uses subprocess pipes, not a new network endpoint. Each
language emits its own raw/LZ4/Zstandard messages and the other decoders check
the numeric bytes, frame positions, revisions and events. Raw/fault encoding is
also compared byte-for-byte with independently generated fixtures; compressed
bitstreams may differ by encoder while preserving the same decoded contents.

Service tests cover partial publication/read boundaries, event rebasing, ACKs
after consumption, reconnect without premature ACK, fan-out with a slow peer,
retained/shared leases, full-window terminal fault reservation, known/unknown loss,
authorized recovery with retained history, underflow, concurrent transfer and
blocked pool cancellation/close. Legacy Python/Java suites include loopback TLS;
the C++ suite includes real WebSocket/mutual-TLS frame tests.

Validation host: Linux x86-64, GCC 16, CPython 3.15.2 and OpenJDK/GraalVM 25;
Java source compatibility was compiled for release 21. Optional Python packages:
lz4 4.4.5 and zstandard 0.25.0. Optional Java artifacts: maintained
`at.yawk.lz4:lz4-java:1.14.0` and `com.github.luben:zstd-jni:1.5.7-26`.
Their URLs and SHA-256 hashes are retained in
[frame-conformance.json](frame-conformance.json), along with source hashes,
exact commands and validation logs. Provider binaries are outside the repository.

[The interoperability guide](../../../../doc/S7_Frame_Interoperability.md) gives
the APIs and reproduction commands. Raw modes remain usable without optional
providers. Managed runtimes enforce packet/frame/pool capacity but do not claim
C++'s allocation behavior or include GC/JNI overhead in their buffer counters.

This qualifies the initial service and wire subset, not runtime deployment,
capability negotiation, managed heap high-water marks, cross-host speed, hardware
clocks or OS audio. Those integration/qualification stages remain open.

## Verification after restart

A fresh headless Release build in `/tmp/s7-transport-resume` passed the same
9/9 C++ transport/net tests. Java compiled into `/tmp/s7-java-resume` with
`--release 21 -Xlint:all -Werror` and repeated all frame and legacy checks above.
Restoring the pinned providers reproduced all 789 cross-language checks without
skips. Python repeated all 80 tests both with and without compression providers;
the codec-enabled environment includes system packages for the TLS conformance
test. The downloaded Java artifacts match the recorded SHA-256 hashes.

The restart checks exposed and fixed an optional-dependency bug in
`tools/check_frame_fixtures.py`: without `--require-codecs`, unavailable encoders
and decoders now skip their cases during pair checks, as fixture-only checks
already did. Each pair reports only actually verified frames. With Python codecs
absent, 703 checks pass and 58 are skipped; with both Python and Java providers
absent, 641 pass and 92 are skipped. Strict mode still fails on an unavailable
codec. Missing bridge arguments now produce an argparse error. No transport,
runtime or graph behavior changed in this follow-up.
