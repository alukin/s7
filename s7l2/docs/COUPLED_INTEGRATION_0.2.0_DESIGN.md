# Coupled numerical integration: production design and acceptance

Status: implementation design for `feature/coupled-integration-0.2.0`.
Research reference: `research/numerical-feedback-0.2.0` at
`ec3eb67f42e122d7bad407f5bfb1732d6083f14d`. The Phase 3 runner is a
numerical reference, not a production component. This change must not modify
the distributed coherence or transport model.

## Choice of public model

Three placements were considered. A method attached only to one integrator
cannot solve coupled peers by itself. A separate schema-wide policy makes
method selection explicit but would require a new schema serialization field,
Studio and deployment parsing, and a way to identify the member integrators.
Use **declarative integrator elements plus a compiler-selected numerical
region**: a new F64 `GaussLegendreIntegrator` declares its method, and an
optional `substeps` parameter on F64 `EulerIntegrator` requests simultaneous
Euler substeps. The compiler groups eligible connected integrators; the
runtime solves/stages the group. The default Euler parameter is one and
retains its existing element step and publication semantics. Existing
implicit trapezoid and all existing diagrams retain their current contract.

This is numerical coordination inside a local compiled graph. It is neither
a coherence domain nor a second scheduler. Elements still own their state;
the runtime temporarily substitutes only their published F64 output values
during an accepted numerical trial. The new methods are not aliases for
trapezoid or for per-element stepping with stale peers.

## Region and timing contract

Each selected memory element has one F64 state output, one F64 derivative
input and one declared physical step `h`. A region contains selected states
with the same method, outer rate and substep count connected through a
derivative path of stateless direct-feedthrough elements. A one-way
dependency within that configuration is included so both states see the
same substep or Runge–Kutta stage. Differently configured regions and other
causal-memory outputs may supply one-way held inputs. A feedback cycle
crossing methods or configurations is rejected. Disconnected systems get
separate regions to avoid an unnecessarily large dense stage solve.

The compiler retains its existing causal order and creates an ordered trial
cone: only stateless, explicitly approved built-in F64 arithmetic that is
both downstream of a selected state and upstream of a selected derivative
may run repeatedly. Unknown plugins, I/O, nested/stateful direct feedthrough
and arbitrary stateful elements in that cone are rejected. Independent
sources and other causal outputs are evaluated once per physical step and
then held. This is a zero-order-held input contract. It does not claim
fourth-order accuracy for an unsampled time-varying source.

The first runtime step publishes `x₀`, evaluates the causal graph, and stages
`x₁`. The next runtime step publishes `x₁` before causal evaluation.
Intermediate Euler substeps and both Gauss stages are unobservable. The
runtime snapshots all candidate state and pure-cone output storage, restores
it after the solve, and stages all accepted endpoints together. On numerical
failure no candidate is published, the previous pending state remains, and
the documented whole-step output rollback applies. Private state of an
unrelated stateful direct-feedthrough element cannot generally be rolled
back; such an element is never trialed.

## Numerical solver and diagnostics

Euler uses `m` simultaneous derivative evaluations at `h/m`, with all state
components updated together. Gauss–Legendre uses the two-stage fourth-order
tableau and a full `2n` stage residual. Start with damped finite-difference
Newton, bounded iterations and line search, relative/absolute residual
scaling, finite checks, and scale-aware pivot checks. Allocate work arrays
once per compiled region and reuse them across physical steps. Record
derivative-vector and pure-element evaluations, residual/Jacobian/line-search
work, solver iterations, failures and final residual. Report solver work
separately from method discretization error. A-stability does not imply
L-stability or accurate coarse transients.

The existing trapezoid fixed-point algorithm and all supported signal types
stay intact. Share the safe compiled-cone discovery and trial-output
snapshot/replay machinery where doing so preserves its exact behavior.

## Acceptance checklist

- [x] Old Euler and trapezoid diagrams load and produce unchanged results.
- [x] New element and `substeps` metadata serialize through ordinary schema
  and registry paths; unsupported types/values fail with useful errors.
- [x] Both methods solve coupled F64 states through `NodeRuntime`, including
  insertion-order independence and publication of the initial state first.
- [x] Euler `m=1,2,4,8,16,32` matches ordinary fine Euler with identical
  held inputs on oscillator, RC and RLC graphs.
- [x] Gauss stages match independent Padé matrix/input transitions, exhibit
  fourth-order refinement, and conserve lossless quadratic energy.
- [x] RC and all four RLC damping cases match closed-form references within
  documented tolerances; coarse stable steps are checked separately for
  accuracy.
- [x] Mixed coupled policies, unsafe trial elements, nonfinite values and
  nonconvergence fail before invalid state publication; retry behavior is
  deterministic within the stated transactional boundary.
- [x] Existing trapezoid, compiler, validator, runtime, example and schema
  compatibility tests pass. Release cost and work counters are reported.
- [x] Documentation and examples describe only implemented methods; the
  feature branch contains no research raw data or plotting code.
