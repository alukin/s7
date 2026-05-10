# S7 Net I/O (s7netio)

## Scope

`s7netio` introduces network-oriented sink/source element families that fit the same schema wiring model as `s7fileio` and `s7memio`:

- `RealNetSink`
- `RealNetSource`
- `ComplexNetSink`
- `ComplexNetSource`

## Current Backend

The current slice uses an in-process endpoint broker for deterministic integration and testing.

Why this shape now:
- keeps public element API stable
- allows net elements to be used in `s7main` and JSON netlists immediately
- keeps queueing and underrun/overflow semantics explicit before wire transport is enabled

## Config

`NetIOConfig`:
- `endpoint` (string, RSocket-style URI form)
- `num_channels`
- `queue_capacity`
- `zero_on_underrun`

## Registry Integration

`registerNetIO()` is now part of factory registration and is called by `s7main` startup registration flow.

Registered JSON element type names:
- `RealNetSink`
- `RealNetSource`
- `ComplexNetSink`
- `ComplexNetSource`

## RSocket Mapping Plan

Phase 2 can replace the in-process broker with an RSocket transport adapter while keeping element constructors and runtime behavior compatible.

Target mapping:
- sink -> publish framed sample messages to endpoint stream
- source -> subscribe to endpoint stream and decode into typed outputs
- preserve queue and underrun/overflow counters for observability

