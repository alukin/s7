# TERMINOLOGY.md

# S7 Terminology

## Purpose

This document defines the official terminology used throughout the S7 project.

All documentation, source code, runtime interfaces and Studio components should use these definitions consistently.

------------------------------------------------------------------------

# Fundamental Concepts

## Causality

A relationship in which one computation may influence another.

Causality is represented by Connections within the Functional Graph.

Causality is the primary organizing principle of S7.

Time is not required to define causality.

------------------------------------------------------------------------

## Structure

The arrangement of Elements and Connections within a Schema.

Structure defines possible causal relationships.

Structure itself contains no state.

------------------------------------------------------------------------

## Dynamics

System evolution resulting from the interaction of:

- State
- Structure
- Causality

Dynamics are properties of systems rather than individual Elements.

------------------------------------------------------------------------

## Coherence

The ability to establish meaningful phase or temporal relationships between Signals.

Coherence may arise from:

- common clocks
- synchronized acquisition
- signal structure
- correlation methods
- reconstruction algorithms

Coherence is independent from deployment topology.

------------------------------------------------------------------------

## Coherence Domain

A context in which phase and temporal relationships between Signals may be meaningfully established.

Coherence Domains are first-class concepts within S7.

A Coherence Domain may span multiple Nodes.

A Coherence Domain does not require a global clock.

------------------------------------------------------------------------

# Computational Model

## Element

The smallest executable unit of computation.

An Element:

- consumes Signals
- maintains optional State
- produces Signals

An Element defines local computation.

------------------------------------------------------------------------

## Stateful Element

An Element that maintains internal State between successive evaluations.

Examples:

- Delay
- Integrator
- IIR Filter
- PLL

------------------------------------------------------------------------

## Stateless Element

An Element whose output depends only on current inputs.

Examples:

- Adder
- Gain
- Multiply
- Complex Conjugate

------------------------------------------------------------------------

## Element Instance

A runtime instance of an Element.

State belongs to Element Instances.

State never belongs to Element definitions.

------------------------------------------------------------------------

## Step

A single local causal transition performed by an Element.

Conceptually:

(State, Inputs) → (State’, Outputs)

The preferred runtime operation name is:

step()

------------------------------------------------------------------------

## Signal

Information exchanged between Elements.

Examples:

- double
- complex
- vector
- vector\<complex\>
- events
- user-defined types

Signals are not required to contain timestamps.

------------------------------------------------------------------------

## State

Information preserved by an Element Instance between successive Step operations.

State belongs exclusively to Element Instances.

------------------------------------------------------------------------

## Parameter

Configuration data associated with an Element.

Parameters influence behavior but are not considered State.

Examples:

- gain
- sample rate
- threshold
- center frequency

------------------------------------------------------------------------

# Structural Model

## Schema

A composite Element.

A Schema contains:

- Elements
- Connections
- Schemas

Fundamental invariant:

Schema is Element.

------------------------------------------------------------------------

## Connection

A directed relationship between two Ports.

Connections represent causality.

Connections define information flow.

------------------------------------------------------------------------

## Port

An interaction point of an Element.

Ports may be:

- Input Ports
- Output Ports

------------------------------------------------------------------------

## Functional Graph

The graph formed by Elements and Connections.

The Functional Graph defines computational semantics.

The Functional Graph defines causality.

------------------------------------------------------------------------

## Feedback Loop

A cycle within a Functional Graph.

Feedback loops are permitted.

A valid feedback loop must contain causal memory.

------------------------------------------------------------------------

## Causal Memory

A mechanism that breaks instantaneous circular dependency: an Element without direct feedthrough, whose outputs do not depend on its inputs in the same step.

Examples:

- Delay
- PreviousValue

State-holding Elements with direct feedthrough (Accumulator, TrapezoidIntegrator, FIR/IIR filters, nested Schemas) are not causal memory; EulerIntegrator (forward Euler) is.

Causal memory allows valid cycles.

------------------------------------------------------------------------

## Model Time

The time a step represents, independent of wall-clock time. One run step advances model time by `dt`, the run's step, which the user sets for each run. Its inverse is the model-time rate, an exact rational.

See [S7_Sample_Rate_Model.md section 8](S7_Sample_Rate_Model.md#8-model-time-and-element-rate-classification-2026-10-31).

------------------------------------------------------------------------

## Rate Domain

A set of connected Elements that share one sample rate, bounded only by Resamplers. The domain at the model-time rate steps once per run step; other domains step as many times as their own samples fall within the model time reached.

------------------------------------------------------------------------

## Rate-Following Element

An Element whose rate is only a step size (an integrator's `dt`, a generator's sample rate). It takes the rate of its Rate Domain instead of holding one of its own.

------------------------------------------------------------------------

## Rate-Authoritative Element

An Element whose rate is fixed by the outside world: a file, a recording, a network stream or a hardware device. Its rate is checked, never adopted or changed implicitly.

------------------------------------------------------------------------

## Rate-Designed Element

An Element whose other parameters were designed for a particular rate (filter taps or coefficients). It records that design rate, recalculates its parameters for another rate where its design intent allows, and otherwise refuses to run.

------------------------------------------------------------------------

## Resampler

The rate-changing Element: the only Element whose input and output rates may differ. It must be placed explicitly; S7 never inserts one. It passes values through unchanged at equal rates, interpolates with order 0, 1 or 2, and filters against aliasing when it lowers the rate.

------------------------------------------------------------------------

# Runtime Model

## NodeRuntime

A local execution environment responsible for executing Functional Graphs.

Responsibilities:

- scheduling
- state management
- resource management
- observation services

------------------------------------------------------------------------

## NetRuntime

A distributed execution environment coordinating multiple NodeRuntime instances.

Responsibilities:

- discovery
- deployment
- synchronization
- distributed observation

------------------------------------------------------------------------

## Scheduler

A runtime component that determines execution order.

The Scheduler does not define causality.

The Scheduler enforces causality defined by the Functional Graph.

------------------------------------------------------------------------

## Executable Graph

A compiled runtime representation of a validated Schema.

The Executable Graph is optimized for execution.

------------------------------------------------------------------------

## Schema Validation

The process of verifying correctness of a Schema.

Validation may include:

- type checking
- connection checking
- cycle checking
- metadata checking

------------------------------------------------------------------------

## Schema Compilation

The process of transforming a validated Schema into an Executable Graph.

------------------------------------------------------------------------

## Snapshot

A captured set of States belonging to Element Instances.

A Snapshot is not a Schema State.

Schemas do not have State.

------------------------------------------------------------------------

# Deployment Model

## Node

A runtime instance (for example, one `s7runtime` process) capable of executing Schemas.

A Node is not necessarily a unique physical host: several Nodes may run on one host with distinct endpoints, identities and state.

Example hosts:

- desktop computer
- server
- Raspberry Pi
- SDR device
- embedded Linux system

------------------------------------------------------------------------

## Deployment

The placement of Schemas onto Nodes.

Deployment does not alter computational semantics.

------------------------------------------------------------------------

## Deployment Graph

A graph describing placement relationships:

Schema → Node

------------------------------------------------------------------------

## Deployment Unit

The smallest deployable architectural unit.

In S7:

Schema is the Deployment Unit.

------------------------------------------------------------------------

# Network Model

## NetSource

An Element that receives Signals from the network.

NetSource is part of the Functional Graph.

------------------------------------------------------------------------

## NetSink

An Element that transmits Signals to the network.

NetSink is part of the Functional Graph.

------------------------------------------------------------------------

## Network Boundary

An explicit point where information crosses Node boundaries.

Network Boundaries are represented using NetSource and NetSink.

------------------------------------------------------------------------

# Observation Model

## Observation

The process of inspecting a running system without altering its semantics.

Observation is independent from computation.

Observation is independent from deployment.

------------------------------------------------------------------------

## Observation Graph

A graph describing observation relationships.

Observation Graphs do not alter Functional Graph semantics.

------------------------------------------------------------------------

## Probe

A logical observation attachment point.

A Probe is not part of the Functional Graph.

A Probe belongs to the Observation subsystem.

------------------------------------------------------------------------

## Observation Client

A system that consumes observation data.

Examples:

- Studio
- Scope
- Spectrum Analyzer
- Logger

------------------------------------------------------------------------

# Studio

## Studio

The primary user interface for the S7 ecosystem.

Studio is a client of Runtime services.

Studio is not part of Runtime execution.

------------------------------------------------------------------------

## Design View

A Studio view used for editing Functional Graphs.

------------------------------------------------------------------------

## Deployment View

A Studio view used for managing Deployments.

------------------------------------------------------------------------

## Observation View

A Studio view used for observing running systems.

------------------------------------------------------------------------

# Architectural Principles

## Headless First

All Runtime systems must be capable of operating without GUI components.

------------------------------------------------------------------------

## Explicit Network Boundaries

Network communication must remain visible within the Functional Graph.

------------------------------------------------------------------------

## Schema Is Element

Schemas and Elements share a common abstraction.

------------------------------------------------------------------------

## Schema Is The Unit Of Deployment

Schemas are the smallest deployable architectural units.

------------------------------------------------------------------------

## State Belongs Only To Elements

State exists only within Element Instances.

Schemas define Structure.

Elements define Computation.

State belongs exclusively to Element Instances.
