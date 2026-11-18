# Cross-node scheduling spikes: real S7 transport and ACK progress

Recorded 2026-11-17. This continues the
[initial experiments](CROSS_NODE_CAUSALITY_DESIGN.md). The
[compact evidence](CROSS_NODE_CAUSALITY_PHASE2_SUMMARY.json) records producing
source/binary/result hashes. No distributed scheduler or X1 ADR is accepted.
One module-local transport correction is implemented and tested; the graph
compiler and runtime execution rules are unchanged.
The subsequent [derived-plan/performance round](CROSS_NODE_CAUSALITY_PHASE3.md)
measures planning, dispatch scaling and matched-engine three-host workloads.

## Scientific and project references

- **Initialized feedback versus communication:** Lee–Seshia, *Introduction to
  Embedded Systems*, §6.3, Example 6.9 (L1), and Lee/Neuendorffer/Zhou,
  *Dataflow*, §3.1.2 (L3). The experiments keep explicit initialized `Delay`;
  no stream supplies an extra model sample.
  [Textbook](https://ptolemy.berkeley.edu/books/leeseshia/releases/LeeSeshia_DigitalV2_2.pdf),
  [chapter](https://ptolemy.berkeley.edu/books/Systems/chapters/Dataflow.pdf).
- **Partitioning and progress:** Pino/Bhattacharyya/Lee, §5 (L7), and Stanford
  EE382C lecture 6, slides 1–14 (T2). Data-dependency order and finite-resource
  waits must be investigated separately.
  [Scheduling report](https://ptolemy.berkeley.edu/publications/papers/95/erl-95-36/erl-95-36.pdf),
  [Stanford teaching notes](https://web.stanford.edu/class/ee382c/lectures/06_deadlock.pdf).
- **Bounded channels:** Lee–Parks (L4) and Parks §4.2 (L5), with the Geilen–Basten
  correction/limitations recorded as L6. No general KPN scheduler is adopted.
  [L4](https://ptolemy.berkeley.edu/publications/papers/95/processNets/proceedings.pdf),
  [L5](https://ptolemy.berkeley.edu/publications/papers/95/parksThesis/thesis.pdf).
- **Logical order and distributed state:** Lamport (L9) and MIT 6.852 Classes
  11–12 (T1). No synchronized-clock or global per-step barrier is added;
  node/channel state and explicit sequence positions establish the evidence.
  [Lamport](https://lamport.org/pubs/time-clocks.pdf),
  [MIT teaching notes](https://ocw.mit.edu/courses/6-852j-distributed-algorithms-fall-2009/pages/lecture-notes/).
- **Implemented contract:** [S7 wire protocol, §12.1](../../doc/S7_Network_Wire_Protocol_v1.md),
  [network elements](../../doc/S7_Net_Element_Contract.md), and ADR-021/024.
  ACKs follow consumption; more frequent ACKs are permitted by the existing
  minimum-frequency rule. Local output rollback does not undo external effects.

Full bibliography, publication types and reading limits are in the
[literature record](CROSS_NODE_CAUSALITY_RESEARCH.md).

## ACK correction and controlled comparison

The new `s7transport_in_memory_credit_smoke` regression failed before the
correction: after a consumed packet, the sole producer credit remained held.
The in-memory endpoint previously called `ack_due(now + 50 ms)` to mimic an
immediate ACK. That stored a future `last_ack_`, suppressing subsequent burst
ACKs; no independent local timer released the credit while the graph waited.

`StreamConsumer::ack_now(now)` now explicitly acknowledges only fully consumed
packets at actual monotonic time. The in-memory endpoint uses it; the network
endpoint retains `ack_due` and its independent tick. The second receive attempt
after a starvation callback also notifies consumption. No wire field, peer
authorization, causal ordering or network ACK timer policy changes.

The regression checks empty/unconsumed/partially consumed packets, duplicate
ACK suppression, the existing 50 ms and 64-packet triggers, and a 128-value
burst through a one-packet local window. Seven focused transport/net regressions
passed; the expanded credit regression was rerun after adding the count trigger.

The original runtime spike was repeated after the correction:

| Cases | Outcome |
| --- | --- |
| Projected order: 26 configurations, including windows 1/8/64, nested memory and pacing controls | All complete 32 steps per node, matching the reference. |
| Current fixed compiler order: four controls | Still stop at step 0; ACK repair does not fix scheduling. |
| Graph-free immediate and paced credit probes | Every consumed value returns the log to zero; all three values publish successfully. |

The separate `s7transport_ack_policy_spike` holds the C++ producer/consumer
implementation fixed and varies only ACK policy and controlled opportunities:
two policies × windows 1/8 × packet limits 1/4 × consumer periods 1/7 × I/O
ticks 1/5 ms = **32 virtual-time cases**, **4,064 values checked**. A 127-value
input forces a final partial four-value batch. All values arrive exactly and
all credits drain within bounds under both policies.

In the one-packet, one-value, 1 ms tick/consumer case, immediate consumption
ACKs finish at virtual 126 ms, while the existing timed policy finishes at
6,350 ms. This is a deterministic protocol comparison, not a measured host or
network speedup. It demonstrates that independent ticks preserve timed-ACK
progress and that small windows can make the ACK interval dominate progress.
It does not establish the best network policy: control traffic, fan-out cost
and high-rate workloads have not been compared.

## Three-host S7 WebSocket/mTLS experiment

Hosts: development machine (Fedora 45 x86-64), `win` (Fedora 45 x86-64), and
`mail` (Linux aarch64). SSH forwards bind only loopback. Consumer connections
are A→C, B→A and C→B; C→B passes through the development host's relay. S7 mTLS
remains end to end inside those forwards. Timing includes the relay/SSH route.

The data path now uses actual S7 binary wire messages, bounded send logs,
RESUME, consumption ACKs and existing WebSocket/mTLS endpoints:

- Fixed/projected cases use C++ `NodeRuntime`, `NetSource`, `NetSink` and
  `BeastRuntimeNetEndpoint` on A. Only the fixture's compiled dispatch records
  change for projected order; the compiler is not modified.
- B and C use unchanged `s7py` `StreamTable`/`DataEndpoint` sources with a
  hand-scheduled scalar fixture. Readiness cases use this fixture on A too;
  they do not implement suspension in C++ `NodeRuntime` or deploy a Python graph.
- All hosts load matching hashed Python sources. Each role gets its own fresh
  EC certificate/key, with S7 UUID SAN, endpoint SAN and client/server TLS usages.
  The temporary CA key is never written or sent remotely. Remote leaf keys are
  removed afterward. No repository fixture credentials are used.
- The `win` Python dependency was installed in a dedicated `/tmp` virtual
  environment; no installed S7 services or firewall settings were changed.

The normal matrix uses two topologies (explicit-delay feedback and independent
constant streams), three schedules, windows 1/8, packet limits 1/4, and a C
delay of 0/5 ms. Each run targets 16 values per node. Two recovery cases close
B's actual outbound data connection after four accepted samples; one negative
case makes A expect the wrong node UUID on its connection to C.

| Cases | Configurations / node runs | Observed result |
| --- | --- | --- |
| Fixed order | 16 / 48 | No node completes its first step; initial receive wait is reproduced. |
| Projected order | 16 / 48 | All nodes complete with exact references, no stream faults or transport errors. |
| Readiness order | 16 / 48 | Same value/progress result, using Python fixture scheduling. |
| Disconnect/reconnect | 2 / 6 | One actual closure and a new outbound connection confirmed in each case; all 16 values match and replay counters advance. |
| Wrong certificate-bound identity | 1 / 3 | A records `certificate identity mismatch` and accepts no incoming value; all workers end with explicit underrun. |
| **Total** | **51 / 153** | Expected successful/negative outcomes, matching source hashes and bounded observed send-log occupancy. |

Successful cases have 102 completed node runs and 1,632 checked values, with
zero unauthorized/malformed counters. This exercises timer-driven
ACKs on real S7 transports; the small-window failure was specific to the old
in-memory ACK path, not evidence against the network timer policy itself.

The identity negative control also exposes a design limit: A completes zero
steps while B and C each complete one before their next read faults. A's
already-sent value is not undone by local step rollback. A future deployment
design must state accepted-prefix/failure behavior rather than promise global
step atomicity. This is an observed S7-specific result, not a claim from the
textbooks.

## Reproduction and remaining decisions

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target s7transport_ack_policy_spike \
  s7tests_cross_node_scheduling_spike s7tests_cross_node_s7_node \
  s7transport_in_memory_credit_smoke --parallel 2
build-release/s7transport/s7transport_ack_policy_spike > /tmp/s7-ack-policy.json
build-release/s7tests/s7tests_cross_node_scheduling_spike > /tmp/s7-runtime-ack.json

# Each Python executable needs cryptography; use the intended remote venv.
python3 -B tools/run_cross_node_s7_spikes.py \
  --host-b win.local --key-alias-b win --python-b /path/to/win/venv/bin/python \
  --host-c mail.local --key-alias-c mail \
  --cpp-node-a build-release/s7tests/s7tests_cross_node_s7_node \
  --output /tmp/s7-three-host-mtls.json
```

Use `--smoke` for two eight-value feedback checks before the 51-case matrix.
Research executables remain opt-in; only the credit regression joins CTest.
Raw results and exact commands are referenced in the compact evidence.

The next prototype should derive a global same-step precedence order and
validate its projection, instead of relying on these hand-derived fixtures.
Compare admission, schedule distribution, revision changes and failure prefixes
before selecting a production approach. Projected ordering is promising for
the existing single-rate serial owner; readiness requires additional ownership
and suspension contracts. This experiment does not rank their CPU performance.

Whole-diagram preflight rejection, Studio deployment errors, arbitrary actor
graphs, multi-rate feedback, long soaks, process restart, durable replay,
remote control authorization and physical devices remain outside this result.
X1 decision and X2/X3 production acceptance stay open.
