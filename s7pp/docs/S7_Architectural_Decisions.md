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
- natural mapping to processes and nodes.

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

`cpp id="m0ql3m" tick()`

implies a global clock.

S7 intentionally avoids making global time fundamental.

------------------------------------------------------------------------

## Decision

The preferred evaluation method name becomes:

`cpp id="e2af7l" step()`

------------------------------------------------------------------------

## Rationale

A step represents a local causal transition:

`text id="4hqt7h" (State, Inputs)         → (State', Outputs)`

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

`text id="qkg4y5" Adder   ↓ Delay   ↓ Adder`

Invalid:

`text id="u7f9hg" Adder   ↓ Gain   ↓ Adder`

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
- remote operation.

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

# Summary

The current S7 architecture is based on the following accepted principles:

\`\`\`text id=“j12h0v” Causality precedes time.

Coherence is contextual.

Schema is Element.

Schema is the unit of deployment.

State belongs only to Element instances.

Connections define causality.

Cycles require causal memory.

Observation is independent.

Deployment is independent.

Network boundaries are explicit.

Headless execution is primary. \`\`\`

These decisions form the architectural foundation of S7 2.0 and should be revisited only with strong justification.
