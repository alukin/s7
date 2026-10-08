# VISION.md

# S7 Vision

## Purpose

This document describes the motivation, goals and long-term vision of the S7 project.

Unlike ARCHITECTURE_OVERVIEW.md, which defines how S7 is organized, this document explains why S7 exists and what problems it is intended to solve.

------------------------------------------------------------------------

# Why S7 Exists

S7 originated from practical work in:

- digital signal processing;
- software-defined radio;
- distributed sensing;
- scientific experimentation;
- dynamic system modeling.

Over time it became apparent that many existing systems solve only fragments of a larger problem.

Examples include:

- DSP frameworks;
- SDR processing systems;
- simulation environments;
- workflow engines;
- control systems;
- distributed computing platforms.

Each addresses a specific domain.

Few provide a unified model for constructing, executing and observing causal systems.

S7 exists to explore such a unified model.

------------------------------------------------------------------------

# The Central Idea

The central idea of S7 is simple:

Causality is more fundamental than time.

Many computational systems can be described using:

- local computation;
- local state;
- causal relationships.

Global time is often introduced as a convenience rather than a necessity.

S7 attempts to build systems starting from causality and introducing temporal concepts only when required.

------------------------------------------------------------------------

# Beyond Signal Processing

Although S7 originated from DSP and SDR applications, it is not intended to be limited to those domains.

DSP systems are important because they provide practical examples of:

- state;
- feedback;
- coherence;
- dynamic behavior.

However, the concepts explored by S7 are more general.

Potential applications include:

- SDR systems;
- audio systems;
- sensor networks;
- distributed monitoring;
- scientific simulations;
- control systems;
- experimental computational models.

------------------------------------------------------------------------

# Why Causality

Many existing systems organize computation around:

- time;
- events;
- processes;
- threads.

S7 organizes computation around causal relationships.

A causal relationship answers a simple question:

Can one computation influence another?

If the answer is yes, a causal connection exists.

The Functional Graph is therefore a representation of causality rather than merely a representation of data flow.

------------------------------------------------------------------------

# Why Coherence

Many physical systems involve phase relationships.

Examples include:

- SDR receivers;
- antenna arrays;
- acoustic arrays;
- distributed sensors;
- synchronization systems.

Traditional software systems often treat time as a universal global quantity.

Physical systems rarely behave that way.

S7 introduces Coherence Domains as a more fundamental abstraction.

Within a Coherence Domain:

- phase relationships may be established;
- delays may be measured;
- temporal ordering may be reconstructed.

Global time becomes optional rather than mandatory.

------------------------------------------------------------------------

# Why Distributed Execution

Modern systems increasingly consist of multiple computing nodes.

Examples include:

- embedded devices;
- SDR platforms;
- Raspberry Pi clusters;
- edge computing systems;
- sensor networks.

S7 is intended to operate naturally in such environments.

Distributed execution should not be a separate mode of operation.

Instead, it should emerge naturally from the same computational model.

The same causal system should be deployable:

- on a single machine;
- across multiple machines;
- across heterogeneous hardware.

without changing its semantics.

------------------------------------------------------------------------

# Why Headless First

Graphical interfaces are valuable.

However, graphical interfaces should not define system architecture.

S7 adopts a Headless First philosophy.

Every runtime component must be capable of operating independently from graphical tools.

This approach enables:

- embedded deployment;
- remote deployment;
- automated operation;
- distributed execution.

Graphical tools become clients of runtime services rather than architectural dependencies.

------------------------------------------------------------------------

# Why Explicit Network Boundaries

Distributed systems often hide network communication behind runtime abstractions.

This can make system behavior difficult to reason about.

S7 chooses the opposite approach.

Network communication remains visible.

NetSource and NetSink are explicit elements of the Functional Graph.

Network boundaries therefore become part of the system model.

This makes distributed behavior easier to understand, debug and observe.

------------------------------------------------------------------------

# Simplicity As A Design Goal

S7 deliberately prioritizes conceptual simplicity.

The project attempts to minimize the number of fundamental concepts.

The current architectural foundation is based on:

- State;
- Structure;
- Causality;
- Coherence.

More advanced behavior should emerge from combinations of these concepts rather than from introducing additional abstractions.

------------------------------------------------------------------------

# Long-Term Goals

The long-term goals of S7 include:

- distributed causal computing;
- distributed signal processing;
- distributed sensing;
- dynamic observation systems;
- experimental computational research;
- integration with physical devices;
- scalable deployment across heterogeneous nodes.

S7 should remain understandable at the level of a single Element while scaling to large distributed systems.

------------------------------------------------------------------------

# Research Motivation

S7 is not merely a software engineering project.

It is also an exploration of how complex dynamic systems can be described using a minimal set of concepts.

Questions that motivate the project include:

- How much can be built from causality alone?
- Which temporal concepts are fundamental and which are derived?
- How can coherence be represented in distributed systems?
- How can dynamic behavior emerge from local rules?

The project does not attempt to answer these questions through theory alone.

Instead, it seeks practical answers through implementation and experimentation.

------------------------------------------------------------------------

# Non-Goals

S7 is not intended to become:

- a clone of Simulink;
- a clone of GNU Radio;
- a generic workflow engine;
- a distributed database;
- a graphical programming environment without runtime semantics.

Although S7 may share features with such systems, its primary objective is different.

Its objective is to provide a platform for constructing and studying distributed causal systems.

------------------------------------------------------------------------

# Future Direction

Future development should prioritize:

1.  Formal model clarity.
2.  Runtime correctness.
3.  Observation capabilities.
4.  Distributed execution.
5.  Coherence-aware computation.
6.  Simplicity of Element implementation.

Optimization, tooling and user interfaces are important, but should remain secondary to the integrity of the computational model.

------------------------------------------------------------------------

# Vision Statement

S7 is a platform for constructing, executing and observing distributed causal systems.

It seeks to minimize fundamental assumptions while maximizing expressive power.

By treating causality as primary, coherence as contextual and time as an optional derived concept, S7 aims to provide a practical environment for experimentation with complex dynamic systems ranging from signal processing pipelines to large-scale distributed networks.
