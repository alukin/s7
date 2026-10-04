# ARCHITECTURE_OVERVIEW.md

# S7 Architecture Overview

## Purpose

This document defines the fundamental architectural principles of the S7 platform.

It serves as the primary architectural reference for all other S7 documentation.

All architectural, runtime and studio-related decisions must remain consistent with this document.

------------------------------------------------------------------------

# What Is S7?

S7 is a platform for constructing, deploying and observing distributed causal computing systems.

The project originated from experiments in digital signal processing, software-defined radio, distributed sensing and dynamic systems, but its architecture is intentionally more general.

The goal of S7 is to provide a unified framework for describing, executing and observing causal systems.

------------------------------------------------------------------------

# Core Principles

## Causality Precedes Time

The primary organizing principle of S7 is causality.

Time is not fundamental to the computational model.

A valid S7 system may operate entirely without a notion of global time.

Temporal relationships may emerge from coherent contexts, synchronization mechanisms or observation systems.

------------------------------------------------------------------------

## Coherence Precedes Global Time

Many physical systems require reasoning about:

- phase
- delay
- synchronization
- temporal alignment

S7 introduces Coherence Domains as the primary abstraction for such relationships.

A Coherence Domain defines a context in which temporal and phase relationships between signals may be meaningfully established.

Global timestamps are optional.

------------------------------------------------------------------------

## Dynamics Emerge From State, Structure and Causality

Dynamics are not created by state alone.

Dynamics emerge from:

- State
- Structure
- Causality

State provides memory.

Structure provides relationships.

Causality governs information propagation.

Complex system behavior emerges from their interaction.

------------------------------------------------------------------------

## Headless First

All S7 systems must be capable of operating without graphical interfaces.

Graphical tools are clients of runtime services.

GUI is never part of the computational model.

------------------------------------------------------------------------

## Explicit Network Boundaries

Distributed execution must remain visible in the model.

Network communication is represented explicitly using NetSource and NetSink elements.

Network communication is never inserted implicitly by deployment tools.

------------------------------------------------------------------------

# Fundamental Entities

## Element

An Element is the smallest executable unit of computation.

An Element:

- consumes signals
- maintains optional state
- produces signals

Elements do not know:

- deployment topology
- GUI representation
- observation systems
- network topology

------------------------------------------------------------------------

## Schema

A Schema is a composite Element.

Schemas contain:

- Elements
- Connections
- Schemas

Fundamental invariant:

Schema is Element.

Schemas may be nested recursively.

------------------------------------------------------------------------

## Signal

A Signal is information exchanged between Elements.

The formal model intentionally does not prescribe a universal signal representation.

Examples:

- double
- complex
- vectors
- frames
- events
- user-defined types

------------------------------------------------------------------------

## State

State belongs exclusively to Element instances.

State never belongs to Schemas.

A Schema defines structure.

An Element defines computation.

An Element instance owns state.

------------------------------------------------------------------------

## Coherence Domain

A Coherence Domain defines a context in which phase and temporal relationships between signals may be established.

Coherence Domains:

- are first-class concepts
- may span multiple Nodes
- may be reconstructed from signal structure
- do not require a global clock

------------------------------------------------------------------------

# Graph Model

S7 distinguishes three independent graph structures.

------------------------------------------------------------------------

## Functional Graph

The Functional Graph defines computational semantics.

It consists of:

- Elements
- Connections
- Schemas
- NetSource
- NetSink

The Functional Graph defines causality.

------------------------------------------------------------------------

## Observation Graph

The Observation Graph defines observation relationships.

Examples:

- scopes
- spectrum analyzers
- recorders
- loggers
- statistical monitors

Observation never modifies computational semantics.

------------------------------------------------------------------------

## Deployment Graph

The Deployment Graph maps Schemas to Nodes.

In deployment terms, a Node is a runtime instance (for example, one `s7runtime` process), not necessarily a unique physical host.

Multiple runtime instances may run on the same host when they use distinct network endpoints and identifiers.

Fundamental rule:

Schema is the unit of deployment.

Deployment does not alter Functional Graph semantics.

------------------------------------------------------------------------

# Runtime Model

## Structure

Schemas define structure.

Elements define computation.

Connections define causality.

Runtime performs execution.

------------------------------------------------------------------------

## Causality

Causality originates from the Functional Graph.

Connections represent causal relationships.

Runtime does not create causality.

Runtime enforces causality.

------------------------------------------------------------------------

## State Ownership

State belongs exclusively to Element instances.

There is no Schema state.

A running Schema instance contains Element instances.

Each Element instance may contain state.

------------------------------------------------------------------------

## Local Transition

Each Element performs local causal transitions.

The preferred operation name is:

    step()

instead of:

    tick()

because execution is not tied to global time.

Conceptually:

(State, Inputs) ↓ step() ↓ (State’, Outputs)

------------------------------------------------------------------------

# Cycles

Cycles are permitted.

A cycle is valid only if it contains causal memory.

Examples:

Valid:

Adder → Delay → Adder

Valid:

Integrator → Gain → Delay → Integrator

Invalid:

Adder → Gain → Adder

Invalid (an Integrator keeps state but has direct feedthrough, so it is not causal memory):

Integrator → Gain → Integrator

The precise definition of causal memory is part of the formal model.

------------------------------------------------------------------------

# Schema Validation

Before execution, a Schema must be validated.

Validation responsibilities include:

- connection validation
- type validation
- cycle validation
- deployment validation
- metadata validation

A valid Schema may be compiled into an Executable Graph.

------------------------------------------------------------------------

# Execution Pipeline

Schema Definition ↓ Validation ↓ Compilation ↓ Executable Graph ↓ Scheduler ↓ Execution

------------------------------------------------------------------------

# Scheduler

The Scheduler is not the source of causality.

The Scheduler is an execution strategy.

Different schedulers may exist:

- topological
- parallel
- real-time
- distributed

Provided they preserve Functional Graph semantics.

------------------------------------------------------------------------

# Runtime Layers

## NodeRuntime

Responsible for:

- local execution
- state management
- scheduling
- observation services
- resource management

A NodeRuntime may execute independently.

A single host may execute multiple `NodeRuntime` instances concurrently.

------------------------------------------------------------------------

## NetRuntime

Responsible for:

- node discovery
- deployment
- synchronization
- distributed observation
- network coordination

NetRuntime extends NodeRuntime.

------------------------------------------------------------------------

# Observation

Observation is independent from computation.

Observation is independent from deployment.

Observation is implemented as a runtime service.

Observation clients may connect and disconnect dynamically.

Observation must never alter computational semantics.

------------------------------------------------------------------------

# Studio

S7 Studio is a runtime client.

Studio is not part of runtime execution.

Studio provides:

- Design View
- Deployment View
- Observation View

------------------------------------------------------------------------

# Architectural Invariants

1.  Causality precedes time.
2.  Coherence precedes global time.
3.  Dynamics emerge from State, Structure and Causality.
4.  Schema is Element.
5.  Schema is the unit of deployment.
6.  State belongs only to Element instances.
7.  Schemas define structure, not state.
8.  Connections define causality.
9.  Runtime enforces causality.
10. Observation is independent from computation.
11. Deployment is independent from computation.
12. Network boundaries are explicit.
13. Cycles require causal memory.
14. Headless execution is a primary operating mode.

------------------------------------------------------------------------

# Long-Term Vision

The long-term goal of S7 is to provide a unified platform for distributed causal computing.

The platform should support:

- distributed signal processing
- distributed sensing
- scientific experimentation
- SDR systems
- audio systems
- control systems
- dynamic simulations

while preserving a minimal conceptual model based on:

- State
- Structure
- Causality
- Coherence
