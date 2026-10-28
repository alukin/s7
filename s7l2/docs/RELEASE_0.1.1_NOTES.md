# S7 0.1.1 preview

S7 0.1.1 improves local simulation and observation in Studio. Studio runs a
schema with its bundled causal graph runtime; no separate service is needed for
local runs. The headless runtime, `s7ctl`, plugins, examples, and the existing
network control and stream features remain included.

## Studio changes

- Recorded Scope runs can be inspected across their full history, compared,
  saved, and reopened. Scope analysis now shares a selected run and model-time
  interval across Time, spectrum, correlation, and trajectory views. Spectrum
  examples include known bin-aligned and off-bin signals.
- The observer dock keeps bounded previews separate from full viewers. Scope
  hover reads stored samples; Live Oscilloscope controls and trigger handles
  support direct inspection and adjustment.
- Editor menus, run controls, element information, fonts, palettes, and bundled
  example selection have been refined. Element metadata is embedded from
  module-owned JSON definitions.
- Plot renders are paced across GUI event-loop passes. Dock previews are capped
  at 10 frames/s, routine viewer status updates at 10 Hz, and 3D views use
  cheaper multisampling. Recording and exported data are unchanged by these
  display limits.

## Qualification and limits

The native Fedora 44 Release suite passes 83/83 tests, including Qt GUI
workflows. A 120-second mixed observer workload on Fedora 44 with Xvfb and
software OpenGL recorded a 236 ms maximum GUI timer gap, no gaps over the
proposed 250 ms responsiveness target, about 1,000 published synthetic source
steps/s, no capture error, and at most four active dock plots. It peaked at
923 MB RSS. This measures GUI load, not `NodeRuntime` throughput or physical GPU
behavior.

Linux x86-64 binaries are qualified on Fedora 44. They require GLIBC 2.43,
GLIBCXX 3.4.36, and CXXABI 1.3.15; older distributions are not qualified.
The Windows x86-64 bundle is experimental and cross-built with Clang 22. Of
79 registered tests, 78 passed under Wine. The Live Oscilloscope module test
stalled in its display-overflow case under Wine and did not pass, although it
passes in the native Linux suite. Native Windows GUI interaction remains
unqualified. Physical GPU, high-DPI, accessibility, other Linux distributions,
larger graphs, and multi-hour soaks remain outside this release qualification.

This is pre-MVP software: APIs, file formats, and protocols can change.
Observation storage is temporary rather than crash-durable. Save or export
results explicitly and keep enough disk space for recordings. Display previews
can skip updates under load; authoritative capture is separate. Live observer
bindings are fixed for a run, and rebinding is deferred.

The detailed [0.1.1 implementation record](RELEASE_0.1.1.md),
[numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md), and
[observer latency profile](OBSERVER_UI_LATENCY_PROFILING.md) describe the
tested envelopes and remaining work.
