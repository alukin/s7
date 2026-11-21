---
name: s7-distributed-conformance
description: Test S7 cross-node causal behavior, control/wire protocols, interoperability, reconnect and failure recovery.
---

# S7 distributed conformance

Use for network deployment, control plane, NetSource/NetSink, Python/Java/C++ compatibility, scheduling and transport failures.

1. Read the relevant protocol or element contract under `doc/` and `s7l2/docs/DISTRIBUTED_CAUSAL_SCHEDULE.md` when changing deployment or scheduling. Check each participating implementation and its advertised capabilities. Java currently implements the data plane. Scheduled graph deployment requires every participating node to advertise the schedule version.
2. State invariants: explicit graph causality, state publication, stream identities and sequence/rate contracts, per-node state, control/data separation, clock domains and failure visibility. Never derive cross-node causality from packet arrival order or assume delivery/replay guarantees absent from implementation.
3. Start with the relevant shared fixtures. From the repository root, `python3 s7l2/tools/check_wire_fixtures.py` checks wire v1. In a built CTest tree, `s7runtime_control_fixtures_smoke` checks the C++ control plane. To check a running node, follow `s7py/README.md` for the fixture runner, loopback URL, test certificates and `--skip-streams` limits. See `doc/S7_Frame_Interoperability.md` for typed frame coverage.
4. For scheduled deployments, begin with `s7ctl check <diagram> --write <separate-plan>`. Check the explicit memory and single-rate limits, including rejection of a direct cycle before PREPARE when relevant. For runtime behavior, choose affected cases such as reconnect, backpressure or revision change and exercise the relevant nodes.
5. Keep tests isolated: ephemeral ports, temporary certificates and state directories, bounded test duration, controlled process cleanup. Never alter real deployment credentials, production nodes or expose plaintext test connections across an untrusted network.
6. Compare emitted events, timestamps, states and wire bytes with an independent expected trace. Record where gaps and retries are observable; do not paper over loss by ignoring samples.
7. Report command lines, commit SHA, node/runtime versions, scope of supported features, negative results and untested conditions. Do not claim physical multi-host validation from a local multi-process test.
