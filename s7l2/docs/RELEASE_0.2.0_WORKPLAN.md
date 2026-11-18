# S7 0.2.0 work plan — qualification and remaining delivery

**Scope revised 2026-11-18.** The [release contract](RELEASE_0.2.0.md)
supersedes older feature assignments. **Distributed causal execution**, proven
on two physical hosts with an independent numerical reference, is the principal
remaining objective. The source still identifies as 0.1.2; previous successful
builds and experiments are baselines, not the final release result.

This file is the **single live status and acceptance record** for 0.2.0.
Keep detailed algorithm derivations, research data, architectural discussions
and historical implementation plans in their existing documents and Git
history. Do not duplicate them here or silently convert experiments into
production guarantees.

## Status and acceptance matrix

Definitions: **Implemented** means code is present, not release-qualified;
**Evidence delivered** means bounded research/test artifacts are available;
**Partial** means some but not all acceptance cases are recorded;
**Open** means an acceptance decision, implementation, fix or test remains;
**Deferred** means explicitly excluded from 0.2.0. Historical passing tests
must not be reported as final-candidate tests.

| Item / release gate | Status at scope revision | Existing evidence | Required remaining acceptance | Lead |
| --- | --- | --- | --- | --- |
| **G1 — Local numerical and semantic regressions** | Implemented baseline; final gate open | [Numerical matrix](NUMERICAL_VALIDATION_MATRIX.md), [integrator review](INTEGRATOR_REVIEW_2026-11-17.md), [0.1.2 notes](RELEASE_0.1.2_NOTES.md) | Complete final-candidate numerical/causality/type/rate/rollback/reference tests, including supported coupled methods; preserve recorded envelopes | Maintainer/Codex |
| **G2 — Static distributed causal scheduling** | Initial C++ implementation; physical-host loop gate open | [ADR-027](../../doc/S7_Architectural_Decisions.md#adr-027), [schedule contract](DISTRIBUTED_CAUSAL_SCHEDULE.md), [Phase 4](CROSS_NODE_CAUSALITY_PHASE4.md) | Two real hosts, explicit-memory/nested feedback and alternate placement; independent per-step results; invalid-cycle/capability preflight; measured resources | Maintainer/Codex, coordinated with teren_ukr |
| **G3 / D0 — Common distributed contract and test matrix** | Partial | C++/Python existing control and F64 data streams; shared fixtures; [transport lab](../../s7py/tools/transport_lab/README.md) | Freeze supported topology, security/transport profiles, host and resource envelopes; record expected pass/fail matrix; make cross-language checks required CI | teren_ukr |
| **G3 / D1 — Python data-profile parity** | Partial | Python F64 mTLS graph streams, ACK/RESUME and fault handling | Explicit split data listener and trusted-plaintext policy plus negative cases with C++; no implied Python cross-node static-feedback scheduling | teren_ukr |
| **G3 / D2 — Multi-node lifecycle** | Partial | Local mixed-language chain and reconnection checks | Multi-process fan-out, slow receiver, gaps, restart/redeploy/reconnect, controlled recovery, revisions and fault observations; two-host C++ loop in G2 | teren_ukr |
| **G3 — Failure and authorization boundary** | Partial | Existing [wire/stream contract](../../doc/S7_Network_Wire_Protocol_v1.md), control authorization and [schedule limits](DISTRIBUTED_CAUSAL_SCHEDULE.md) | Assert unauthorized, wrong-pin, profile mismatch, missing values, non-atomic network sends and explicit faults; document limits and operator recovery | Maintainer/Codex + teren_ukr |
| **G4 — Studio and packaged candidate** | Tooling present; final gate open | [Studio testing](STUDIO_GUI_TESTING.md), [Linux packages](NATIVE_LINUX_PACKAGES.md), [Windows checks](WINDOWS_BUILD.md) | Complete configured Linux/Studio/Wine regressions; candidate artifact rebuilding, relocation, notices, hashes and accurate native-vs-Wine qualification | Maintainer/Codex |
| **G4 — Reproducible two-host release experiment** | Open | [Distributed schedule smoke cases](DISTRIBUTED_CAUSAL_SCHEDULE.md) and research lab reports | Public schema, independent reference, tested commands, host/profile setup, measured figures and deliberate failure/recovery results | Maintainer/Codex + teren_ukr |

**Not on the 0.2.0 critical path:** D3 runtime administration and restart
protocol, D4 full credential tooling and remote provisioning UX, U2 printing,
U3 signal generator, U4 certificate viewer, U5 Trusted Networks UI, video
providers V0–V5, general test-label taxonomy T1, within-node parallelism,
automatic coherence/hardware clock alignment and multi-rate distributed
scheduling. Their previous detailed requirements remain in Git history and
linked design documents; the [0.3.0 record](RELEASE_0.3.0.md) is a candidate
backlog, not an automatic commitment to deliver everything in that release.

## Ownership and coordination

- **Maintainer/Codex:** S7 formal semantics, local numerical regressions,
  compiled causal schedule, the two-host reference experiment, Studio and
  final packaging/release decision. Preserve serial execution and the
  existing production numerical contracts.
- **teren_ukr:** D0 common-subset definition, D1 Python split-profile parity
  and D2 multi-node lifecycle. Own the corresponding cross-language
  fixtures and required checks. Coordinate network/control contract
  changes with the maintainer before implementations diverge.
- **Shared boundary:** C++ remains the reference for execution semantics;
  Python conformance covers an explicitly named subset. Do not make
  Python support for every C++ feature a hidden acceptance criterion.
  Ownership can change by an explicit maintainer decision, not by an
  unnoticed work-plan edit.

No separate workstream may introduce distributed graph semantics based on
packet arrival time. Control identity and authorization remain verified at
the transport and control boundary. Studio is a client of headless services,
not a second simulation/control implementation.

## Remaining work, in dependency order

### Step 1 — Freeze D0 and the candidate qualification matrix

1. Inventory existing C++, Python and Java implementation subsets. Document
   which operations and data profiles are **supported**, **supported after a
   listed D1 task**, or **not part of this release**.
2. Specify the test hardware (at least two physical Linux hosts), operating
   systems, CPU/memory, toolchain, process configuration and independent
   credentials. Include the serial rate, graph/stream sizes, recording length,
   expected sample counts, latency/throughput and memory/queue measurements.
   Choose meaningful performance acceptance bounds **before** the final
   qualification run; do not invent quantitative guarantees from a synthetic
   benchmark.
3. Freeze representative test cases: forward chain, fan-out, C++ two-host
   feedback, nested causal memory, mixed C++/Python traffic, invalid
   direct-feedthrough loop, blocked consumer, lost link, restart and
   redeployment. Specify the independent expected output or explicit fault
   state of every case.
4. Make shared fixture/conformance checks visible in CI and document where
   live two-host tests require opt-in test hardware. A test that cannot run
   in public CI must have repeatable local/host commands and archived results.

**Exit:** a reviewed matrix with no ambiguous claim that Python, Java or
Windows supports an untested distributed feature.

### Step 2 — Qualify the static causal schedule (G2)

<a id="feedback-loops-across-nodes--020-task"></a>

1. Run `s7ctl check` on the same two-host feedback diagram both before
   deployment and after serializing its schedule. Validate the entire
   flattened graph, including generated stream endpoints.
2. Record every output on both physical hosts and independently calculate
   the expected discrete recurrence. Compare ordered values, sample counts,
   initial conditions and final states. Repeat with causal memory on either
   host and nested inside a reusable plant. Check a simple forward topology
   and fan-out as controls.
3. Reject missing memory, an unrelated memory that does not break the
   direct-feedthrough cycle, a stale/malformed order and a node without
   advertised static-schedule version, before PREPARE where prescribed.
4. Start with `max_elements_per_packet: 1` and sufficient bounded send-log
   capacity for the feedback test, as described in the
   [schedule guide](DISTRIBUTED_CAUSAL_SCHEDULE.md). Show the effects of
   batching, bounded waits and slow consumers without treating wall-clock
   delivery order as causal semantics.
5. Measure throughput, achieved model-step pace, lag, CPU, RSS and queue/log
   occupancy. Report the precise graph and hardware. Record any qualified
   deadlines separately; **no hard real-time claim** is implied.

**Exit:** independent-reference, physical-host causal-sequence evidence and
explicitly stated supported profiles. Previously passing loopback/socket
smokes are regression inputs, not substitutes.

### Step 3 — Close D1/D2 lifecycle and security gaps (G3)

1. Finish Python split-profile listener/client and trusted-plaintext
   configuration only within the frozen D0 scope. Cover wrong identity,
   wrong endpoint/security scheme, unauthorized control and attempted
   policy downgrade. Reuse the shared wire/control fixtures.
2. Exercise at least three processes, with a C++/Python mix where the
   subset permits: forwarding, fan-out, asymmetric progress, backpressure,
   disconnect/reconnect, sequence gap, stream fault, controlled recovery,
   restart and redeployment. Check committed revisions and stream status
   after every change.
3. Inject faults while the C++ feedback model runs. Check that no absent
   sample is fabricated, failures are visible, and completed-step output
   stays consistent with the specified local publication contract.
   Explicitly record that a network send already performed cannot be
   rolled back by a later local step failure.
4. Use independently generated node/control credentials for the physical
   hosts via existing documented tools/manual provisioning. Demonstrate
   authorization with the correct control-purpose EKU and pinned fingerprint.
   **Do not** make a new cert-issuing program, GUI viewer, settings protocol
   or supervisor/restart service a release gate.
5. Include bounded stress/resource cases and relevant sanitizer checks;
   keep crash-persistent replay and exactly-once external actuation outside
   the contractual claim.

**Exit:** repeatable fail/recover scenarios with checked stream states and
precise, documented limits. Failure-injection success means predictable
failure semantics, not continuous operation through every fault.

### Step 4 — Close G1 and package G4

1. Re-run all applicable numerical tests, independently referenced
   oscillators/control examples, nonlinear validation, nested/rollback/rate
   checks and supported opt-in coupled integration. Preserve model
   initial-state, sample-time and comparison conventions.
2. Run Studio diagram/edit, viewer/analysis, lifecycle and distributed
   preflight regressions. Retain screenshots or recorded UI artifacts where
   a visual property matters. No new printing/instrument UI is required.
3. Build the final source/version into Linux portable x86-64, Fedora 45 RPM,
   Ubuntu 26.04 DEB and Windows portable ZIP artifacts in their documented
   profiles. Check installed/relocated operation, dependencies, notices,
   licensing and SHA-256 checksums.
4. Run full applicable Linux and Wine suites, cross-language fixtures and
   relevant sanitizers on the candidate. Record unsupported tests as
   exclusions with reasons; **never count unfinished or skipped tests as
   passes**. Native Windows camera/audio/device evidence is distinct from
   Wine regression evidence.
5. Publish the two-host experiment and release notes with the same commit
   SHA, dependency profiles, result provenance, known limits and exact
   reproduction commands.

**Exit:** all G1–G4 acceptance records refer to one identifiable candidate,
or a documented maintainer-approved revision of the release contract.

## Acceptance artifacts

Before tagging 0.2.0, gather:

| Artifact | Minimum contents |
| --- | --- |
| Numerical regression summary | Test command, candidate SHA, pass/fail/skip, independent reference and numerical envelopes |
| Two-host causal experiment | Public graph, expected recurrence, placement variants, host inventory, commands and complete observed sequences |
| Failure/recovery report | Scenario, injected fault, active revision, expected/actual stream status, output prefix, gap/recovery behavior and limits |
| Interoperability matrix | Supported C++/Python combinations, control/data profiles, fixtures and negative security cases; Java boundary explicit |
| Performance record | Rate, traffic, packet/batch settings, latency/lag, CPU, RSS, queue/log occupancy and reproducible host identifiers |
| Release/package manifest | Version/SHA, artifact checksums, packaging and relocation checks, notices, CI/local results and platform exclusions |

A headline test count without the exact candidate and test selection is not
sufficient evidence. Release criteria take priority over attractive additions
or further optimization experiments.

<a id="numerical-research-milestones--p1-planned"></a>

## Completed and retained research/baseline

The N0–N4 numerical investigations are delivered as separate research reports:
[N0](NUMERICAL_FEEDBACK_RESEARCH_N0.md),
[Phase 1](NUMERICAL_FEEDBACK_RESEARCH_REPORT.md),
[Phase 2](NUMERICAL_FEEDBACK_RESEARCH_PHASE2_REPORT.md) and
[Phase 3](NUMERICAL_INTEGRATION_PHASE3_REPORT.md). Results include limitations
and negative experiments. Production Euler substeps and coupled
Gauss–Legendre are implemented separately under
[COUPLED_INTEGRATION.md](COUPLED_INTEGRATION.md). The initial integrated
cross-node scheduler and its research are in
[DISTRIBUTED_CAUSAL_SCHEDULE.md](DISTRIBUTED_CAUSAL_SCHEDULE.md).
The [integrator review](INTEGRATOR_REVIEW_2026-11-17.md), [sample-rate
contracts](../../doc/S7_Sample_Rate_Model.md), [frame services](FRAME_TRANSPORT.md),
[hardware qualification](HARDWARE_IO_LIVE_TESTS.md) and
[Studio analysis](STUDIO_ANALYSIS_TESTING.md) likewise supply existing
baseline/evidence, not new feature assignments.

For historical task breakdowns (B1/B2, N0–N4, R1–R7, S1–S3, F1, H1,
X1–X3, U1–U5, T1, PKG, Q1 and D0–D5), consult this file's Git history
before the **2026-11-18 scope revision** and the original linked design
documents. This condensed work plan intentionally keeps **only** active
0.2.0 acceptance work.

## Stop rule and release decision

Stop accepting unrelated new capabilities into 0.2.0. Fix regressions or
inconsistencies revealed by G1–G4; do not require new video, PKI, remote
administration, printing, signal-generator or general parallel/multi-rate
features to close a gate. Any substantive gate change needs an explicit
maintainer decision, an updated contract and a visible release-note exclusion.
