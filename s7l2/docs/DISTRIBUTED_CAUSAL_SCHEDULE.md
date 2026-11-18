# Static causal schedules for deployed diagrams

Implemented after the maintainer approved the strongest candidate on
2026-11-17. [ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027) records the
decision; [Phase 4](CROSS_NODE_CAUSALITY_PHASE4.md) and the
[bibliography](CROSS_NODE_CAUSALITY_RESEARCH.md) provide the research evidence.
Real-loop/hardware timing qualification is still pending.

Studio **Deploy to Nodes** and `s7ctl deploy` now derive an outgoing-first
static schedule for the actual diagram. `s7ctl check` performs the same
planning offline and `--write` saves the streams and schedule for inspection.
An invalid cycle fails before any control request, including PREPARE.
Elements and streams are named in the error. No stream supplies a guessed
initial value: feedback needs explicit causal memory, including inside nested
schemas. An unrelated memory element or stateful direct-feedthrough block
does not validate a direct cycle elsewhere in the graph.

## Execution and format

Planning uses registered factories, existing type validation and ADR-024
flattening. Each node fragment contributes its direct-feedthrough elements,
including generated stream ends. Memory outputs are already available at
step entry. Sends precede receives through explicit dependency edges; eligible
sends have priority over other ready work. Node UUID and flattened element ID
break ties. The global order is projected onto each node, excluding virtual
schema ports and causal-memory elements.

The committed diagram contains:

```json
"causal_schedule": {
  "version": 1,
  "nodes": {
    "<node_uuid>": ["<flattened_element_id>", "<generated_stream_end_id>"]
  }
}
```

The extracted runtime schema carries that node's array as `causal_order`.
Every node checks the combined serial-order/stream wait graph for cycles.
Its compiler independently checks complete membership, uniqueness and real
local dependencies after resolving nested aliases. A stale or malformed order
fails preparation. The compiler fills its ordinary dispatch vectors from the
validated order; there is no scheduling work added to `NodeRuntime::step()`.
Memory staging, rollback, rate adoption, model time, activation pacing and
transport ACK policy retain their existing contracts. Early network effects
are not undone by local rollback.

The root `causal_order` schema field is also supported by the schema-library
and runtime JSON loaders. It contains real flattened IDs, excludes memory and
virtual ports, and must list every causal element exactly once. A standalone
`Schema::step()` adds its private exposed-input proxies before the requested
order. A parent compilation flattens nested schemas and uses the parent's
order, rather than treating each nested schema as an independently scheduled
runtime.

## Initial boundaries

- Participating nodes must advertise `NODE_INFO.causal_schedule_versions`
  containing `1`. Older C++ nodes and implementations without this capability
  are refused before preparation. Rebuild/update **all participating nodes**
  and the deployment client for testing.
- Initial distributed planning admits one rate domain and no rate-changing
  elements or coupled numerical regions. Unsupported diagrams receive an
  explicit error. Local graphs keep their existing scheduling/numerical path.
- With a diagram `dt`, preflight also checks rates. Without a model rate,
  structural preflight validates factories, wiring, types and cycles without
  manufacturing timing. Final node compilation remains strict about rates;
  a structurally schedulable graph can still fail preparation for missing
  timing or resources.
- Hand-written external network elements without diagram cross-node connections
  retain their existing compilation path; the planner does not infer hidden
  dependencies by matching their stream IDs. Generated diagram stream ends
  require the versioned schedule.
- This does not implement global step atomicity, durable replay, distributed
  numerical solves or a general readiness scheduler. Arbitrary fan-out,
  process restart and deployment revision transitions need broader real-loop
  qualification. Existing control authorization still requires the verified
  certificate, control key purpose and pinned fingerprint.

## Tomorrow's loop checks

1. Rebuild/start the current runtime on each target, using a distinct state
   directory, and use the current Studio or `s7ctl`. Existing certificate and
   clock requirements still apply.
2. Begin with one rate and a loop containing an explicit initialized `Delay`.
   Try memory on either host and then inside the reusable nested plant schema.
   A same-host loop should continue to behave as before.
3. Run `s7ctl check diagram.s7diagram --write planned.json` before deployment.
   Inspect `causal_schedule.nodes` if diagnosing ordering. Removing the loop's
   memory should produce a cycle error before any node is prepared.
4. For the first latency-sensitive cross-host feedback check, use
   `max_elements_per_packet: 1` on its stream descriptors and adequate send-log
   capacity (the automated socket test uses 64 packets). `check --write`
   preserves valid existing stream settings. A feedback loop with only one
   initialized token cannot fill a large packet with future values; the batch
   timer can dominate its wall-clock rate. These are test settings, not an
   automatic change to transport defaults.
5. Check reference values, achieved step rate, stream faults, blocked/log
   counters and runtime lag. Increase workload/placement complexity only after
   those checks. Correct causal values do not establish real-time deadlines.

The new core/net smoke tests exercise explicit orders, nested aliases, invalid
orders, two-/three-node feedback, local fan-out, slow owners and one-packet
credit. The deployment test checks unsupported-node refusal. Studio's network
service test checks that an invalid loop produces an error without a control
request. The existing two-process mTLS/shared/split deployment test now also
deploys nested feedback, records both nodes and checks every recorded value
against the independent recurrence. All test state/recordings use temporary
directories.

```sh
cmake --build build-release --parallel 2
cmake --build build-release --target build-tests --parallel 2
ctest --test-dir build-release --output-on-failure
```

## Implementation validation — 2026-11-17

The local Linux Release application and `build-tests` builds succeeded with
Studio enabled. **126 distinct CTest cases passed**, including the new core/net
smoke tests, runtime and Studio deployment checks, installed-SDK checks,
observation/lifecycle regressions and one bundled-example shard (27 example
test rows). This is not a complete 129-case suite result: the remaining three
bundled-example rendering shards were not completed; the broad run was stopped
during the second shard after the first passed. Desktop rendering had already
been checked by the maintainer.

The three socket deployment cases initially exposed an unregistered recording
element name in the new fixture. After switching it to `RecordingSink`, all
three passed, including independent recurrence checks of both recordings.
No production change was needed for that fixture correction. Results were
collected from tests 1–108 of the broad run, the corrected socket rerun and
tests 112–129:

```sh
ctest --test-dir build-release -R '^s7_two_node_(deployment|split_mtls|split_trusted_plaintext)$' --output-on-failure
ctest --test-dir build-release -I 112,129 --output-on-failure
```

Build/test logs for this working session are `/tmp/s7-static-final-build.log`,
`/tmp/s7-static-final-test-build.log`, `/tmp/s7-static-ctest-all.log`,
`/tmp/s7-static-ctest-network.log` and `/tmp/s7-static-ctest-tail.log`.
`git diff --check`, shell syntax and implementation-guide links were also
checked. These are automated local-process results; remote real-plant loops,
hardware deadlines and this implementation's Windows build remain untested.
