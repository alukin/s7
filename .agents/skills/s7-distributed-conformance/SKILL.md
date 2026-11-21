---
name: s7-distributed-conformance
description: Test S7 cross-node causal behavior, control/wire protocols, interoperability, reconnect and failure recovery.
---

# S7 distributed conformance

Use for network deployment, control plane, NetSource/NetSink, Python/Java/C++ compatibility, scheduling and transport failures.

1. Read `AGENTS.md`, `doc/S7_Control_Protocol_MVP.md`, `doc/S7_Network_Wire_Protocol_v1.md`, `doc/S7_Net_Element_Contract.md`, `s7l2/docs/DISTRIBUTED_CAUSAL_SCHEDULE.md`, and relevant `doc/S7_Frame_Interoperability.md`. Identify **implemented** profiles vs planned ones.
2. State invariants: explicit graph causality, state publication, stream identities and sequence/rate contracts, per-node state, control/data separation, clock domains and failure visibility. Never derive cross-node causality from packet arrival order or assume delivery/replay guarantees absent from implementation.
3. Find shared fixtures under `s7l2/s7runtime/tests/fixtures/` and `s7l2/s7transport/tests/fixtures/`; check exact implementation and test paths before invoking commands. Compare behavior across languages only for their supported common subset.
4. Start with offline serialization/fixture and local two-node checks; add connected hosts only when needed. Test deploy/prepare/commit/abort, illegal cycles, wrong ports/types, sequencing gaps, restart/reconnect, slow consumers, backpressure and revision changes appropriate to the contract.
5. Keep tests isolated: ephemeral ports, temporary certificates and state directories, bounded test duration, controlled process cleanup. Never alter real deployment credentials, production nodes or expose plaintext test connections across an untrusted network.
6. Compare emitted events, timestamps, states and wire bytes with an independent expected trace. Record where gaps and retries are observable; do not paper over loss by ignoring samples.
7. Report command lines, commit SHA, node/runtime versions, scope of supported features, negative results and untested conditions. Do not claim physical multi-host validation from a local multi-process test.
