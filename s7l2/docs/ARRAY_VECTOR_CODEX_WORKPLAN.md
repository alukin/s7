# S7 0.2.0 vector signals and observers — bounded Codex session plan

**Status:** D1 compatibility policy, D2 publication, D3 buffer ownership and D4 descriptor foundation, D6 provenance semantics and D7 nested array scope approved. D5 FFT and PSD conventions approved. See the [AV00 baseline](ARRAY_VECTOR_AV00_BASELINE.md) for measured scalar behavior.

Preserve scalar numerical behavior and fast bindings while introducing vector elements that share a reusable array, lease and port substrate. Earlier S7 versions and third-party plugins carry no compatibility obligations. Update repository-owned components and bundled diagrams to the chosen current interfaces and formats. The 0.2.0 slice includes local vector wires, FFT/PSD array output, a read-only Waterfall and a simple Video Player with synthetic/recorded whole-frame input. Production FFmpeg/GStreamer/camera, network tensor transport, 2D FFT and spatial solvers are out of scope.

Companion documents:
- [Independent review](ARRAY_VECTOR_INDEPENDENT_REVIEW_2026-11-23.md) — findings F1–F12 and proposed, unapproved fixes.
- [Migration/source inventory](ARRAY_VECTOR_MIGRATION_INVENTORY.md) — impacted code and explicit preserve/defer classification.
- [Array signals design](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md) — conceptual model and causal/ownership contracts.
- [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) — authoritative live release gate status; this file is the **Codex implementation decomposition**, not a competing status matrix.
- [Studio printing](STUDIO_PRINTING_0.2.0.md) — includes P08/P09 Waterfall/Video Player snapshots.

## Session operating contract (every Codex session)

**Hard limit: one Codex session must fit within a five-hour allocation.** Task sizes below are budgets, not predictions or guarantees. Aim for a focused change and leave verification/handoff capacity inside the five hours. If any task is bigger than it appears, **stop at a coherent tested boundary, explain what remains, and split it into a smaller follow-up session**; never mask incomplete tests or expand the timebox.

Each session uses a fresh branch/PR or an explicitly agreed continuation; do not have parallel agents edit overlapping core headers or change main in conflicting ways. The dependency order below is deliberately mostly sequential for core changes. Later viewer work can branch once the core contracts are stable. Avoid one giant PR. Documentation-only scope decisions may land on main; code PRs should be reviewed, built and merged one dependency at a time.

### Common start
1. Read root and \`s7l2/AGENTS.md\`, this plan, the inventory, design notes and relevant ADRs (particularly step rollback, observation, type/rate contracts). Use the newest \`main\` head, inspect \`git status\`, and **recheck source** before relying on this inventory.
2. Record task ID, base commit, build profile/OS/compiler and chosen single deliverable. Do not blindly continue a prior unfinished patch.
3. Locate existing CMake/tests/scripts. Prefer \`tools/ai/README.md\` and the nearest reusable tool; avoid inventing duplicate scripts. GUI capture uses the project GUI test workflow, not a claimed headless screenshot.
4. Make **one modest behavior change** plus focused positive and negative tests. No broad formatting/refactoring, unrequested changes under \`external/\`, or silent schema/protocol redesign.
5. Run the focused headless build/tests and appropriate sanitizers where feasible. For schema/graph changes run actual saved-diagram validation; for GUI sessions run relevant GUI tests **and inspect actual screenshot/image evidence** when available. A test not executed is *not passed*.
6. Commit/push only verified changes to the task branch, open/update the PR if available, and hand off exact commands/results, relevant SHA, untested items, risks and the next session ID. Do not merge or mark a 0.2.0 gate closed solely because a Codex task completed.

### Common prohibitions
- No scalar element rewrites; \`step()\` remains causal, not a physical time tick. Retain existing rate/model-dt meaning.
- No automatic numeric conversion/broadcast/implicit reshape or silent \`Resampler\` insertion.
- No \`memcpy\` of nontrivial lease objects; no payload copies merely to connect, fan out, delay, stage or roll back a vector.
- No output observed until committed; no in-place mutation of a published value. Existing private-state/external-effect rollback limits remain explicit.
- Do not open microphones, speakers, radio TX, cameras, network endpoints, or install packages in ordinary tests. Use opt-in named hardware profiles only in later releases.
- Do not reinterpret scalar \`NetFrameSource/Sink\` packet framing as array-valued graph wires.
- Do not change the scalar numerical-trial/coupled-solver API to accept arrays before a reviewed numerical contract; unsupported arrays must fail explicitly.

### Required session handoff record

For each session, provide a PR description or checked-in task note with:

~~~text
Session: AVxx
Base SHA / resulting SHA:
Files / APIs changed:
Single deliverable achieved:
Positive tests (exact commands, pass/fail):
Negative/fault tests (exact commands, pass/fail):
Saved diagram(s) / reference oracle:
Scalar regression / measured allocations or copies:
GUI screenshot review (if applicable; actual paths/evidence):
Blocked items, unsupported profiles, remaining risks:
Next AVxx dependency:
~~~

A standalone implementation session is **not complete** if its promised tests are not exercised. A timeboxed partial result should be honestly marked WIP and handed off; it is not a successful release acceptance.

## Binding dependency DAG and review gates (supersedes original linear ordering)

**AVxx identifies a work family, not automatically one five-hour session.** Each suffix `a/b/c/d` below is a separate **maximum five-hour Codex allocation including tests, independent oracle review and handoff**. Stop and record WIP rather than joining adjacent suffixes to exceed the limit. No public array element is advertised before AV06b's fault/ownership gate; no viewer is called complete before headless data and visual oracle evidence.

~~~text
AV00 baseline, source SHA, D1–D7 decision register
  +-- [D4] --> AV01 descriptors --> AV02 metadata --> AV03 exact shape/type checks
  |                                       |                 |
  |                              [D1 policy]              |
  +-- [D3] --> AV04 CPU producer pools/leases <-------------+
                                              |
                                     AV05a stable slot API
                                              |
                                     AV05b compiled fan-out
                                              |
                                     [D2] AV06a candidate/commit
                                              |
                                     AV06b faults/retry (safety gate)
                                              |
                          +-------------------+--------------------+
                          |                   |                    |
                    AV07a memory         AV09a internal       AV13a headless
                          |              fixture only        observation
                    AV07b Delay              |                    |
                          |                  |                  AV13b GUI
                   [D7] AV08a flattened     |                  mailbox
                          |                  |                    |
                    AV08b persistence --------+----------+         |
                          |                              |         |
                    AV09b public source/Pass/Gain         |         |
                          |                              |         |
                      +---+-----------+                  |         |
                      |               |                  |         |
                       AV10a      [D6] AV12 U8           AV14a pin+shape
                       |               |                  |
               [D5] AV10b FFT          |                  AV14b registry/routing <--- AV13b
                       |               |                  |
                   [D5/D6] AV11a       |                  |
                       |               |                  |
                    AV11b PSD          |                  |
                       |               |                  |
                       +---------- AV15a/b Waterfall <----+
                                       (independent)
                    AV12 ------------ AV16a/b Video <-----+
                                                 |
                       AV15b + AV16b --> AV17a P08/P09
                                             |
                                           AV17b packaging
                                             |
                                    AV18a/b/c/d evidence
~~~

The ASCII layout denotes prerequisites **by the explicit table below**, not a requirement to finish Waterfall before starting AV12 or Video. AV04 can begin after AV01 and D3 while AV02/AV03 proceed separately; AV05 convergence requires both validation and leases. AV09a is an **internal** test fixture allowed immediately after AV06b, independently of AV07/AV08; AV09b *public* registration requires AV07b/AV08b. AV13a can start after AV06b, without FFT/Waterfall or public AV09. AV14a may begin after shape metadata and flattened schema, while AV14b requires headless + Studio observation delivery. Shared ABI/core headers must be merged sequentially or explicitly coordinated. **AV12 is NOT downstream of AV15**; AV15 and AV16 are parallel presentation tracks.

### Mandatory per-family task split and evidence (review-driven superseding execution cards)

The descriptive AV00–AV18 cards below remain feature-level context. **This table defines actual per-session dependency, boundary and minimum acceptance.** A suffix cannot be combined with another task without re-planning a new, still bounded reviewable session; failed tests block publication. Source/test paths must be rechecked against the chosen base SHA.

| Session | Required predecessor / decision | One bounded deliverable and acceptance |
| --- | --- | --- |
| **AV00** | Current main | Freeze base SHA, source/CTest inventory, installed SDK and rebuilt repository plugin smoke, scalar dispatch time/allocations on a specified host/build, ADR-021/022/024/029 fault and rate baselines, and the decision register. Record an evidence ledger. |
| **AV01** | AV00 + **D4** | Generic fixed numeric shape and compact CPU view descriptors. Test positive, zero and invalid extents, overflow before allocation, alignment and backing ranges. Cover native rank zero, DSP and video, and F64 rank-two and rank-four spatial shapes. |
| **AV02** | AV01 + **D1 policy** | Metadata JSON parser -> registry -> catalog -> canonical schema representation. Update repository-owned metadata and fixtures as needed. Reject malformed format and rank. |
| **AV03** | AV02 | Headless exact numeric type/rank/extents/format validation, scalar/vector mismatch, no symbolic/broadcast/reshape, forbidden array coupled-trial and invalid-cycle cases. Exercise compiler rejection and scalar compilation. The first executable array fixture is AV05b. |
| **AV04** | AV01 + **D3** (parallel to AV02/03) | Headless candidate writable pool, read leases, stale-generation/cancel safety, execution-reserved bytes vs optional retention budget; tiny-pool slow-viewer/stalled-consumer producer-progress and bounded skip tests. |
| **AV05a** | AV03 + AV04 + **D1** | Stable array output slot/input binding and typed lifetime rules; unit tests for slot swap, borrow/retained lease, mixed scalar+array ports, no byte-copied owner. No public vector registration. |
| **AV05b** | AV05a + **D2 contract approved** | Real Compiler/NodeRuntime source -> pass-through -> dual consumers, same payload identity and instrumented zero wire copies; negative shape checks, scalar CTest and before/after scalar benchmark. Experimental/internal only. |
| **AV06a** | AV05b + **D2** | Per-domain direct candidate vs externally committed snapshot machinery, fallible admission before nonthrowing commit; smoke mixed scalar+array outputs and observer invisibility until commit. |
| **AV06b** | AV06a | Late direct and multi-output exceptions, repeated retries, retained observers, exhausted pool, later-domain fault/committed-prefix checks vs unfaulted oracle; no payload copy on handle rollback. **Unlocks internal array fixtures.** |
| **AV07a** | AV06b | Causal-memory previous valid and next staged handle phases; first/last memory-fault retries preserve previous staging and scalar semantics. |
| **AV07b** | AV07a + **D6 forwarding rule** | Public whole-array VectorDelay/initialized feedback fixtures; recurrence oracle, valid memory vs illegal direct cycles, source occurrence retained through delay and retry, pool-scoped retention across the initializer-to-upstream switch, one whole value delayed per domain evaluation. |
| **AV08a** | AV07b + **D7** | Flattened nested exposed-port slot aliases, direct/Delay inline-vs-nested oracle, mismatch and placeholder-byte negative cases. Standalone array Schema is deferred by D7. |
| **AV08b** | AV08a | Schema+Studio save/open/save in the current format. Cover shape/format/layout capability, exposed aliases, invalid ranks and current scalar diagrams. No remote profile work. |
| **AV09a** | AV06b | Minimal **internal-only** deterministic array source/forwarding headless fixture; repeated steps, copy/alloc ledger and fault injection, not public library. May precede AV07/AV08. |
| **AV09b** | AV07b + AV08b + AV09a | Public VectorConstant/PassThrough/Gain, PackVector and UnpackVector with saved compiled diagrams, an independent elementwise oracle, preallocated reference profiles and a mixed status scalar port. |
| **AV10a** | AV09b | Audit the four FFT paths against accepted D5. Record reuse, supported lengths, scratch/output reservations, SDK linkage and numeric tolerances. Prepare an independent DFT fixture. |
| **AV10b** | AV10a + **D5 FFT decision** | Whole-vector FFT C32[N] and/or explicitly approved conversion, impulse/DC/+/- tone/two-tone/inverse and normalization/bin-order tests vs independent DFT; tracked copies/allocations, plugin load smoke. |
| **AV11a** | AV10b + **D5/D6** | Written PSD/window/floor/power/bin-order, sample-rate/first-index/provenance contract, valid unknown-rate and gap policy; reference frequency-axis/PSD oracle. |
| **AV11b** | AV11a | Headless PSD/magnitude element and saved array path; DC, zero/floor, signed bins, tones, power/tolerance, shape/rate mismatch and discontinuity tests. |
| **AV12** | AV09b + **D4/D6**, independent of AV15 | Deterministic U8 [H,W]/[H,W,3] frame fixture with frame ID and explicit layout capability; exact independent checkerboard/moving-bar pixel oracle, padded rows **if supported**, invalid shape/format and headless saved diagram. Split AV12a/b if this cannot fit five hours. |
| **AV13a** | AV06b + **D3** | Headless committed-array Observation API, coherent domain snapshots, retained lease after advancement, cancellation and tiny-pool no-starvation oracle; preserve scalar Observation/Scope. |
| **AV13b** | AV13a | Bounded thread-safe stepping-to-Studio mailbox; skip counters, attach/detach/paused/closed slow viewer, sanitizer/lifetime evidence and cross-domain position disclosure. |
| **AV14a** | AV03 + AV08b | Studio ordinary **and observer** pin numeric/rank/shape/format checking; metadata JSON/catalog/labels, invalid drag/drop and round-trip GUI tests. |
| **AV14b** | AV14a + AV13b | Waterfall/Video registry, saved bindings, source routing, rebind/open/close lifecycle and scalar Studio regression. No rendering claimed. |
| **AV15a** | AV11b + AV13b + AV14b + **D5/D6** | Bounded deterministic PSD row->image mapping with frequency/time axes, gaps/unknown-rate policy, range/palette reference-pixel oracle. |
| **AV15b** | AV15a | Actual Waterfall GUI, scale/controls, pause/reopen/rebind/skip evidence and reviewed screenshot/image comparisons; printing input contract ready, no model-state changes. |
| **AV16a** | AV12 + AV13b + AV14b | U8 grayscale/RGB frame->image adapter; exact pixel/stride/aspect oracle, bounded newest-frame and explicit unsupported-format/skip behavior. |
| **AV16b** | AV16a | Actual Video Player UI and status/controls, pause/close/reopen/rebind/screenshots independently reviewed; no device/codec or model pause. |
| **AV17a** | AV15b + AV16b | **P08/P09 only** coherent retained snapshots printed to PDF/printer, axes/legend/row gaps and exact video frame identity/pixels, cancel/error/no-step-advance; separate existing **P01–P07** workstream still mandatory. |
| **AV17b** | AV17a | Viewer help, registry/resources and installed/relocated package/SDK smoke, actual PDF visual inspection and print matrix integration. No claim that P01–P07 were implemented here. |
| **AV18a** | AV17b | Candidate-SHA headless graph, negative shape, nested, rollback, independent FFT/PSD/frame oracles and G1/V evidence ledger. |
| **AV18b** | AV17b | Candidate-SHA bounded pool/copies/allocations, scalar benchmark comparison, sanitizer and multi-rate fault evidence; explicit results/skips. |
| **AV18c** | AV17b + existing printing P01–P07 | Candidate-SHA Studio GUI image/pixel/axes/lifecycle and full **P01–P09** coherent print/PDF visual matrix, supported printer gaps recorded. |
| **AV18d** | AV18a/b/c + independent G1–G4 owners | Candidate-SHA installed SDK and rebuilt shipped plugins, platform/package tests and release evidence reconciliation; cannot close G1–G4 merely because AV cards pass. |

**Decision blockers:** D1–D7 are accepted in [the design register](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#open-decision-register--explicit-maintainer-approval-required). Record accepted choices before their gated implementation sessions.

## Session cards — narrow work units

### AV00 — Baseline and test contract (no production API change)

**Goal:** capture existing scalar performance/rollback/type/Studio baselines and identify the smallest legal first array shape/lease interfaces.

**Scope:** inspect \`s7core\`, \`s7observe\`, \`s7studio\`, plugin SDK, metadata generator/build rules, existing test names, CTest registration and diagram schema syntax. Produce a short concrete test matrix and API decision note (array port slot ownership, numeric type plus fixed shape, supported rank/type matrix). Run representative baseline scalar tests; capture measurement environment, without a fabricated speedup target.

**Accept:** source and test links, baseline commands/results, and explicit open architectural questions. See the [AV00 baseline](ARRAY_VECTOR_AV00_BASELINE.md).

### AV01 — Fixed shape and view descriptors

**Depends:** AV00 and D4 approval.

**Scope:** implement [D4](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#d4--fixed-shape-cpu-layout-and-domain-semantics) with rank-generic numeric extents and a separate validated compact, aligned CPU view. Rank zero describes native scalar storage. Exercise rank one for DSP, ranks two and three for U8 images, and ranks two and four for F64 fields. Domain validation, runtime publication and port registration belong to later cards.

**Accept:** tests for empty rank, valid extents, zero and negative extents, derived dense strides, offset bounds and alignment. The view API accepts only compact CPU backing. Complete the core build and selected regressions. Include a negative test proving an unsafe large shape fails before payload allocation.

### AV02 — Port metadata and schema declaration

**Depends:** AV01.

**Scope:** extend \`PortSpec\`, metadata/registry and the runtime schema to declare concrete fixed-shape array ports. Use one canonical format for scalar and vector ports. Keep numeric type separate from shape and conversions explicit. Execution bindings are AV05.

**Accept:** metadata parse/serialize/create tests, current scalar element metadata resolution, and malformed rank/extent rejection. Represent scalar defaults centrally.

### AV03 — Numeric + shape inference and compile-time rejection

**Depends:** AV02.

**Scope:** extend resolver, connection validation and compiler preflight to match both numeric type and exact fixed shape. No implicit broadcast or scalar-to-vector coercion. Add explicit unsupported cases for coupled/implicit numerical trials and prohibited feedback. Keep existing scalar wiring tests intact.

**Accept:** validator tests for matching/mismatching F32/C32 and U8 shapes, scalar-to-vector mismatch, different rank, illegal cycles and array numerical-trial rejection. Exercise the compiler's error path and compile current scalar diagrams.

### AV04 — CPU buffer leases and bounded pool

**Depends:** AV01 + D3; independent of AV02/AV03 until AV05 convergence.

**Scope:** a headless \`s7core\` CPU lease/pool, read-only typed access, writable candidate acquisition, stable memory lifetime, bounded capacity, immutable publication contract. Resource exhaustion must be explicit. No Studio, hardware or GPU support. Design borrowed view versus retained lease so compiled synchronous fan-out has no mandatory atomic increments per wire.

**Accept:** pool lifetime, multireader retention, exclusive writes, stale/invalid lease rejection, deterministic pool exhaustion, sanitizer-ready tests and no payload copy on share; report the allocation policy. Do not infer "end-to-end zero-copy" from this unit test.

### AV05 — Array-valued Element/Port slots and compiler binding

**Depends:** AV03/AV04.

**Scope:** add a second (array) storage class under the same logical port contract; bind array inputs to stable output **slots** rather than movable payload addresses. Prove a minimal non-stateful source -> pass-through -> consumer direct-feedthrough chain using the real Compiler/NodeRuntime. Keep scalar fast path unchanged; don't add array history/feedback yet.

**Accept:** compiled matching vector pipeline, fan-out to two readers yields identical payload identity, wrong shape/type preflight fails, scalar CTest regression and a small before/after scalar dispatch benchmark or a documented baseline comparison. If full rollback is not yet present, mark the feature internal/experimental and prevent release use until AV06.

**AV05b evidence:** The headless Release fixture recorded 12 wire reads across four steps. Each read used the address of the source's freshly sealed payload, with zero bytes materialized at the wires. The selected CTest run passed 14 tests, including array validation, scalar rollback, numerical integration, nested schemas and observation.

The scalar benchmark compared AV05a (`2dbda9f`) with AV05b using GCC 16.2.1 Release builds on the same host. Each median uses five interleaved runs of one million steps pinned to CPU 4. All output checksums matched.

| Scalar graph | Before, ns/step | After, ns/step |
| --- | ---: | ---: |
| Chain of 512 Gains | 1786.10 | 1791.34 |
| 512 Gains sharing one source | 1449.97 | 1451.69 |
| Scalar feedback | 28.99 | 30.41 |
| Flattened nested feedback | 208.77 | 210.86 |

The scalar feedback median increased by 1.42 ns (4.9%). The run ranges overlapped at 29.26–29.42 ns. Recheck this case during release qualification.

### AV06 — Direct array candidate publication and faults

**Depends:** AV05b + D2; split into AV06a/AV06b according to the binding table.

**Scope:** extend direct-output publication to stage/commit array **handles** atomically at the logical step boundary. Downstream direct elements see current candidates. External Observation sees last committed output. A fault after a producing element restores prior published slots and leaves retained observers intact. Do not make coupled numerical trial regions array-capable.

**Accept:** late direct-element throw, several outputs, fan-out, observers, retry, memory pool safety, stable prior published payload and no copying of array bytes during rollback; scalar ADR-021 tests remain green.

**AV06a evidence:** The observation smoke test steps a mixed scalar/array source into a downstream reader. During evaluation, the downstream reader sees the current scalar and array values. Array observation sees the previous domain position and payload, or no payload before the first commit. After each step it retains the exact buffer used by the graph at the new domain position. The test also checks that another thread cannot acquire samples directly from the runtime. All 16 selected headless Release tests passed, including the four array smoke tests, observation and scalar rollback. AV06b covers the fault, repeated-retry and exhaustion matrix.

**AV06b evidence:** `s7core_array_rollback_smoke` checks first-step failure, a failure between two output writes, a late failure after forwarding, and execution-pool exhaustion after publishing one candidate. Each rollback preserves the published buffer addresses and array values. A retained view survives repeated faults while optional admission saturates. Clocked and unclocked retries match an unfaulted run and preserve the committed prefix across three consecutive faults. An injected failure in scalar rate-boundary delivery preserves the source commit and latches the fault. All 18 selected headless Release tests passed. Internal direct-array fixtures can use the experimental compiler entry point.

### AV07 — Causal memory, VectorDelay and feedback

**Depends:** AV06.

**Scope:** new VectorDelay (and/or explicit whole-array PreviousValue variant) using handle staging and defined startup values. No modification of scalar Delay. Ensure all causal-memory elements see the correct previous committed values, including two independent feedback states and failure in a later memory element.

**Accept:** deterministic vector recurrences, initialized memory, invalid direct-only cycles, memory-phase throw/rollback/retry, no unintended payload copy, no one-sample vs block ambiguity. Include a checked saved diagram if metadata/wiring are ready.

**AV07a evidence:** `s7core_array_memory_smoke` exercises two mixed scalar/array memory elements in a feedback loop. Both elements and a direct probe read the same current values throughout evaluation. The test injects faults in the first and last memory elements, including consecutive faults, and checks the published addresses, retained values and subsequent recurrence. Runtime construction rejects an array memory output with no initializer. All 19 selected headless Release tests passed. The fixture uses the experimental compiler entry point.

**AV07b evidence:** `VectorDelay` is available in the public C++ API. `s7core_vector_delay_smoke` checks a whole-array feedback recurrence, rejects a direct-only cycle, and verifies that rollback preserves the staged source occurrence. Two occurrences use the same upstream buffer with different sequence numbers, and a new epoch can reuse a sequence. The initializer remains readable across the switch to the upstream pool, and retention charged to the wrong pool is refused. All 20 selected headless Release tests passed. Registry-backed diagrams follow the schema and persistence work in AV08/AV09.

### AV08 — Nested schema and persistence round-trip

**Depends:** AV07.

**Scope:** expose array ports through nested schema boundaries and flattened bindings. Round-trip saved array and scalar diagrams in the current format and verify ordinary scalar exposed-port behavior. Keep array shape negotiation local.

**Accept:** direct and nested graph numerical equivalence, array pass-through and delay through exposed ports, save/open/save equivalence and negative mismatch tests, scalar nested/flattening tests.

**AV08a evidence:** Two levels of exposed array ports compile to the same producer slot. The headless fixture compares nested delay feedback to the inlined recurrence and verifies direct forwarding, buffer identity, source occurrence and committed position through output aliases. It rejects invalid type and shape, a nested direct-only cycle, scalar reads of an array alias and standalone array schema execution. All 22 selected headless Release tests passed, including scalar nested schema regressions.

**AV08b evidence:** The Qt offscreen Release test saves a diagram with mixed scalar and shaped exposed ports, reopens it from disk, and compares its second save. The runtime port parser verifies the descriptor and alias endpoints. Studio rejects malformed rank and unsupported format/layout declarations on open. This verifies persistence for the current compact CPU profile. Registry-backed execution is covered by AV09b.

### AV09 — Minimal new vector element library + deterministic diagrams

`PackVector` assembles N scalar inputs of one numeric type into a rank-one vector of length N. `UnpackVector` writes one scalar output per vector component. N is fixed at compile time and at least two. Each element runs once per domain evaluation and preserves component order by port index. Inputs and outputs require exact numeric types and shape. Packing writes into an execution-reserved buffer and leaves source provenance absent for independent scalar inputs. Unpacking writes native scalar values. Verify both directions with distinct component values and negative tests for mismatched types, lengths and rates.

**AV09a evidence:** The internal source, forwarder and reader used the same sealed buffer on each of 20,002 attempts, including one injected late fault. The source wrote 640,064 bytes and the two wire readers borrowed 1,280,128 bytes with zero bytes materialized on the wires. The independent sum oracle matched every committed step. The ELF allocation counter detected setup allocations and reported zero C++ allocations on the stepping thread over 20,000 successful warmed steps. The selected 23 headless Release tests passed.

**AV09b progress:** The C++ `PackVectorT` and `UnpackVectorT` steps round-trip distinct F32 and C32 components in port order. Tests reject insufficient components or reserve, incompatible types and lengths, and conflicting scalar rates. The selected 24 headless Release tests passed. Registration and execution from saved diagrams remain open.

The local registry and runtime now load and execute the vector source, forwarder, gain and adapters. The saved `schema_vector_adapters.json` example ran ten steps through `s7runtime`. The elementwise oracle checks all three gained components and the separate U64 count output. Concrete descriptors come from each instance's checked parameters. Distributed placement continues to reject array wires. All 24 selected headless tests passed after registration.

**Depends:** AV08.

**Scope:** register **new** VectorConstant/fixture source, VectorPassThrough and VectorGain with concrete shapes and CPU lease-backed outputs; a deterministic U8 array source may be started here but video metadata/layout and full frames are AV12. Provide a small canonical saved diagram running via the actual headless production runtime. No replacement of scalar Constant/Gain.

**Accept:** independent elementwise oracle and exact output count/ordering, fan-out identity, no steady-state heap allocation in the declared fixed test profile, malformed vector size failure, required Studio type label metadata ready for AV14. Do not add general broadcasting.

### AV10 — Full vector FFT numerical transform

**Depends:** AV09.

**Scope:** new whole-array FFT (not a rewrite of scalar WindowedFFT), minimally C32[N] input/output for a fixed tested transform size. Reuse existing backend/code only after checking normalization, licensing/SDK and build linkage; choose a deterministic supported size/profile and document unsupported lengths. Do not hard-wire Qt or external hardware into the graph transform.

**Accept:** independently generated impulse, DC, single complex tone and two-tone fixtures; complex bin order and inverse convention documented; known-output array checked against independent DFT/FFT oracle; allocation/copy instrumentation. Keep plugin SDK boundary qualified if output lives in a plugin.

**AV10a backend audit**

| Existing path | Runtime use |
| --- | --- |
| `s7spectral::Transform` in `analysis.hpp` | Reuse its in-place radix-2 algorithm and precomputed roots. Generalize the component type and preserve the existing C64 analysis entry point. |
| `s7spectral::WindowedFFT` | Retain the FFTW real-input streaming peak analysis. Its one-sided peak values and window normalization have a separate contract. |
| `s7core` overlap-save convolution | Keep the convolution kernel spectrum, overlap history and work arrays with convolution. Its padded stream processing needs a different lifetime from a whole-array transform. |
| Studio's FFTW wrapper | Keep its C64 scratch, planner and allocating return value in Studio analysis. |

Use fixed lengths from 2 through 1,048,576, powers of two only. A C32 transform keeps N/2 roots and a bounded output pool. At the largest length, two output slots and the root table account for 20 MiB of numeric storage. The baseline writes N C32 values into the output candidate, transforms them in place, and forwards the corresponding source occurrence. No wider scratch buffer or FFTW planner is required. The vector transform builds and loads through the installed SDK as part of the spectral plugin. The plugin already links FFTW for `WindowedFFT`.

The direct long-double DFT oracle lives in `s7spectral/tests/direct_dft.hpp`. For the C32 fixture, compare absolute forward error to `8 × ε × log2(N) × max(1, sum(abs(x)))`, with ε the C32 component epsilon. Compare inverse reconstruction to `16 × ε × log2(N) × max(1, max(abs(x)))`. Use small lengths for the independent oracle and 256, 1,024, 4,096 and 65,536 for full-step performance comparisons. Report the input initialization separately from wire materialization.

### AV11 — Vector power / PSD and spectral contract

**Depends:** AV10.

**Scope:** new vector magnitude/power or PSD element(s) with explicit window/normalization/sample-rate/frequency indexing. Specify whether bins are FFT-shifted or not; avoid introducing hidden sample rate inference. Expose traceable block/sequence information for presentation and data-quality gaps if source provides it.

**Accept:** independent PSD and frequency-axis checks using known IQ blocks, zero-input and near-zero floor behavior, shape validation, no magic dB epsilon without documentation, discontinuity policies. Produce Waterfall's reference array input schema.

### AV12 — Deterministic U8 whole-frame source (rank 2/3)

**Depends:** AV09b + D4/D6. Independent of AV10/AV11/AV15; may proceed on its own branch after a stable runtime.

**Scope:** separate new frame fixture source producing known grayscale [H,W] and RGB [H,W,3] arrays through real graph ports with deterministic frame identity. Support compact CPU layout first; specify any padded row-stride fixture and ensure validation matches actual layout. Define presentation-relevant frame identity separately from model clock and media timestamps. No camera, codec or FFmpeg/GStreamer.

**Accept:** pixel oracle (checkerboard/moving bar), width/height/type metadata, nontrivial row stride tested if supported by chosen first layout, sequential frame IDs, bounded allocation, unsupported format and invalid extent tests. Saved diagram must execute headlessly.

### AV13 — Read-only array Observation API, bounded delivery

**Depends:** AV06b + D3. AV13a headless observation can start before public AV09; AV13b Studio mailbox follows AV13a, without needing FFT or Waterfall.

**Scope:** add lease-backed committed array observations to \`s7observe\`; adapt Studio's stepping-thread-to-GUI transfer using a **new** bounded frame/array mailbox or compatible source. Do **not** copy a vector into \`ObservationSample::storage[32]\` or \`ObservationSource::Sample::double\`. Keep scalar observation and Scope unchanged. A consumer may skip presentation with visible counters, never silently drop authoritative graph values.

**Accept:** selected output snapshot coherence, retention after producer advances, slow subscriber/pool exhaustion, attach/detach, explicit gaps and counters, thread safety, error cancellation; measured payload copies for local observation, scalar \`s7observe\` regressions.

### AV14 — Studio metadata, wiring and observer registration

**Depends:** AV14a on AV03/AV08b; AV14b on AV14a + AV13b. Observer pin checking must not wait for rendering.

**Scope:** show correct array shape on pins/wire labels, validate incompatible drag-connections, register Waterfall/Video Player viewer types and editor metadata, keep saved observer bindings/settings. Provide a minimal no-op or test presentation adapter only; full renderers belong to AV15/AV16. Avoid modifying \`external/\` libraries.

**Accept:** saved diagram round-trip with array wire label, type/shape error UI, registry discovery, attach/rebind lifecycle, invalid selection errors and scalar Studio regressions. Screenshots explicitly reviewed if GUI toolchain is available.

### AV15 — Waterfall viewer rendering (G4/W)

**Depends:** AV11, AV13, AV14.

**Scope:** consume committed PSD or magnitude arrays, bounded time-frequency history, documented frequency order and timestamps/row sequence; min/max and dB range controls, palette, readable frequency/time axes, explicit gaps and pause/freeze presentation only. No computational FFT in UI when the functional diagram already provides PSD; if view-local FFT is offered later it remains an observation processor.

**Accept:** canonical saved IQ -> FFT -> PSD -> Waterfall diagram; numeric oracle and deterministic image/axis screenshots checked against reference, 1-step vs block-rate metadata consistency, skipped-row/pause/close/reopen/fast producer behavior, unchanged graph output on viewer attach/detach.

### AV16 — Basic Video Player viewer rendering (G4/V)

**Depends:** AV12, AV13, AV14.

**Scope:** consume decoded/generated whole U8 grayscale/RGB frame arrays, fit-to-window/aspect, presentation-only play/pause (no change to model), frame step/sequence/geometry/format status, bounded newest-frame policy and visible presentation-skip counters. No webcam, media-file decoding, arbitrary seek or FFmpeg/GStreamer requirement.

**Accept:** checkerboard/moving-bar saved diagram; pixel exactness (including tested stride), aspect/fit, pause/resume and late frames, same graph outputs with/without viewer, close/reopen/rebind, unsupported format/error, memory bound. Check actual rendered image screenshots, not only widget creation.

### AV17 — PDF/print, installation and documentation of new viewers

**Depends:** AV15/AV16.

**Scope:** G4/P P08 and P09 coherent immutable snapshot printing to PDF/printer, including correct Waterfall axes/legend and Video Player pixel/frame identity. Add viewer help, examples and shipping resources to supported installed packages; preserve existing printing cases. A platform without a printer still supports PDF.

**Accept:** P08/P09 tests plus existing P01–P07, source SHA, screenshot or PDF visual inspection, canceled/error print, no step advancement from printing, proper installed/relocated viewer registry discovery. Do not claim real printer evidence from CI or Wine.

### AV18 — Final dedicated array qualification / release evidence

**Depends:** AV00–AV17 merged, with clean candidate.

**Scope:** run complete relevant headless, Studio, plugin SDK, nested schema, package and performance suites on a named candidate; independent vector FFT/PSD and video pixel oracles, resource-pressure/failure injection, scalar benchmark comparisons and supported platform matrix. Verify **no mandatory 0.2.0 gate** was silently replaced by a synthetic smoke case. Update authoritative [0.2.0 work plan](RELEASE_0.2.0_WORKPLAN.md) with reproducible actual evidence or blockers.

**Accept:** a candidate-specific evidence ledger and explicit pass/fail/skip decision for G1/V, G4/W, G4/V and P08/P09; no claim of full G1–G4 closure unless all other independent release gates are qualified. Use the required AV18a/b/c/d candidate-specific matrices above; never invent completed results, and do not conflate their results with other G1–G4 release work.

## Per-session sizing and split policy

Each **unsplit AVxx or suffixed AVxxa/AVxxb/AVxxc/AVxxd** is one Codex allocation capped at five hours (not guaranteed to fit). The binding task table above overrides a broad card below when a card would exceed one session. High-risk cards **AV05–AV08, AV13–AV16** are mandatory split candidates: as soon as an implementation reveals independent subproblems, make \`AV05a\`/ \`AV05b\` etc., each with one testable result. For GUI work, separate headless data delivery from actual renderer/image verification; do not submit "viewer working" based only on successful compilation.

Sessions should **not** attempt both a core API migration and a new feature UI in the same allocation. Prefer two smaller passing PRs to one large unreviewed patch. Reserve the last part of the session for tests, diff review and handoff. The user may reorder cards only after resolving their dependencies and updating this document.

## Codex invocation template

Use the same short prompt for **one** named card, filling only the task ID and branch:

~~~text
Implement only AVxx from s7l2/docs/ARRAY_VECTOR_CODEX_WORKPLAN.md.
Read AGENTS.md, s7l2/AGENTS.md, the linked migration inventory, array design
notes and current source/tests first. Work on branch feature/array-AVxx
(or an agreed existing task branch), based on current main.

Keep scalar element classes and scalar fast path unchanged. Use only new
vector elements, with local typed/shape-checked CPU array ports and explicit
leases. Preserve causal step/publication/rollback semantics and observer
independence; do not touch network array protocols, codecs or hardware.

One Codex session has a hard five-hour budget including verification and
handoff. If the card proves too big, stop at a coherent, tested boundary,
record remaining subtasks, and propose AVxxa/AVxxb. Do not overclaim success.
Add focused positive/negative tests and execute the relevant existing suite
with exact commands. Provide the required session handoff record, commit
verified changes to the task branch, and open a reviewable PR; do not merge
or mark release gates passed merely because the task is complete.
~~~

## Merge and release order

- **G1/V foundation:** AV00–AV09, plus AV12 frame fixture and AV13a/b array observation. Do not claim G1/V until array-causality, nested/persistence, fan-out, real saved diagrams and regression results pass.
- **G4/W:** AV10a/b, AV11a/b + AV13a/b, AV14a/b, AV15a/b. Full-bin vector FFT must precede Waterfall qualification.
- **G4/V:** AV12 + AV13a/b, AV14a/b, AV16a/b. A synthetic frame view is a legitimate 0.2.0 viewer test, **not** an H5 production VideoSource.
- **G4/P:** AV17a/b covers new viewers P08/P09 only; the independent P01–P07 implementation/qualification remains mandatory. P08/P09 are part of the mandatory printing matrix.
- **Release:** AV18 candidate evidence, **plus all independently required** 0.2.0 gates from the live work plan, not just this array workstream.

## Deferred backlog after 0.2.0

New, independent vector element families can be delivered incrementally: pointwise add/multiply/convert/trigonometry, block FIR/IIR/convolution, block SDR/audio device boundaries, and 2D transforms/video processing. 0.3.0 retains production GStreamer audio/video and its own independent hardware and replay qualification. More advanced shapes, GPU/DMA-BUF, remote array transport, multi-plane video and spatial field numerics need separate reviewed contracts.

Do not let the five-hour Codex allocation turn into a reason to compromise causality, data ownership or error handling. A smaller verified slice is the correct outcome of an overlong task.
