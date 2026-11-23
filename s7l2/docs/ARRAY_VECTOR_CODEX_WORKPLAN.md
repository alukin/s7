# S7 0.2.0 vector signals and observers — bounded Codex session plan

**Status: implementation plan, 2026-11-23. No implementation, test pass or release gate closure is claimed by this plan.**
Approved direction: **preserve scalar element classes and their fast bindings**. Introduce new vector elements that share a small, reusable array/lease/port substrate. The 0.2.0 slice includes local vector wires, FFT/PSD array output, a read-only Waterfall and a simple Video Player with synthetic/recorded whole-frame input. Production FFmpeg/GStreamer/camera, network tensor transport, 2D FFT and spatial solvers are out of scope.

Companion documents:
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
Scalar compatibility / measured allocations or copies:
GUI screenshot review (if applicable; actual paths/evidence):
Blocked items, unsupported profiles, remaining risks:
Next AVxx dependency:
~~~

A standalone implementation session is **not complete** if its promised tests are not exercised. A timeboxed partial result should be honestly marked WIP and handed off; it is not a successful release acceptance.

## Dependency map

~~~text
AV00 baseline/invariants
   |
AV01 shape + descriptors
   |
AV02 metadata + schema declarations
   |
AV03 shape inference + connection validation
   |
AV04 CPU views, leases and bounded pools
   |
AV05 array slot, Element bindings and compiled direct pipeline
   |
AV06 handle-safe direct publication/rollback
   |
AV07 vector causal-memory staging/Delay
   |
AV08 nested schemas + serialization/round-trip
   |
AV09 minimal vector elements and fixture diagrams
   +-----------+
   |           |
AV10 FFT      AV13 observation lease source
   |           |
AV11 PSD       +------ AV14 Studio routing/metadata
   |                         |
   +---------------+---------+
                   |
             AV15 Waterfall
                   |
          AV12 U8 video fixture
                   |
             AV16 Video Player
                   |
          AV17 printing/packages
                   |
          AV18 final qualification
~~~

The schematic is a **review order**, not a claim that every branch can be developed concurrently. AV12 depends on AV09 and the fixed-shape rank-2/3 contract, not mathematically on Waterfall; once the shared array observation boundary (AV13–AV14) is accepted, Waterfall and Video Player GUI implementations may proceed in separate branches. AV10/AV11 and AV13 may be prepared in parallel **only if no overlapping core/metadata files** are being edited. Timebox each AVxx separately.

## Session cards — narrow work units

### AV00 — Baseline and test contract (no production API change)

**Goal:** capture existing scalar performance/rollback/type/Studio baselines and identify the smallest legal first array shape/lease interfaces.

**Scope:** inspect \`s7core\`, \`s7observe\`, \`s7studio\`, plugin SDK, metadata generator/build rules, existing test names, CTest registration and diagram schema syntax. Produce a short concrete test matrix and API decision note (array port slot ownership, numeric type plus fixed shape, supported rank/type matrix). Run representative baseline scalar tests; capture measurement environment, without a fabricated speedup target.

**Accept:** links to actual files/tests, baseline commands/results, explicit open architectural questions, no new arrays claimed. If publication or plugin ABI needs maintainer approval, stop and ask before subsequent invasive sessions.

### AV01 — Fixed shape and view descriptors

**Depends:** AV00.

**Scope:** add a small core-only, validated rank/extents and CPU layout/view descriptor API. For 0.2.0 support rank 0 (conceptual scalar), rank 1, and rank 2/3 required for U8 image fixtures. Define byte bounds, stride/overflow/error checks. Do not modify runtime publication or existing scalar Port registration in this session.

**Accept:** unit tests for empty rank, valid extents, zero/negative/overflow extents as applicable, stride/offset bounds, contiguous vs padded rows; complete core build and selected regressions. Include a negative test proving unsafe large shape fails before allocation.

### AV02 — Port metadata and schema declaration

**Depends:** AV01.

**Scope:** extend \`PortSpec\`, metadata/registry and diagram schema as needed to declare concrete fixed-shape array ports while leaving old scalar metadata valid. Preserve current numeric \`SignalType\` IDs and explicit conversion semantics. Do not yet wire execution buffers.

**Accept:** metadata parsing/serialization/creation tests, old scalar element metadata still resolves, malformed rank/extent rejected. Avoid adding array fields to every scalar element's JSON by hand.

### AV03 — Numeric + shape inference and compile-time rejection

**Depends:** AV02.

**Scope:** extend resolver, connection validation and compiler preflight to match both numeric type and exact fixed shape. No implicit broadcast or scalar-to-vector coercion. Add explicit unsupported cases for coupled/implicit numerical trials and prohibited feedback. Keep existing scalar wiring tests intact.

**Accept:** tests connecting matching/mismatching F32/C32 and U8 shapes, scalar-to-vector mismatch, different rank, illegal cycles, required explicit failure of array numerical-trial case; old scalar diagrams still compile.

### AV04 — CPU buffer leases and bounded pool

**Depends:** AV01, AV03 for integration assumptions.

**Scope:** a headless \`s7core\` CPU lease/pool, read-only typed access, writable candidate acquisition, stable memory lifetime, bounded capacity, immutable publication contract. Resource exhaustion must be explicit. No Studio, hardware or GPU support. Design borrowed view versus retained lease so compiled synchronous fan-out has no mandatory atomic increments per wire.

**Accept:** pool lifetime, multireader retention, exclusive writes, stale/invalid lease rejection, deterministic pool exhaustion, sanitizer-ready tests and no payload copy on share; report the allocation policy. Do not infer "end-to-end zero-copy" from this unit test.

### AV05 — Array-valued Element/Port slots and compiler binding

**Depends:** AV03/AV04.

**Scope:** add a second (array) storage class under the same logical port contract; bind array inputs to stable output **slots** rather than movable payload addresses. Prove a minimal non-stateful source -> pass-through -> consumer direct-feedthrough chain using the real Compiler/NodeRuntime. Keep scalar fast path unchanged; don't add array history/feedback yet.

**Accept:** compiled matching vector pipeline, fan-out to two readers yields identical payload identity, wrong shape/type preflight fails, scalar CTest regression and a small before/after scalar dispatch benchmark or a documented baseline comparison. If full rollback is not yet present, mark the feature internal/experimental and prevent release use until AV06.

### AV06 — Direct array candidate publication and faults

**Depends:** AV05.

**Scope:** extend direct-output publication to stage/commit array **handles** atomically at the logical step boundary. Downstream direct elements see current candidates. External Observation sees last committed output. A fault after a producing element restores prior published slots and leaves retained observers intact. Do not make coupled numerical trial regions array-capable.

**Accept:** late direct-element throw, several outputs, fan-out, observers, retry, memory pool safety, stable prior published payload and no copying of array bytes during rollback; scalar ADR-021 tests remain green.

### AV07 — Causal memory, VectorDelay and feedback

**Depends:** AV06.

**Scope:** new VectorDelay (and/or explicit whole-array PreviousValue variant) using handle staging and defined startup values. No modification of scalar Delay. Ensure all causal-memory elements see the correct previous committed values, including two independent feedback states and failure in a later memory element.

**Accept:** deterministic vector recurrences, initialized memory, invalid direct-only cycles, memory-phase throw/rollback/retry, no unintended payload copy, no one-sample vs block ambiguity. Include a checked saved diagram if metadata/wiring are ready.

### AV08 — Nested schema and persistence round-trip

**Depends:** AV07.

**Scope:** expose array ports through nested schema boundaries and flattened bindings; round-trip a saved diagram/schema with fixed array shape. Preserve ordinary scalar exposed-port behavior, plugin element metadata and compatibility of old diagrams. Do not add remote array schema negotiation.

**Accept:** direct and nested graph numerical equivalence, array pass-through and delay through exposed ports, save/open/save equivalence and negative mismatch tests, scalar nested/flattening tests.

### AV09 — Minimal new vector element library + deterministic diagrams

**Depends:** AV08.

**Scope:** register **new** VectorConstant/fixture source, VectorPassThrough and VectorGain with concrete shapes and CPU lease-backed outputs; a deterministic U8 array source may be started here but video metadata/layout and full frames are AV12. Provide a small canonical saved diagram running via the actual headless production runtime. No replacement of scalar Constant/Gain.

**Accept:** independent elementwise oracle and exact output count/ordering, fan-out identity, no steady-state heap allocation in the declared fixed test profile, malformed vector size failure, required Studio type label metadata ready for AV14. Do not add general broadcasting.

### AV10 — Full vector FFT numerical transform

**Depends:** AV09.

**Scope:** new whole-array FFT (not a rewrite of scalar WindowedFFT), minimally C32[N] input/output for a fixed tested transform size. Reuse existing backend/code only after checking normalization, licensing/SDK and build linkage; choose a deterministic supported size/profile and document unsupported lengths. Do not hard-wire Qt or external hardware into the graph transform.

**Accept:** independently generated impulse, DC, single complex tone and two-tone fixtures; complex bin order and inverse convention documented; known-output array checked against independent DFT/FFT oracle; allocation/copy instrumentation. Keep plugin SDK boundary qualified if output lives in a plugin.

### AV11 — Vector power / PSD and spectral contract

**Depends:** AV10.

**Scope:** new vector magnitude/power or PSD element(s) with explicit window/normalization/sample-rate/frequency indexing. Specify whether bins are FFT-shifted or not; avoid introducing hidden sample rate inference. Expose traceable block/sequence information for presentation and data-quality gaps if source provides it.

**Accept:** independent PSD and frequency-axis checks using known IQ blocks, zero-input and near-zero floor behavior, shape validation, no magic dB epsilon without documentation, discontinuity policies. Produce Waterfall's reference array input schema.

### AV12 — Deterministic U8 whole-frame source (rank 2/3)

**Depends:** AV09, AV08. May occur before AV15 if files do not conflict.

**Scope:** separate new frame fixture source producing known grayscale [H,W] and RGB [H,W,3] arrays through real graph ports with deterministic frame identity. Support compact CPU layout first; specify any padded row-stride fixture and ensure validation matches actual layout. Define presentation-relevant frame identity separately from model clock and media timestamps. No camera, codec or FFmpeg/GStreamer.

**Accept:** pixel oracle (checkerboard/moving bar), width/height/type metadata, nontrivial row stride tested if supported by chosen first layout, sequential frame IDs, bounded allocation, unsupported format and invalid extent tests. Saved diagram must execute headlessly.

### AV13 — Read-only array Observation API, bounded delivery

**Depends:** AV06, AV09. Can proceed independently of AV10, with separate touched files.

**Scope:** add lease-backed committed array observations to \`s7observe\`; adapt Studio's stepping-thread-to-GUI transfer using a **new** bounded frame/array mailbox or compatible source. Do **not** copy a vector into \`ObservationSample::storage[32]\` or \`ObservationSource::Sample::double\`. Keep scalar observation and Scope unchanged. A consumer may skip presentation with visible counters, never silently drop authoritative graph values.

**Accept:** selected output snapshot coherence, retention after producer advances, slow subscriber/pool exhaustion, attach/detach, explicit gaps and counters, thread safety, error cancellation; measured payload copies for local observation, scalar \`s7observe\` regressions.

### AV14 — Studio metadata, wiring and observer registration

**Depends:** AV08, AV13.

**Scope:** show correct array shape on pins/wire labels, validate incompatible drag-connections, register Waterfall/Video Player viewer types and editor metadata, keep saved observer bindings/settings. Provide a minimal no-op or test presentation adapter only; full renderers belong to AV15/AV16. Avoid modifying \`external/\` libraries.

**Accept:** saved diagram round-trip with array wire label, type/shape error UI, registry discovery, attach/rebind lifecycle, invalid selection errors, legacy scalar Studio tests. Screenshots explicitly reviewed if GUI toolchain is available.

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

**Accept:** a candidate-specific evidence ledger and explicit pass/fail/skip decision for G1/V, G4/W, G4/V and P08/P09; no claim of full G1–G4 closure unless all other independent release gates are qualified. If it does not fit one session, split independent test matrices into AV18a/AV18b/…; never invent completed results.

## Per-session sizing and split policy

Each AVxx is **one intended Codex allocation, capped at five hours** (not guaranteed to fit). High-risk cards **AV05–AV08, AV13–AV16** are mandatory split candidates: as soon as an implementation reveals independent subproblems, make \`AV05a\`/ \`AV05b\` etc., each with one testable result. For GUI work, separate headless data delivery from actual renderer/image verification; do not submit "viewer working" based only on successful compilation.

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

- **G1/V foundation:** AV00–AV09, plus AV12 frame fixture and AV13 array observation. Do not claim G1/V until array-causality, nested/persistence, fan-out, real saved diagrams and regression results pass.
- **G4/W:** AV10/AV11 + AV13/AV14/AV15. Full-bin vector FFT must precede Waterfall qualification.
- **G4/V:** AV12–AV14 + AV16. A synthetic frame view is a legitimate 0.2.0 viewer test, **not** an H5 production VideoSource.
- **G4/P:** AV17. P08/P09 are part of the mandatory printing matrix.
- **Release:** AV18 candidate evidence, **plus all independently required** 0.2.0 gates from the live work plan, not just this array workstream.

## Deferred backlog after 0.2.0

New, independent vector element families can be delivered incrementally: pointwise add/multiply/convert/trigonometry, block FIR/IIR/convolution, block SDR/audio device boundaries, and 2D transforms/video processing. 0.3.0 retains production GStreamer audio/video and its own independent hardware and replay qualification. More advanced shapes, GPU/DMA-BUF, remote array transport, multi-plane video and spatial field numerics need separate reviewed contracts.

Do not let the five-hour Codex allocation turn into a reason to compromise causality, data ownership or error handling. A smaller verified slice is the correct outcome of an overlong task.
