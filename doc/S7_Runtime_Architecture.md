# S7_RUNTIME_ARCHITECTURE.md

# S7 Runtime Architecture

## Purpose

This document describes the architecture of the S7 Runtime subsystem.

The Runtime is responsible for executing Functional Graphs while preserving the semantics defined by the S7 Formal Model.

This document describes:

- Runtime responsibilities
- Runtime structure
- Schema lifecycle
- Validation
- Compilation
- Execution
- State management
- Scheduling
- Observation integration

The Runtime does not define computational semantics.

Computational semantics are defined by the Functional Graph and the Formal Model.

The Runtime exists solely to execute those semantics.

------------------------------------------------------------------------

# Architectural Principles

## Runtime Does Not Define Semantics

The Runtime does not create causality.

The Runtime does not create state.

The Runtime does not create structure.

The Runtime executes a model that already defines these concepts.

------------------------------------------------------------------------

## Scheduler Is Secondary

The Scheduler is an implementation detail.

The Scheduler must preserve Functional Graph semantics.

Different Scheduler implementations may exist.

A change of Scheduler must not alter computational meaning.

------------------------------------------------------------------------

## State Belongs To Elements

Runtime stores and manages Element Instance states.

Runtime never introduces Schema state.

Fundamental invariant:

    Schema defines structure.

    Element instances contain state.

------------------------------------------------------------------------

## Observation Is External

Observation is not part of execution.

Observation is implemented as a service attached to Runtime.

Observation must not alter computational behavior.

------------------------------------------------------------------------

# Runtime Layers

The Runtime is divided into several logical layers.

    Schema Definition
            ↓
    Validation
            ↓
    Compilation
            ↓
    Executable Graph
            ↓
    Execution
            ↓
    Observation Services

------------------------------------------------------------------------

# Runtime Components

## Schema Repository

Stores Schema definitions.

Responsibilities:

- loading schemas
- versioning
- metadata access
- dependency resolution

The repository contains definitions only.

No runtime state is stored here.

------------------------------------------------------------------------

## Validator

Responsible for validating Schemas before execution.

Validation is mandatory.

Responsibilities include:

- connection validation
- port validation
- type validation
- cycle validation
- metadata validation

------------------------------------------------------------------------

## Compiler

Transforms a validated Schema into an Executable Graph.

The Compiler does not change semantics.

The Compiler may perform:

- graph flattening
- instance expansion
- optimization
- scheduling preparation

Compiler output:

    Executable Graph

------------------------------------------------------------------------

## Scheduler

Determines execution order.

The Scheduler enforces causality already defined by the Functional Graph.

The Scheduler is not the source of causality.

Possible implementations:

- Topological Scheduler
- Parallel Scheduler
- Real-Time Scheduler
- Distributed Scheduler

------------------------------------------------------------------------

## Execution Engine

Responsible for executing Element Instances.

The Execution Engine invokes:

    step()

on Element Instances according to Scheduler decisions.

------------------------------------------------------------------------

## State Manager

Responsible for:

- state allocation
- state access
- state snapshots
- state restoration

State ownership remains with Element Instances.

The State Manager only coordinates access.

------------------------------------------------------------------------

## Observation Service

Provides runtime visibility.

Responsibilities:

- probe attachment
- streaming observation data
- state inspection
- performance metrics

Observation is semantically transparent.

------------------------------------------------------------------------

# Runtime Objects

## Element Definition

Immutable description of an Element.

Contains:

- ports
- metadata
- parameters
- implementation information

Contains no runtime state.

------------------------------------------------------------------------

## Element Instance

Runtime realization of an Element.

Contains:

- current state
- runtime bindings
- parameter values

Only Element Instances contain state.

------------------------------------------------------------------------

## Schema Definition

Immutable graph description.

Contains:

- Elements
- Connections
- Nested Schemas

Contains no runtime state.

------------------------------------------------------------------------

## Schema Instance

Runtime realization of a Schema.

Contains:

- Element Instances
- Runtime Bindings
- Executable Graph

Contains no Schema state.

------------------------------------------------------------------------

# Schema Lifecycle

A Schema progresses through the following stages.

------------------------------------------------------------------------

## Definition

Schema exists as a design object.

    Schema Definition

No execution possible.

------------------------------------------------------------------------

## Validation

Runtime validates structure.

Checks include:

- port compatibility
- graph correctness
- cycle validity

Validation failures prevent execution.

------------------------------------------------------------------------

## Compilation

Runtime generates Executable Graph.

Output contains:

- expanded instances
- execution metadata
- scheduling metadata

------------------------------------------------------------------------

## Instantiation

Runtime creates:

    Schema Instance

and allocates:

    Element Instances

with initial state.

------------------------------------------------------------------------

## Execution

Scheduler executes the graph.

State evolves.

Outputs are produced.

Observation may occur.

------------------------------------------------------------------------

## Snapshot

Runtime captures current Element states.

Produces:

    Snapshot

A Snapshot is:

    ElementInstanceId → State

mapping.

------------------------------------------------------------------------

## Restore

Runtime restores Element states from a Snapshot.

Structure remains unchanged.

------------------------------------------------------------------------

## Termination

Runtime releases:

- Element Instances
- runtime resources
- observation bindings

Schema Definition remains available.

------------------------------------------------------------------------

# Functional Graph Compilation

The Compiler transforms:

    Schema Definition

into:

    Executable Graph

------------------------------------------------------------------------

## Graph Expansion

Nested Schemas are expanded into executable form.

Schema composition remains semantically preserved.

*Implemented 2026-10-31 ([ADR-024](S7_Architectural_Decisions.md#adr-024)).* The compiler flattens every nested Schema into the parent's graph: internal Elements get path ids (`plant/integrator`) and each exposed port becomes a typed wire that is checked but never stepped; a nested Schema's own rate is declared by its ports and seeds its Elements. A nested Schema keeps its id for addressing its outputs. Flattening is local to one node's compilation and never crosses a network boundary. Only a Schema stepped on its own keeps a private runtime.

------------------------------------------------------------------------

## Instance Identification

Each runtime Element receives a unique identifier.

Example:

    Receiver.Filter1.Delay3

Identifiers remain stable during execution.

------------------------------------------------------------------------

## Dependency Analysis

Compiler constructs dependency information.

This information is used by the Scheduler.

------------------------------------------------------------------------

## Cycle Analysis

Compiler identifies:

    Strongly Connected Components

within the Functional Graph.

------------------------------------------------------------------------

## Causal Memory Verification

Every cyclic component must contain at least one Causal Memory Element.

Valid:

    Adder
      ↓
    Delay
      ↓
    Adder

Invalid:

    Adder
      ↓
    Gain
      ↓
    Adder

Invalid cycles prevent execution.

------------------------------------------------------------------------

# Execution Model

## Local Transition

Each Element performs local transitions.

Conceptually:

    (State, Inputs)
            ↓
         step()
            ↓
    (State', Outputs)

This is the fundamental runtime operation.

------------------------------------------------------------------------

## Execution Order

Execution order is determined by causality.

The Runtime may choose any execution order preserving Functional Graph semantics.

------------------------------------------------------------------------

## Parallel Execution

Independent graph regions may execute concurrently.

Parallel execution must not alter semantics.

------------------------------------------------------------------------

## Determinism

Given:

- identical inputs
- identical initial states

the Runtime should produce identical outputs and states.

Subject to scheduler guarantees.

------------------------------------------------------------------------

# State Model

## State Ownership

State belongs exclusively to Element Instances.

------------------------------------------------------------------------

## Runtime State

Runtime infrastructure may maintain internal operational data.

Examples:

- scheduling queues
- statistics
- caches

Such data is not part of computational state.

------------------------------------------------------------------------

## Snapshot Model

Snapshots include only Element states.

Snapshots exclude:

- observation bindings
- GUI state
- deployment metadata

------------------------------------------------------------------------

# Scheduling Model

## Scheduler Responsibility

The Scheduler determines:

    What may execute next.

The Functional Graph determines:

    What is allowed.

------------------------------------------------------------------------

## Scheduler Independence

The Formal Model does not prescribe:

- topological scheduling
- event scheduling
- parallel scheduling

These remain implementation choices.

------------------------------------------------------------------------

## Scheduler Constraint

Every Scheduler must preserve causal semantics.

------------------------------------------------------------------------

# Observation Integration

Observation attaches to Runtime through Observation Services.

Observation must never:

- modify state
- modify causality
- modify execution order

Observation is read-only.

------------------------------------------------------------------------

# Deployment Interaction

Deployment is external to Runtime semantics.

Runtime receives already deployed Schemas.

Runtime does not decide deployment policy.

------------------------------------------------------------------------

# NodeRuntime

NodeRuntime executes Schemas on a single host.

Responsibilities:

- execution
- scheduling
- state management
- observation
- local resource management

NodeRuntime is fully functional in headless mode.

------------------------------------------------------------------------

# NetRuntime

NetRuntime coordinates multiple NodeRuntime instances.

Responsibilities:

- deployment
- discovery
- synchronization
- distributed observation

NetRuntime extends but does not replace NodeRuntime.

------------------------------------------------------------------------

# Future Extensions

Potential future additions include:

- distributed scheduling
- state migration
- checkpointing
- runtime optimization
- heterogeneous execution
- accelerator support

These extensions must preserve Formal Model semantics.

------------------------------------------------------------------------

# Summary

The Runtime is an execution engine for the S7 Formal Model.

The Runtime:

- validates Schemas;
- compiles Functional Graphs;
- creates Element Instances;
- executes local causal transitions;
- manages state;
- provides observation services.

The Runtime does not define semantics.

Semantics originate from:

    Structure
    +
    State
    +
    Connections

The Runtime merely executes them.
