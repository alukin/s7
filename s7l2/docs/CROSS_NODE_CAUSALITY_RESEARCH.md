# Cross-node causality: literature and scheduling spikes

Research started 2026-11-17 for 0.2.0 work-plan steps X1–X3. This document
records sources, assumptions and experiments before a production design is
chosen. It is not an accepted ADR or a change to S7's execution contract.
The [work plan](RELEASE_0.2.0_WORKPLAN.md#feedback-loops-across-nodes--020-task)
retains the open decision and acceptance gates.
The initial design/findings and
Phase 2 real-transport experiments cite these
sources at the relevant questions and separate results from hypotheses.
The Phase 3 performance experiment adds
derived plans, planning/dispatch scaling and repeated transport measurements.
The [Phase 4 comparison](CROSS_NODE_CAUSALITY_PHASE4.md) adds outgoing-first
static schedules, incremental queues and actual C++ element/runtime costs.

## Literature used

The sources below are original papers, author/university archives and textbooks.
Journal/conference papers, a dissertation, technical reports and teaching notes
have different review status; hosting on a university website alone is not
validation of a proposed S7 design. Section pointers state what was consulted.
No paper's proof transfers to S7 without checking its assumptions.

| ID | Reference and publication type | Consulted material / relevance |
| --- | --- | --- |
| L1 | Edward A. Lee and Sanjit A. Seshia, *Introduction to Embedded Systems: A Cyber-Physical Systems Approach*, second edition, MIT Press, 2017. [Author textbook site](https://ptolemy.org/books/leeseshia/), [PDF](https://ptolemy.berkeley.edu/books/leeseshia/releases/LeeSeshia_DigitalV2_2.pdf). | §6.3, especially pp. 149–155 and Example 6.9: a delay first emits its initial value without reading an input; subsequent firings copy input tokens. Fixed token rates, buffer bounds and deadlock are separate questions. |
| L2 | Edward A. Lee and David G. Messerschmitt, “Synchronous Data Flow,” *Proceedings of the IEEE* 75(9), 1235–1245, 1987. [University record](https://ptolemy.berkeley.edu/publications/papers/87/synchdataflow/), [DOI](https://doi.org/10.1109/PROC.1987.13876). | Publication metadata and abstract checked. Foundational fixed-production/consumption-rate model and static scheduling. The scanned manuscript was downloaded but not fully read; the detailed feedback argument here uses L1/L3, not an assumed theorem from this abstract. |
| L3 | Edward A. Lee, Stephen Neuendorffer and Gang Zhou, “Dataflow,” chapter 3 in Claudius Ptolemaeus (ed.), *System Design, Modeling, and Simulation Using Ptolemy II*, Ptolemy.org, 2014. [Book](https://ptolemy.berkeley.edu/books/Systems/), [chapter](https://ptolemy.berkeley.edu/books/Systems/chapters/Dataflow.pdf). | §3.1.2, pp. 103–105: initial tokens enable feedback; a multirate example needs four tokens despite having a delay. §3.1.3 distinguishes token order from model time. A cycle-with-memory check alone is not a general multirate liveness proof. |
| L4 | Edward A. Lee and Thomas M. Parks, “Dataflow Process Networks,” *Proceedings of the IEEE* 83(5), 773–801, 1995. [Author manuscript](https://ptolemy.berkeley.edu/publications/papers/95/processNets/proceedings.pdf). | The formal model uses FIFO communication, blocking reads and nonblocking writes with conceptually unbounded storage. This is useful semantic background, but S7's bounded, blocking publication and explicit faults require additional analysis. |
| L5 | Thomas M. Parks, *Bounded Scheduling of Process Networks*, PhD dissertation, UC Berkeley, UCB/ERL-95-105, 1995. [Record](https://ptolemy.berkeley.edu/publications/papers/95/parksThesis/), [dissertation](https://ptolemy.berkeley.edu/publications/papers/95/parksThesis/thesis.pdf). | Chapter 4, especially §4.2: bounding channels introduces write blocking and can create artificial deadlock. Dynamic capacity growth is studied there; it is not a proposed S7 policy. |
| L6 | Marc Geilen and Twan Basten, “Requirements on the Execution of Kahn Process Networks,” *ESOP 2003*, LNCS 2618, 319–334. [University publication record](https://research.tue.nl/en/publications/requirements-on-the-execution-of-kahn-process-networks/), [DOI](https://doi.org/10.1007/3-540-36575-3_22). | Peer-reviewed publication metadata and abstract checked. It identifies limitations of Parks' scheduler and supplies stronger execution requirements. Full-text retrieval was unsuccessful; no algorithm or proof from it is claimed implemented or fully reviewed here. Do not treat L5 as a universal scheduler guarantee. |
| L7 | José Luis Pino, Shuvra S. Bhattacharyya and Edward A. Lee, *A Hierarchical Multiprocessor Scheduling Framework for Synchronous Dataflow Graphs*, UC Berkeley technical report UCB/ERL M95/36, 1995. [Record](https://www2.eecs.berkeley.edu/Pubs/TechRpts/1995/2770.html), [report](https://ptolemy.berkeley.edu/publications/papers/95/erl-95-36/erl-95-36.pdf). | §§2.2–2.3 and 5: execution precedence and clustering. Grouping actors can introduce deadlock; the composition theorem supplies sufficient conditions for safe clustering. Mapping a valid actor graph to sequential node processes therefore needs its own check. |
| L8 | Ye Zhou and Edward A. Lee, “A Causality Interface for Deadlock Analysis in Dataflow,” *EMSOFT 2006*, 44–52. [Publication record](https://ptolemy.berkeley.edu/publications/papers/06/causalityInterfaces_EMSOFT06/), [author paper](https://ptolemy.berkeley.edu/publications/papers/06/causalityInterfaces_EMSOFT06/CausalityInterfaces_EMSOFTpaper_ZHOU.pdf). | Introduction and §2: causality interfaces express dependencies between particular input/output ports and compose through hierarchy. This motivates preserving path-level information; it does not justify treating every stateful component as causal memory. |
| L9 | Leslie Lamport, “Time, Clocks, and the Ordering of Events in a Distributed System,” *Communications of the ACM* 21(7), 558–565, 1978. [Author paper](https://lamport.org/pubs/time-clocks.pdf), [DOI](https://doi.org/10.1145/359545.359563). | “The Partial Ordering” and “Logical Clocks”: local event order and send-before-receive induce causality. Increasing logical timestamps do not establish the converse. Physical transmission latency is not an explicit model-state initialization rule. |
| L10 | Tomas Kalibera and Richard Jones, “Rigorous Benchmarking in Reasonable Time,” *ISMM 2013*, 63–74. [University record](https://kar.kent.ac.uk/33611/), [corrected author manuscript](https://kar.kent.ac.uk/33611/45/p63-kaliber.pdf), [DOI](https://doi.org/10.1145/2464157.2464160). | Introduction, §2 and §6: account for variation between executions and iterations, distinguish initialization from independence, and qualify performance ratios. Phase 3 uses warm-up and independent process repetitions but does not claim to implement the paper's full statistical experiment-sizing method. |

## MIT and Stanford teaching material

| ID | Material | Use and boundary |
| --- | --- | --- |
| T1 | Nancy Lynch, MIT 6.852J/18.437J *Distributed Algorithms*, Fall 2009: [course notes](https://ocw.mit.edu/courses/6-852j-distributed-algorithms-fall-2009/pages/lecture-notes/), [Class 11](https://ocw.mit.edu/courses/6-852j-distributed-algorithms-fall-2009/resources/mit6_852jf09_lec11/), [Class 12](https://ocw.mit.edu/courses/6-852j-distributed-algorithms-fall-2009/resources/mit6_852jf09_lec12/). | Class 11 covers causality/logical time; Class 12 covers consistent snapshots, termination and deadlock detection. Runtime deadlock diagnosis needs process/channel state, not a timeout treated as a proof of graph invalidity. These are teaching notes, not a dataflow scheduling implementation. |
| T2 | Stanford EE382C *Interconnection Networks*, Spring 2016, lecture 6, [“Deadlock and Livelock”](https://web.stanford.edu/class/ee382c/lectures/06_deadlock.pdf). | Slides 1–14 define progress, holds/waits and resource dependencies. The network-buffer model differs from S7's actor graph. Use it to structure analysis of workers, queues and ACK resources; do not equate a physical channel-dependency cycle with a semantic S7 cycle. |

For a short reading sequence, start with L1 Example 6.9 and L3 §3.1.2,
then T1 Classes 11–12 and T2 slides 1–14, followed by L7 §5 and L5 §4.2.
L6's correction is required background before considering a general KPN scheduler.

## Mapping to S7: findings, not a design decision

The following are S7-specific deductions to investigate, not claims that the
publications prove the correctness of the current runtime.

1. **Model initialization is explicit.** Existing `NetSink` sends the current
   input and `NetSource` waits for the next value; neither specifies a stream
   initial token. Transport buffering and latency alone cannot supply the
   initialized state illustrated in L1/L3. See the
   [network element contract](../../doc/S7_Net_Element_Contract.md).
2. **Check every direct-feedthrough cycle, not merely whether an SCC contains
   memory somewhere.** Preserve nested path dependencies (ADR-024, L8).
   Stateful direct-feedthrough elements are not interchangeable with `Delay`.
3. **Causal validity and deployment progress need separate evidence.** S7's
   [compiler](../s7core/src/compiler.cpp) chooses a FIFO topological order
   locally. Its [runtime](../s7core/src/runtime.cpp) executes that order
   serially, and [NetSource](../s7net/include/s7/net.hpp) can block inside it.
   A ready outgoing computation can consequently be held behind an unrelated
   incoming read. L7 explains why graph partitioning/scheduling deserves an
   explicit analysis rather than an assumption of compositional liveness.
4. **Finite resources add dependencies.** Send-log capacity, packet sealing,
   consumption ACKs and fan-out slow peers matter in addition to the element
   graph (L4–L6, T2). A cycle check must not claim to prove arbitrary bounded
   runtime progress.
5. **Start with the documented single-rate scope.** L3's multirate example
   shows why one initialized delay is not generally sufficient across rates.
   Current S7 rejects feedback across rate domains. These spikes do not
   authorize rate-crossing feedback, new resampling rules or a global solver.
6. **Timing and recovery are separate contracts.** Lamport ordering (L9/T1)
   does not prescribe lockstep execution or repair a missing value. A restart,
   sequence gap or lost state needs the existing explicit fault/recovery path;
   an implicit zero or last-value fallback would change the model.

## Spike contract

Experiments compare three scheduling approaches without changing production:

| Approach | Question |
| --- | --- |
| Existing fixed local order | Which semantically valid partitions fail when a worker waits on an incoming stream before publishing a ready outgoing value? |
| Projected global causal order | Can a single same-step precedence order, projected onto each node, preserve the serial reference and eliminate those scheduling waits? No wall-clock/global-step barrier is assumed. |
| Readiness-based local execution | Can the owner skip temporarily unavailable stream operations while executing other causally ready work in the same step? What additional state/eligibility checks are required? |

Common conditions: explicit initialized memory, one sample per stream per
logical step, one rate domain, one owner per node, fixed bounded queues,
no fabricated samples or unbounded capacity growth. A delay-free cycle is a
negative control, not a supported algebraic-loop experiment. An artificial
stream-initialization control must be labeled a changed graph, not a transport
optimization. Include independent bidirectional streams so node-level cycles
are not automatically mistaken for element-level feedback.

Measure progress/deadlock, output equality against an independent recurrence
or unpartitioned runtime, queue high-water marks, work/check counts, sensitivity
to element order, fan-out, node speed and buffer capacity. Distinguish a
deterministic logical simulation from real `NodeRuntime` checks. Neither alone
qualifies TLS, packet batching, replay, restart, crash durability, coupled
numerical regions, arbitrary plugins or physical devices.

The next design document must cite these source IDs at the relevant decisions,
name the spike artifacts/results, and distinguish demonstrated results,
unproved hypotheses and explicit scope restrictions. X1 stays open until an
architectural decision is recorded; the experiments do not amend it silently.
