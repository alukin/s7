# S7_FORMAL_MODEL.md

# S7 Formal Model

## Purpose

This document defines the formal computational model of S7.

The purpose of the formal model is to provide a language-independent and implementation-independent description of S7 systems.

All runtime implementations, Studio tools and deployment systems should preserve the semantics defined by this document.

------------------------------------------------------------------------

# Fundamental Assumptions

The S7 model is based on the following assumptions:

1.  Computation is local.
2.  State is local.
3.  Causality is explicit.
4.  Structure is compositional.
5.  Time is not fundamental.
6.  Coherence is contextual.

------------------------------------------------------------------------

# Primitive Concepts

The formal model is constructed from the following primitive concepts:

- Signal
- State
- Element
- Connection
- Schema

All other concepts are derived.

------------------------------------------------------------------------

# Signal

A Signal is a value that may be transferred between Elements.

The formal model does not prescribe a concrete representation.

Examples include:

- scalar values
- complex values
- vectors
- frames
- events
- user-defined structures

A Signal is treated as an opaque value.

The formal model does not require timestamps.

------------------------------------------------------------------------

# State

State is information preserved by an Element Instance between evaluations.

State is local.

State belongs exclusively to Element Instances.

State never belongs to Schemas.

------------------------------------------------------------------------

# Element

An Element is the fundamental unit of computation.

An Element is defined as:

    E = (I, O, S, F)

where:

    I = Input space

    O = Output space

    S = State space

    F = Evaluation function

------------------------------------------------------------------------

# Evaluation Function

The evaluation function defines local causal evolution.

    F : I × S → O × S

or equivalently:

    (inputs, state)
            →
    (outputs, state')

An evaluation corresponds to a single:

    step()

operation.

------------------------------------------------------------------------

# Stateless Element

A Stateless Element is an Element whose state space contains exactly one value.

    |S| = 1

Examples:

- Adder
- Gain
- Multiply

------------------------------------------------------------------------

# Stateful Element

A Stateful Element is an Element whose state space contains multiple values.

    |S| > 1

Examples:

- Delay
- Integrator
- Filter
- PLL

------------------------------------------------------------------------

# Connection

A Connection defines a causal relationship between two Ports.

A Connection is directed.

    A → B

means:

Outputs of A may influence Inputs of B.

Connections define causality.

------------------------------------------------------------------------

# Functional Graph

A Functional Graph is a directed graph:

    G = (V, E)

where:

    V = Element Instances

    E = Connections

The Functional Graph completely defines computational semantics.

------------------------------------------------------------------------

# Causality

Causality is defined by reachability within the Functional Graph.

If a path exists:

    A → ... → B

then:

    A may influence B

The reverse is not necessarily true.

------------------------------------------------------------------------

# Structure

Structure is the arrangement of Element Instances and Connections within a Functional Graph.

Structure contains no State.

Structure defines possible causal relationships.

------------------------------------------------------------------------

# Schema

A Schema is a composite Element.

A Schema contains:

    Elements
    Connections
    Schemas

Fundamental invariant:

    Schema ∈ Element

or informally:

    Schema is Element

------------------------------------------------------------------------

# Recursive Composition

Because a Schema is an Element, recursive composition is permitted.

A Schema may contain Schemas without architectural limitation.

------------------------------------------------------------------------

# Runtime Instance

A Runtime Element Instance is defined as:

    EI = (E, s)

where:

    E = Element Definition

    s ∈ S

is the current State.

------------------------------------------------------------------------

# Runtime System

A Runtime System is:

    R = (G, Σ)

where:

    G = Functional Graph

    Σ = Set of Element States

Note:

State belongs to Elements.

The graph itself has no State.

------------------------------------------------------------------------

# Snapshot

A Snapshot is a mapping:

    Snapshot :
    ElementInstance → State

A Snapshot is not a Schema State.

Schemas do not possess State.

------------------------------------------------------------------------

# Feedback Loops

A Feedback Loop is a cycle in the Functional Graph.

Cycles are permitted.

------------------------------------------------------------------------

# Causal Memory

A Causal Memory Element is an Element without direct feedthrough: its outputs at step n depend only on its State and inputs from steps before n, never on its inputs at step n.

Examples:

- Delay
- PreviousValue

Keeping State does not make an Element causal memory. An Integrator (y[n] = y[n-1] + x[n]) or an FIR/IIR filter whose output uses the current input has direct feedthrough: it is a Stateful Element evaluated in causal order together with stateless Elements, and its output is visible downstream in the same step. A feedback loop through such Elements needs an explicit Delay.

A nested Schema has direct feedthrough. It adds no delay beyond the Elements it contains, and it cannot close an outer cycle even if it contains causal memory internally.

Evaluation of one step:

1. Publish the outputs that Causal Memory Elements computed in the previous step.
2. Evaluate all other Elements, stateful or not, in topological order.
3. Evaluate each Causal Memory Element from the current inputs. Its new output is staged and published at the start of the next step (1), so all values read after a step belong to that step.

------------------------------------------------------------------------

# Valid Cycles

A cycle is valid if and only if it contains at least one Causal Memory Element.

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

because no causal memory exists.

------------------------------------------------------------------------

# Strongly Connected Components

Let:

    SCC(G)

denote the strongly connected components of a Functional Graph.

A Functional Graph is valid if every SCC with cardinality greater than one contains at least one Causal Memory Element.

------------------------------------------------------------------------

# Evaluation Semantics

The formal model does not prescribe a scheduling algorithm.

The Scheduler is implementation-dependent.

However, every Scheduler must preserve Functional Graph semantics.

------------------------------------------------------------------------

# Semantic Equivalence

Two executions are semantically equivalent if they produce identical sequences of:

    Outputs
    States

for identical:

    Inputs

regardless of scheduling strategy.

------------------------------------------------------------------------

# Coherence Domain

A Coherence Domain is a context in which temporal and phase relationships between Signals may be established.

A Coherence Domain is defined as:

    CD = (Signals, Relations)

where:

    Relations

represent meaningful phase or temporal relationships.

------------------------------------------------------------------------

# Coherence Independence

Coherence Domains are independent of:

- Functional Graph topology
- Deployment Graph topology
- Runtime implementation

A Coherence Domain may span multiple Nodes.

------------------------------------------------------------------------

# Deployment

Deployment is not part of the computational model.

Deployment affects execution location but does not affect computational semantics.

------------------------------------------------------------------------

# Observation

Observation is not part of the computational model.

Observation must not modify:

- State
- Structure
- Causality

Observation is therefore semantically transparent.

------------------------------------------------------------------------

# Derived Principles

The following principles follow directly from the formal model:

1.  State belongs only to Element Instances.
2.  Schemas define Structure, not State.
3.  Connections define Causality.
4.  Runtime enforces Causality.
5.  Scheduling is secondary to Semantics.
6.  Cycles require Causal Memory.
7.  Deployment does not alter Semantics.
8.  Observation does not alter Semantics.
9.  Coherence is independent of Deployment.
10. Time is not required by the computational model.

------------------------------------------------------------------------

# Summary

The S7 formal model is based on five primitive concepts:

    Signal
    State
    Element
    Connection
    Schema

From these concepts emerge:

    Structure
    Causality
    Dynamics
    Coherence
    Distributed Execution
    Observation

The model intentionally minimizes fundamental assumptions while remaining expressive enough to describe complex distributed causal systems.
