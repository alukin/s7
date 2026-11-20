# S7 0.5.0 — preliminary distributed coherence notes

**Status: planning (2026-11-20), not a committed release contract.**
Moved from the earlier proposed 0.4.0 direction by the maintainer.
See the [roadmap](RELEASE_ROADMAP.md).

## Main goal

Relate physical signals acquired on **different nodes with different
hardware clocks** while retaining S7 causal semantics. Do not infer
synchronization from packet arrival, nominal equal sample rates,
shared callback timing or a common UTC label alone.

## Candidate workstreams

1. **Coherence and time contract:** distinguish sample positions,
   source acquisition time, host/processing time, model time, clock
   domains and physical drift. Define explicit mappings, validity,
   uncertainty and failure when timestamps cannot be compared.
2. **Multi-domain signal processing:** build on current rate inference,
   explicit resampling and rate-domain runtime records; extend to
   independent physically paced clocks, drift tracking and bounded
   alignment without silently changing graph causality.
3. **Distributed signal path:** continuity/gap evidence, bounded
   buffering, reconnection, late data and reference-checked delay
   estimation across two or more hardware nodes.
4. **Observation:** show offset/drift/uncertainty, loss, alignment
   residuals and queue behavior as measured, not fabricated facts.
5. **Flagship experiment:** a two-node acoustic array with known
   geometry and independently checked inter-microphone/inter-node
   delay estimates, then additional nodes for better localization.
   A smaller dual-SDR phase/time experiment is useful but needs
   qualified clock/reference hardware before claiming RF coherence.

## Preliminary qualification

Retain synthetic injected offset/drift/loss fixtures with independent
oracles, then measured captured inputs from named devices, timestamps
and uncertainty metadata, verified distributed results and recovery.
Document throughput, latency and known synchronization limitations.
Do not require universal clock synchronization, general FPGA support
or compilation of distributed graphs. A **local** compiled ARM
hardware beta in [0.6.0](RELEASE_0.6.0.md) can pass without universal
multi-node synchronization.
