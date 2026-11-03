# Transport lab

Runs one stream between two S7 implementations, C++ (`s7runtime`), Python (`s7py`) or Java
(`s7java`), while its data connection is reset at a fixed period, and checks every value the
consumer receives. It reproduces the multi-node results reported for the s7py and s7java data planes.
It is a manual tool, not part of the test suite: a run takes about 30 s and needs a C++ build.

## Files

| File | What it does |
|---|---|
| `run_pair.py` | Starts three nodes over mTLS on localhost, deploys the lab diagram, runs it, and prints the stream status of every end and the value check as JSON. |
| `chaos_proxy.py` | A TCP proxy on the A → B data path that resets every open connection each `period` seconds (linger 0, so the close is an RST and the data in flight is lost). It forwards bytes unchanged, so TLS stays end to end. SIGUSR1 stops the drops. |
| `deploy_nodes.py` | PREPARE / COMMIT to the nodes that have a control plane, as the Studio certificate, with one activation time. `s7ctl deploy` cannot be used when a Java node takes part, because the Java node has no control plane yet. |

## The diagram and the check

```
A (producer):  one = 1  ->  count = Accumulator(one)              1, 2, 3, ...
                  stream A -> B, through chaos_proxy.py
B (consumer):  out = count;  bad = (out - Delay(out) != 1);  errors = Accumulator(bad)
                  stream B -> C, no drops
C (observer, s7py):  echo = errors
```

A value lost on A → B makes one step's difference 2, a duplicate makes it 0, and a reordering
makes it something else, so `errors` counts every loss, duplicate or reordering that reached the
consumer's graph. `errors_at_observer = 0` means that every value from 1 to the last one arrived
exactly once and in order. The counters in the stream status (`replayed`,
`duplicates_discarded`, faults) show what the protocol did to get there.

The diagram is a chain on purpose. With a stream back from B to A, each node's step would wait
for the other's (`s7l2/docs/OPEN_PROBLEMS.md`, X1–X3).

## Running

From this directory, with Python that has the `cryptography` package (s7py's TLS needs it) and
`openssl` on the path:

```bash
python3 run_pair.py <cpp|py|java> <cpp|py|java> --build ../../../s7l2/build \
    [--seconds 20] [--dt 0.001] [--drop 2] [--java java] [--java-tls-resume] [--out result.json]
```

- `--build`: the C++ build directory with `s7runtime` and `s7ctl` (default `s7l2/build`, or `$S7_BUILD`).
- `--drop`: reset period in seconds; `0` runs without drops.
- Java nodes need `s7java/build.sh` to have been run, and `java` 21 or later (`--java`).
  `--java-tls-resume` makes them resume the TLS session when they reconnect.

Nodes A and B use the test node certificates in `s7l2/s7runtime/examples/certs`. The observer's
certificate is issued for each run by the same test CA, in a temporary directory; no key is added
to the repository. These keys are public, so every listener binds to `127.0.0.1` only. Each run
works in a new `/tmp/s7-lab-*` directory (the node logs are kept there) and picks random ports from
21000–24003.

## Reading the result

- `summary.delivered`: elements the consumer received. `produced - delivered` is what was still
  in flight when the status was read (the consumer is read first). It can be negative with a
  Java producer, which writes its status file every 0.5 s.
- `summary.replayed_packets`: packets the producer sent again after a RESUME.
- `summary.latency_*`: from `SEND_TIME` (packet sealed) to arrival, upper bounds of log2
  histogram buckets. p99 near 1 s is the reconnect delay after a drop.
- `value_check.errors_at_observer`: the check above, over the values 1 to `observer_step`. The
  observer prints its watch once a second, so the lab waits up to 10 s for one that covers every
  value the consumer had received; `covers_delivered` says whether it did. `consumer_errors` is
  the same value watched on the consumer, where the implementation can watch (s7py, s7java).

## Results

One run per pair on 2026-11-03, on one Linux host: `--seconds 20 --dt 0.001 --drop 2` (1000
values/s, 10 dropped connections, then 3 s without drops). C++ and s7py were from `5d1ba32`, s7java
from its data-plane change (#15).

| Producer → consumer | delivered | replayed packets | duplicates discarded | faults | errors at observer | latency p50 / p99 |
|---|---|---|---|---|---|---|
| C++ → C++ | 22083 | 914 | 0 | none | 0 | ≤ 1.0 / ≤ 1007 ms |
| C++ → Python | 22091 | 914 | 0 | none | 0 | ≤ 1.0 / ≤ 1013 ms |
| C++ → Java | 21584 | 931 | 0 | none | 0 | ≤ 2.0 / ≤ 1049 ms |
| Python → C++ | 22056 | 929 | 0 | none | 0 | ≤ 1.0 / ≤ 1008 ms |
| Python → Python | 22055 | 924 | 0 | none | 0 | ≤ 1.0 / ≤ 1012 ms |
| Python → Java | 21544 | 929 | 0 | none | 0 | ≤ 1.0 / ≤ 1049 ms |
| Java → C++ | 22082 | 954 | 0 | none | 0 | ≤ 1.0 / ≤ 1037 ms |
| Java → Python | 22078 | 948 | 0 | none | 0 | ≤ 1.0 / ≤ 1049 ms |
| Java → Java | 21659 | 940 | 0 | none | 0 | ≤ 1.0 / ≤ 1049 ms |

In every run the observer's check covered all the values the consumer had received
(`covers_delivered`), and the B → C stream had no faults.
