# s7java — minimal S7 data node in Java

A second independent implementation of the S7 data stream protocol
(`../doc/S7_Network_Wire_Protocol_v1.md`, `../doc/S7_Net_Element_Contract.md`), to check that the
protocol is implementable from its specification in another language and that nodes in different
languages exchange streams. Standard library only, Java 21 or later.

## What it does

- `s7.wire`: the wire format v1 (every type of section 8, 12-bit packing, `TIMESTAMPS` and
  `SEND_TIME` extensions, `ACK` / `RESUME` / `RESUME_NO_STATE`), with the checks of
  `s7transport`'s `wire_format.cpp` in the same order.
- `s7.stream`: stream descriptors as `s7net` reads them, and lossless delivery as
  `stream_protocol.cpp`: batching, the send log with backpressure, cumulative acknowledgements,
  resume and replay, per-consumer positions, gaps as faults, recovery, derived and explicit
  timestamps, latency statistics, and sequence-range reservations in the C++ node's file format
  (`<state-dir>/streams/<stream_id>.seq`, fsync'ed with their directory). The state directory
  (default `~/.s7java/<node_uuid>`) has one owner: `DataNode` holds `<state-dir>/runtime.lock`
  while it runs and refuses to start if another Java node process holds it. That is a POSIX record
  lock, which on Linux does not exclude the `flock` that C++ and s7py nodes take on the same file.
- `s7.net`: the stream table with the blocking graph-side API, and the `/data` connections of
  `transport.cpp` over mTLS (`javax.net.ssl`): a consumer node connects to each producer node,
  announces itself in the `S7-Node` handshake header and RESUMEs its streams after every
  (re)connect; a stream is accepted only from its producer node (identified by the
  `spiffe://s7/node/<uuid>` SAN of its certificate) and served only to its consumer nodes. Sockets
  use `TCP_NODELAY`.
- `s7.node`: `DataNode` runs its part of a deployed diagram, which it derives as `s7net` does
  (`NodePart`: its elements, a `NetSink` for each stream it produces, a `NetSource` for each it
  consumes), on the UTC grid of the diagram's `dt`. `Graph` steps with the S7 causal semantics for
  F64 signals and the elements `Constant`, `Gain`, `Adder`, `Accumulator`, `Delay` /
  `PreviousValue`, `Comparator`, `NetSink` and `NetSource`.
- `s7.json`: a small JSON reader and writer that keeps integers and reals apart, as Boost.JSON does.

## Frame transport service

The opt-in `s7.frames` package now matches C++'s frame transport: raw numeric
frames, optional independent LZ4/Zstandard codecs, bounded immutable leases,
consumption ACKs/replay and ordered loss faults. All 25 whole-byte numeric types
preserve their bits. It is separate from `DataNode`'s deployed F64 graph path;
frame capabilities are not advertised yet. Java 21 compilation remains
standard-library-only; optional codec jars are supplied at runtime through
`S7_FRAME_CODEC_CLASSPATH` for the tests. See the
[cross-language guide](../doc/S7_Frame_Interoperability.md) for APIs, dependencies
and shared conformance commands.

## Not implemented

The control protocol: `DataNode` takes its part from a diagram file (for example the one
`s7ctl deploy --write` or `s7ctl check --write` writes) instead of `PREPARE_DEPLOYMENT` /
`COMMIT_DEPLOYMENT`, and writes its streams' status, in the `GET_STREAM_STATUS` form, to a file.
Deploy the other nodes of the diagram as usual; the Java node exchanges streams with them (on
other hosts, see `--bind` below).

## Build, test, run

```bash
./build.sh --test   # wire fixtures, delivery rules, node and JSON checks, and /data over mTLS
                    # on loopback with the test certificates in s7l2/s7runtime/examples/certs
java -cp build s7.node.DataNode --diagram deployed.s7diagram \
  --cert node.cert.pem --key node.key.pem --ca root_ca.cert.pem --port 9102 \
  [--bind ADDRESS] [--state-dir DIR] [--watch ELEMENT]... [--status-file FILE] [--activate-at-ns UTC_NS]
```

Producers stamp `SEND_TIME`, from which consumers measure latency, only when started with
`-Ds7.clock.synchronized=true`: the standard library cannot tell whether the host clock is
synchronized (the C++ and Python nodes check it, or take `clock.assume_synchronized`), and an
unsynchronized clock would add its offset to every latency.

The `/data` listener binds to `--bind`, `127.0.0.1` by default. When consumer nodes run on other
hosts, bind to an address they can reach (for example `0.0.0.0`) and give that host in the
diagram's `data_endpoint` for this node. Every peer must present a node certificate from `--ca`.

Set `JAVAC` / `JAVA` when the tools on `PATH` are older than Java 21, for example
`JAVAC=/usr/lib/jvm/java-25-openjdk-amd64/bin/javac ./build.sh --test`.
