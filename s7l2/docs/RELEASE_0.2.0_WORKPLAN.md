# S7 0.2.0 work plan and distributed development

Working plan recorded 2026-10-30. The maintainer and Codex own architecture,
development planning, simulation accuracy, integrator research and Studio
usability. teren-ukr leads the distributed work, building on his Python-node
work. The ownership split reflects that agreement; the steps below are a
proposed sequence. The [0.2.0 release scope](RELEASE_0.2.0.md) assigns the
separate numerical research branch to this release as a P1 research deliverable,
without requiring production adoption of an experimental algorithm.

## Ownership and decision boundaries

| Area | Lead | Review boundary |
| --- | --- | --- |
| Causal model, runtime and protocol architecture, release gates | Maintainer and Codex | Agree on semantics and an executable contract before implementations diverge. |
| Distributed transport, multi-node lifecycle, headless administration and Python interoperability | teren-ukr | C++ remains the reference implementation. Changes to its protocol, authorization or causal behavior require maintainer/Codex review. |
| Within-node parallel execution (0.3.0 planning) | Maintainer and Codex | Deferred from 0.2.0. Scheduling, cost and plugin-safety decisions remain open; the indexed serial runtime, with sequential nested schemas and compiled exposed-output bindings, is the reference. |
| Numerical integration and feedback-settling research (P1, planned) | Maintainer and Codex | Deliver reproducible evidence and conclusions for 0.2.0 from a separate branch; production integration requires explicit review and decision. |
| Studio editing, observation and administration UI | Maintainer and Codex | Headless services expose typed operations; Qt remains a client. Coordinate request/result types with the distributed work. |

Small PRs should follow these boundaries. A Python implementation does not
define a new S7 protocol or numerical rule by itself: the language-neutral
contract, C++ reference behavior and shared fixtures must agree. Conversely,
tests should expose a C++ defect rather than copying it into Python.

## Distributed goal and architecture

The goal is a qualified workflow for several node processes on one host and
at least two real hosts, including mixed C++/Python nodes where the documented
common subset permits it. Deployment, streaming, discovery, configuration,
restart and recovery must form one observable lifecycle. The release must state
which graph sizes, rates, node counts, operating systems and resource limits
were measured; a local two-node smoke test is a baseline, not remote acceptance.

Keep these existing boundaries:

1. `s7core` validates, compiles and executes each node's causal graph. A network
   boundary is explicit `NetSink`/`NetSource` behavior; packet arrival time does
   not determine graph causality. The scheduler may change only under a separate
   within-node contract.
2. `s7transport` owns sessions, wire framing, stream ordering, ACK/RESUME,
   backpressure and data-security enforcement. `s7net` owns the network
   elements and uses transport services; `s7runtime` owns node lifecycle,
   deployment and control policy. `s7ctl` and Studio are clients.
3. Control mutations require the transport-verified `ControlPeer`, control
   purpose EKU and pinned fingerprint. Split control/data listeners keep mTLS
   control and explicit encrypted or trusted-plaintext data. There is no
   downgrade fallback or authorization by JSON trust labels.
4. Studio discovery credentials, diagram `runtime_nodes`, and runtime listener
   settings have different owners. `GET_NETWORK_CONFIGURATION` returns the
   deployed diagram, not node startup settings. New administration operations
   need their own versioned request/result contract, validation, revision and
   idempotency rules. A restart is performed by an identified supervisor; the
   runtime cannot promise to restart its own process after it exits.
5. A completed step is the observation boundary. Network metrics and faults
   report identity, revision, stream and sequence without mutating the graph.
   Existing payload replay across a crash is not guaranteed; any broader
   durability claim requires a separate retention and acknowledgement design.

The [split transport contract](SPLIT_TRANSPORT.md),
[control protocol](../../doc/S7_Control_Protocol_MVP.md),
[transport architecture](S7_Transport_Architecture.md) and
[runtime architecture](../../doc/S7_Runtime_Architecture.md) are starting
references. The transport architecture contains future proposals; this plan
does not add NAT traversal, relays, consensus, automatic failover, remote
observation or implicit multi-rate conversion to 0.2.0.

## Development steps for the distributed work

Each step should produce a reviewable PR with the changed contract, focused
tests and stated limits. Validate the C++ reference contract and shared
protocol fixtures before claiming Python conformance.

| Step | Deliverable and acceptance |
| --- | --- |
| D0. Baseline and contract | Inventory the C++/Python common subset and missing cases. Record supported hosts, security profiles, resource and performance measures. Turn the control/wire fixtures and Python checks into required CI gates. Write the multi-node acceptance matrix before changing behavior. |
| D1. Python data-plane vertical slice | Implement the documented wire v1 and split data listener/client for one F64 `NetSink`/`NetSource` stream. Interoperate with a C++ node; verify identity/policy rejection, order, ACK/RESUME, bounded send log, disconnect and explicit fault. `s7py` currently has control but no data plane. |
| D2. Local multi-node lifecycle | Exercise at least three processes, including a C++/Python mix: fan-out, bidirectional streams, different wall-progress rates, redeployment, restart, sequence gap and controlled recovery. Give each node a separate identity/state directory. Assert the committed revision and stream state after each transition. |
| D3. Administration contract and headless implementation | First review a protocol design for read/validate/stage/commit of node settings, optimistic revisions, atomic persistence, apply timing, lost-connection reconciliation and supervisor-owned restart. Then implement the C++ reference, CLI and shared fixtures; add Python parity for the supported subset. Test unauthorized requests and changed endpoint/credential/pin cases. Do not reuse deployment `GET_NETWORK_CONFIGURATION` for node settings. |
| D4. Provisioning and remote hosts | Agree certificate profiles, signing/CSR workflow, key protection and replacement behavior. Implement a headless credential tool and use independently generated credentials on at least two physical hosts. Verify mTLS control and both explicit data profiles, restart/reconnect and packaging without test-fixture keys. Studio's certificate viewer belongs to the UI track. |
| D5. Qualification | Measure throughput, latency, CPU, memory, queue/log occupancy and recovery time on named hardware for representative topologies. Run stress/sanitizer and platform checks, document failures and limits, and close the acceptance matrix. Keep constrained-device results separate until measured there. |

D1 can start after D0. Within-node parallelism is outside the 0.2.0 delivery
path and is recorded for 0.3.0 planning. D3's protocol review precedes its
implementation. D4's generated credentials
precede claims about remote-host provisioning. Studio's settings and node UI
should consume the reviewed D3 service rather than introduce a second protocol.

## Numerical research milestones — P1 planned

The maintainer and Codex own N0–N4. This work proceeds on a dedicated research
branch independently of D0–D5 and Studio delivery. The
[release scope](RELEASE_0.2.0.md#numerical-integration-and-feedback-settling-research--p1-planned)
defines the model matrix, architecture constraints and acceptance gate;
[open problems](OPEN_PROBLEMS.md#simulation-accuracy--numerical-research-for-020)
records unresolved decisions. Existing indexed serial execution is the
performance baseline, not a pending numerical milestone.

| Milestone | Deliverable and review gate |
| --- | --- |
| N0 — Experimental specification | Define each integrator update and feedback-settling contract, including publication, held state, iteration and failure semantics. Freeze reference models, parameters, timestep grids and independent references. Record baseline measurements and correctness, convergence and performance metrics before trying new methods. |
| N1 — Experimental implementations | Implement candidate methods on the research branch while preserving production behavior. Use configurable iteration limits and tolerances where relevant; expose non-finite values, nonconvergence and other numerical failures reproducibly. No production merge follows automatically. |
| N2 — Numerical validation | Run harmonic oscillator, nonlinear pendulum, Van der Pol, Lorenz and linear/nonlinear closed-loop cases against independent references. Test multiple timesteps, long-duration behavior and convergence/failure cases; use short-horizon and statistical/geometric criteria for chaos. |
| N3 — Performance analysis | Compare accuracy versus timestep and computational budget against the indexed serial runtime. Record per-step time, element evaluations, feedback iterations, memory where relevant, failure counts, long-term stability, regressions and instability. Distinguish numerical improvement from extra evaluations alone. |
| N4 — Research conclusions | Publish a technical report with methods, reproducible results, limits and recommendations. Name promising, rejected and unresolved approaches; make an explicit recommendation on whether any implementation merits production review. Negative or inconclusive results complete the research if properly documented. |

N0 precedes N1; N1 enables N2 and N3; N4 uses both validation and performance
evidence. The 0.2.0 research gate requires the branch artifacts and report,
including failed experiments. A proposed production change has a separate
review: demonstrated benefit, numerical and causal-semantic approval, preserved
existing behavior or an explicit mode, regression/compatibility checks and an
explicit maintainer decision. No new integrator or settling mechanism is a
required production capability for 0.2.0.

## PR review agreement

The maintainer requests a Codex review of every distributed PR before merge.
Each PR should identify its D-step, the contract or fixture it uses, C++
reference impact, Python subset impact, deterministic test results and
operational limits.
Review checks include:

- causal order, step boundaries, model time and stream sequence after faults;
- TLS peer identity, authorization, explicit data policy and no fallback;
- bounded queues, cancellation, graph replacement, shutdown and restart;
- idempotent control operations, revision conflicts, atomic persistence and
  reconciliation after lost replies;
- C++/Python interoperability through the same fixtures, plus local and real
  process tests where the PR claims them;
- build, sanitizer, package and platform evidence appropriate to the change.

No PR is accepted solely because it builds or because a Python-only test
passes. A new wire or control field needs a documented contract and conformance
fixture. A cross-module numerical or scheduler change needs the simulation
track's review as well. Reviews occur when PRs are submitted; this plan does
not establish an unattended monitor.

## Our parallel work

1. **Architecture and development plan:** close the 0.2.0 scope and acceptance
   matrix first. Audit graph replacement, stream ownership and shutdown; then
   record the indexed serial measurements and unresolved within-node parallel
   questions for 0.3.0 planning. Nested-schema exposed outputs are bound once
   at compilation (done 2026-10-31); nested schemas stay sequential in 0.2.0,
   and their parallel scheduling is a possible 0.3.0 research topic. Review D3 administration semantics and
   integration boundaries before implementation, keeping the language-neutral and C++
   documents aligned.
2. **Simulation accuracy, research and Studio usability:** extend independent
   numerical references and supported envelopes for saved models before
   changing execution. Deliver N0–N4 on a separate research branch without
   adding a global solver; compare methods, feedback settling and timestep
   refinement before proposing a production change. Improve diagram
   sizing/pictograms, printing and instrument controls through focused Studio
   owners; build the signal generator from schema elements with headless tests
   and Studio controls. Certificate viewing and network settings UI consume
   reviewed headless contracts from the distributed track.

The first joint review should accept or amend D0's matrix and the administration
design questions in D3. This lets distributed implementation and our local
simulation/Studio work proceed without changing each other's contracts by
accident.
