# ARCHITECTURAL_DECISIONS.md

# S7 Architectural Decisions

## Purpose

This document records major architectural decisions made during the design and evolution of S7.

The purpose of this document is:

- to preserve design rationale;
- to avoid repeating past discussions;
- to explain why specific architectural choices were made;
- to provide guidance for future development.

This document follows an ADR-like (Architectural Decision Record) style.

------------------------------------------------------------------------

# ADR-001

## Title

Causality Is The Primary Organizing Principle

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Many computational systems are organized around:

- time
- events
- processes
- scheduling

During development it became clear that these concepts are secondary.

The true semantic structure of a system is determined by causal relationships.

------------------------------------------------------------------------

## Decision

S7 adopts causality as its primary organizing principle.

Connections define causality.

Time is not fundamental to the computational model.

------------------------------------------------------------------------

## Consequences

Positive:

- simpler formal model;
- supports distributed execution;
- avoids dependence on global clocks.

Negative:

- requires careful reasoning about coherence;
- differs from many conventional frameworks.

------------------------------------------------------------------------

# ADR-002

## Title

Coherence Domains Are First-Class Concepts

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Many target applications involve:

- SDR
- phased arrays
- acoustic arrays
- synchronized sensors

Global time is often insufficient for describing relationships between signals.

------------------------------------------------------------------------

## Decision

Introduce Coherence Domains as an explicit architectural concept.

Coherence Domains define contexts in which phase and temporal relationships may be established.

------------------------------------------------------------------------

## Consequences

Positive:

- supports SDR applications naturally;
- supports distributed sensing;
- aligns with physical systems.

Negative:

- introduces additional conceptual complexity.

------------------------------------------------------------------------

# ADR-003

## Title

Schema Is Element

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Hierarchical composition is required.

Special treatment of Schemas would complicate the model.

------------------------------------------------------------------------

## Decision

A Schema is a composite Element.

Schemas may contain:

- Elements
- Schemas

without limitation.

------------------------------------------------------------------------

## Consequences

Positive:

- recursive composition;
- uniform model;
- simplified tooling.

Negative:

- recursive validation becomes necessary.

------------------------------------------------------------------------

# ADR-004

## Title

Schema Is The Unit Of Deployment

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Deployment metadata often applies to groups of Elements rather than individual Elements.

In this context, node assignment means assignment to runtime instances (deployment nodes), not directly to physical hosts.

Multiple runtime instances may be deployed on a single host.

Examples:

- node assignment;
- execution constraints;
- resource requirements.

------------------------------------------------------------------------

## Decision

The smallest deployable architectural unit is a Schema.

------------------------------------------------------------------------

## Consequences

Positive:

- simpler deployment model;
- natural mapping to processes and nodes;
- supports multi-instance deployment on one host for isolation and scaling.

Negative:

- fine-grained deployment requires schema decomposition.

------------------------------------------------------------------------

# ADR-005

## Title

State Belongs Only To Element Instances

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Discussion revealed ambiguity regarding Schema State.

The concept introduced unnecessary complexity.

------------------------------------------------------------------------

## Decision

State belongs exclusively to Element Instances.

Schemas define structure.

Schemas do not possess state.

------------------------------------------------------------------------

## Consequences

Positive:

- cleaner formal model;
- simpler runtime architecture;
- clearer ownership rules.

Negative:

- snapshots must be represented as collections of Element states.

------------------------------------------------------------------------

# ADR-006

## Title

step() Replaces tick()

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

The term:

`tick()`

implies a global clock.

S7 intentionally avoids making global time fundamental.

------------------------------------------------------------------------

## Decision

The preferred evaluation method name becomes:

`step()`

------------------------------------------------------------------------

## Rationale

A step represents a local causal transition:

`(State, Inputs) → (State', Outputs)`

------------------------------------------------------------------------

## Consequences

Positive:

- avoids implicit time semantics;
- aligns with Formal Model.

Negative:

- requires migration of legacy code.

------------------------------------------------------------------------

# ADR-007

## Title

Connections Define Causality

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

The role of Scheduler was examined.

A distinction emerged between:

- semantic causality;
- execution strategy.

------------------------------------------------------------------------

## Decision

Connections define causality.

Schedulers enforce causality.

Schedulers do not create causality.

------------------------------------------------------------------------

## Consequences

Positive:

- execution strategy becomes replaceable;
- semantics remain independent of implementation.

------------------------------------------------------------------------

# ADR-008

## Title

Cycles Require Causal Memory

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Feedback loops are required for dynamic systems.

Unrestricted cycles lead to algebraic loops.

------------------------------------------------------------------------

## Decision

A cycle is valid only if it contains at least one Causal Memory Element.

Examples:

Valid:

`Adder → Delay → Adder`

Invalid:

`Adder → Gain → Adder`

------------------------------------------------------------------------

## Consequences

Positive:

- well-defined execution semantics;
- predictable behavior.

Negative:

- cycle validation required.

------------------------------------------------------------------------

# ADR-009

## Title

Schema Validation Precedes Execution

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Invalid graphs should never reach Runtime execution.

------------------------------------------------------------------------

## Decision

Every Schema must pass validation before compilation and execution.

Validation includes:

- type checking;
- connection checking;
- cycle checking;
- metadata checking.

------------------------------------------------------------------------

## Consequences

Positive:

- simpler Runtime;
- earlier error detection.

Negative:

- additional compilation stage required.

------------------------------------------------------------------------

# ADR-010

## Title

Executable Graph Is A Runtime Artifact

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Runtime execution requires a representation optimized for scheduling and traversal.

------------------------------------------------------------------------

## Decision

Schema Definitions are compiled into Executable Graphs.

Executable Graphs are Runtime artifacts.

They are not part of the Formal Model.

------------------------------------------------------------------------

## Consequences

Positive:

- runtime optimization opportunities;
- implementation flexibility.

------------------------------------------------------------------------

# ADR-011

## Title

Observation Is Independent From Computation

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Previous architectures embedded observation elements into computational graphs.

This created semantic coupling.

------------------------------------------------------------------------

## Decision

Observation becomes an independent subsystem.

Observation consumes information but never participates in computation.

------------------------------------------------------------------------

## Consequences

Positive:

- cleaner architecture;
- dynamic observation;
- easier tooling.

Negative:

- separate Observation infrastructure required.

------------------------------------------------------------------------

# ADR-012

## Title

Probe Is Not A Functional Graph Element

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Probes were originally considered computational elements.

Further analysis showed they belong to observation infrastructure.

------------------------------------------------------------------------

## Decision

Probes are Observation concepts.

They are not Elements.

They are not part of the Functional Graph.

------------------------------------------------------------------------

## Consequences

Positive:

- preserves semantic purity;
- supports dynamic attachment.

------------------------------------------------------------------------

# ADR-013

## Title

Observation Is Read-Only

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Observation systems must not affect execution semantics.

------------------------------------------------------------------------

## Decision

Observation may:

- inspect;
- record;
- stream.

Observation may not:

- modify state;
- modify causality;
- modify scheduling.

------------------------------------------------------------------------

## Consequences

Positive:

- reproducibility;
- predictable behavior.

------------------------------------------------------------------------

# ADR-014

## Title

Headless First Runtime

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Target systems include:

- SDR devices;
- embedded Linux systems;
- remote nodes;
- distributed deployments.

GUI availability cannot be assumed.

------------------------------------------------------------------------

## Decision

All Runtime functionality must be available without Studio.

Studio becomes a Runtime client.

------------------------------------------------------------------------

## Consequences

Positive:

- embedded deployment;
- automation;
- remote operation;
- allows multiple headless runtime instances on one host.

------------------------------------------------------------------------

# ADR-015

## Title

Explicit Network Boundaries

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Distributed systems often hide communication details.

This complicates reasoning about behavior.

------------------------------------------------------------------------

## Decision

Network communication remains visible.

NetSource and NetSink are explicit Functional Graph Elements.

------------------------------------------------------------------------

## Consequences

Positive:

- easier debugging;
- clearer semantics;
- predictable deployment behavior.

------------------------------------------------------------------------

# ADR-016

## Title

Deployment Is Independent From Computation

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

Functional semantics should not depend on execution location.

------------------------------------------------------------------------

## Decision

Deployment is represented separately from the Functional Graph.

Deployment affects location, not meaning.

------------------------------------------------------------------------

## Consequences

Positive:

- portability;
- easier scaling;
- cleaner architecture.

------------------------------------------------------------------------

# ADR-017

## Title

Metadata-Driven Architecture

------------------------------------------------------------------------

## Status

Proposed

------------------------------------------------------------------------

## Context

Future Runtime, Studio and Observation systems require structured information about Elements.

Modern C++ offers:

- templates;
- constexpr;
- attributes;
- future reflection capabilities.

------------------------------------------------------------------------

## Decision

S7 should evolve toward a metadata-driven architecture.

The exact implementation mechanism remains open.

Possible implementations:

- templates;
- macros;
- generated metadata;
- C++ reflection.

------------------------------------------------------------------------

## Consequences

Positive:

- simpler Element development;
- powerful tooling;
- reduced boilerplate.

Negative:

- metadata model remains to be designed.

------------------------------------------------------------------------

# ADR-018

## Title

No Dedicated DSL At Current Stage

------------------------------------------------------------------------

## Status

Accepted

------------------------------------------------------------------------

## Context

A dedicated language could eventually describe Elements and Schemas.

Such development would significantly increase project complexity.

------------------------------------------------------------------------

## Decision

S7 will use modern C++ as its implementation language.

No dedicated DSL will be introduced at this stage.

------------------------------------------------------------------------

## Consequences

Positive:

- focus remains on Runtime architecture;
- lower maintenance burden;
- faster progress.

Negative:

- some abstractions remain tied to C++.

------------------------------------------------------------------------

# ADR-019

## Title

Layered Licensing: LGPL Runtime, GPL Applications

------------------------------------------------------------------------

## Status

Accepted (2026-10-21, by the maintainer)

------------------------------------------------------------------------

## Context

S7 was distributed under GPL version 2 only. Studio uses Qt Graphs and
Qt Quick 3D, which are available only under GPL version 3 or a commercial
license, so GPL-2.0-only Studio binaries could not be distributed compliantly.

Element plugins are loaded into the runtime and Studio processes and link
`s7core`. Plugins of hardware or instrument vendors may not be free software,
and applications may want to embed the headless runtime.

Documentation, example models and protocol fixtures are copied into other
people's work and other implementations.

------------------------------------------------------------------------

## Decision

- The runtime and the plugin SDK (`s7core`, `s7io`, `s7observe`, `s7net`,
  `s7transport`, `s7runtime`, `s7noise`) are LGPL-3.0-or-later.
- Studio and `s7ctl` are GPL-3.0-or-later with an additional permission under
  GPLv3 section 7 to be combined with plugins that use only the plugin
  interface.
- Other code (tests, tools, `s7py`) is GPL-3.0-or-later.
- Documentation is CC-BY-4.0; example models and conformance fixtures are
  CC0-1.0; the reference plugin `s7spectral` is MIT-0.
- Every file states its license in SPDX form, checked with REUSE.

`LICENSING.md` at the repository root is the authoritative description.

------------------------------------------------------------------------

## Consequences

Positive:

- Studio binaries can be distributed with Qt Graphs and Qt Quick 3D;
- plugins and applications embedding the runtime may use any license;
- protocol specifications, examples and fixtures can be reused freely.

Negative:

- the LGPL runtime must not depend on GPL-only code, including Qt modules
  that are GPL-only;
- moving code between the runtime and Studio can change its license.

------------------------------------------------------------------------

# ADR-020

## Title

Independent Control and Data Listeners with Explicit Data Security Profiles

## Status

Accepted 2026-10-23 by the maintainer; required before the 0.1.0 preview.

## Context

Headless implementations in Python and other languages should be able to
implement authenticated control independently of bulk data. Loopback and
explicitly trusted LAN deployments on constrained devices need a data path
without mandatory payload encryption. No Raspberry Pi performance result is
claimed; throughput and CPU cost must be measured on the target device.

## Decision

Support independent `/control` and `/data` listeners within one runtime and
transport. In split mode control requires mTLS client certificates and retains
certificate role/pinned-fingerprint mutation authorization. A control client
opens no implicit data connection. Shared listeners remain available.

Data defaults to `mtls`. An operator may explicitly select `trusted_plaintext`
for loopback or a trusted LAN. Both local node policy and committed deployment
endpoints declare that choice. Plaintext data uses claimed node UUIDs and
relies on network trust; control authentication does not authenticate that
separate socket. It provides no cryptographic confidentiality, packet integrity
or proof of peer identity. Do not add per-packet cryptography to this mode or
automatically fall back from an encrypted connection.

Listener advertisement and configuration are language-neutral. The binary
wire format, explicit graph boundaries, per-stream sequencing, ACK/RESUME,
backpressure and restart-fault semantics remain unchanged. Local observation
continues to use memory. Networked observation APIs are separate work.

## Consequences

Implementations can keep control on mTLS while choosing encrypted or trusted
plaintext data. A plaintext data listener never accepts control operations.
Deployments must describe separate endpoints and their security profiles;
applications must display plaintext as a network-trust policy rather than
certificate-authenticated data. See the [configuration and interoperability
guide](../s7l2/docs/SPLIT_TRANSPORT.md).

------------------------------------------------------------------------

# ADR-021

## Title

All-or-Nothing Publication of a Faulting Runtime Step

## Status

Accepted 2026-10-31 by the maintainer.

## Context

`NodeRuntime::step()` already restored every published output when a
direct-feedthrough element or a coupled trapezoid trial threw. A fault in a
causal-memory element restored only that element's outputs. The direct outputs
of the failed step and the memory outputs published at its start stayed
visible while the step count did not advance. The values staged by the memory
elements that had already run were then discarded, so the next step repeated
one step's state instead of continuing it.

## Decision

A step that throws in any phase publishes nothing. Direct outputs and the
memory outputs published at the start of the step return to their pre-step
values. The step count does not advance. The values staged by the previous
successful step are kept and published by the next successful step. Memory
elements stage into a second buffer that becomes current only after every
memory element and coupled trial has succeeded.

The guarantee covers published outputs and staged values only. Private element
state (for example a filter's delay line or a nested schema's runtime) and
external side effects that advanced before the fault are not rolled back.

## Consequences

After a fault, observers see the last completed step, and a retried step
continues exactly where the failed one began for elements whose state is their
published output. Each causal-memory output carries one more staging buffer;
no extra copy is made per step. `s7core_step_rollback_smoke` checks faults in
direct elements and in the first and last memory elements, on the first step
and on consecutive steps, against an unfaulted run.

------------------------------------------------------------------------

# ADR-022

## Title

One Model-Time Rate per Run, Classified Element Rates and Explicit Resampling

## Status

Accepted 2026-10-31 by the maintainer. Implemented 2026-10-31 except the items listed as research; see the [sample rate model, section 8](S7_Sample_Rate_Model.md#8-model-time-and-element-rate-classification-2026-10-31).

## Context

Model time was set in three ways that did not agree. Studio overwrote every element's `dt` and `sample_rate_hz` with the run's step before each run. Deployments rounded `dt` to whole nanoseconds, which cannot represent 44.1 kHz and conflicted with elements declaring it exactly. Setting model time checked only elements that declared a rate, not rates seeded by the schema. Durations were stored as sample counts, and filter coefficients silently changed meaning at a new rate.

## Decision

The run's step is the single source of truth for model time. It is an exact rational rate end to end; nanoseconds are only rounded once, from absolute time, for reporting and real-time pacing. Every element's resolved rate must equal the rate of its domain, and the main domain runs at the model-time rate. A mismatch stops the run and suggests a `Resampler`.

Elements are classified by what their rate means. Rate-following elements take their domain's rate. Rate-authoritative elements (files, recordings, streams, hardware) keep theirs and are checked. Rate-designed elements record their design rate and recalculate their parameters at compile time where their design intent allows, otherwise they refuse to run. Durations are always in time units. Documents are not rewritten when the run rate changes.

A different rate requires an explicit `Resampler`, which S7 never inserts. It passes values through at equal rates, interpolates causally with order 0 (default), 1 or 2, and filters against aliasing when it lowers the rate. It splits the graph into rate domains that step on an exact rational schedule. Feedback across rate domains is rejected until it has been researched.

## Consequences

A run either has one consistent model time or does not start. Changing `dt` in the run dialog never edits the design. A file, stream or device at another rate needs a visible resampling step, with a documented latency. Existing documents keep loading: stored sample counts are converted using the rate they were written at, and stored `dt`/`sample_rate_hz` values of rate-following elements become legacy fields. Rate-following elements built in C++ with an explicit rate remain pins for now; the 0.2.0 work plan includes reviewing such tests. Hardware clock domains, drift and networked audio are 0.3.0 research. In a graph with several rate domains, ADR-021's all-or-nothing rule holds per domain step: a fault partway through a run step leaves domains that already completed that step advanced, although a retry continues exactly.

------------------------------------------------------------------------

# ADR-023

## Title

SDR Sample Formats: CU8 Converts Normalized Only, 12-Bit I/Q Is CI12, Types Added to the Wire Catalog Are Advertised

## Status

Accepted 2026-10-31 by the maintainer. Implemented 2026-10-31.

## Context

S7's integer and complex integer types exist for SDR I/Q. The common devices deliver: RTL-SDR unsigned 8-bit offset binary whose measured zero is near 127.4, not 128; HackRF signed 8-bit; ADALM-Pluto (AD9361/AD9363), LimeSDR, USRP sc12 and Airspy signed 12-bit, either packed (3 bytes per I/Q pair) or in 16-bit words aligned low (Pluto receive) or high (Pluto transmit). `Convert` in value mode treated CU8 codes as plain unsigned numbers and clipped half their range into signed types. S7 had no complex 12-bit type, so 12-bit I/Q travelled as CI16 at 4/3 of its native width, and normalized CI16 read low-aligned 12-bit data at 1/16 of full scale. The wire protocol required every implementation to transport every listed type, so a new type could not be added without breaking existing nodes.

## Decision

`Convert` refuses value mode to or from CU8: an offset-binary code has no plain numeric value. Normalized CU8 is (c − 128)/128 by convention; DC is removed explicitly in the graph. Signed↔unsigned normalized conversions are refused when the element is created.

CI12 (`0x0207`) is a pair of I12, stored as two 16-bit integers restricted to the 12-bit range and packed on the wire like I12 (in-phase then quadrature, 3 bytes per element). An SDR element publishes samples at its converter's native width (CU8, CI8, CI12, CI16); 12-bit samples are never published as CI16, so normalized conversion gives full scale on every device and CI12 → CI16 normalized gives the high-aligned transmit format.

Every node transports the original v1 catalog (wire ids up to `0x0206`). `NODE_INFO.data_protocol.signal_types` lists every type a node transports; a node that lists none has the original catalog. The deployment client refuses a stream whose type an end does not list, before anything is prepared. Later types are added the same way, without a new packet version.

## Consequences

Diagrams that converted CU8 in value mode, or signed↔unsigned in normalized mode, are now validation errors instead of producing wrong values or failing at the first step; no example in the repository did either. CI12 streams run only between nodes of this version or later, and an older node is refused with a message naming the stream, type and node. CI12 has the same arithmetic as the other complex integer types, saturating or wrapping at 12 bits. The catalog has 27 types, which adds template instantiations and compile time.

------------------------------------------------------------------------

# Summary

The current S7 architecture is based on the following accepted principles:

- Causality precedes time.
- Coherence is contextual.
- Schema is Element.
- Schema is the unit of deployment.
- State belongs only to Element instances.
- Connections define causality.
- Cycles require causal memory.
- One model-time rate per run; rates change only through an explicit Resampler.
- SDR samples keep their native format; types added to the wire catalog are advertised by each node.
- Observation is independent.
- Deployment is independent.
- Network boundaries are explicit.
- Headless execution is primary.

These decisions form the architectural foundation of S7 2.0 and should be revisited only with strong justification.
