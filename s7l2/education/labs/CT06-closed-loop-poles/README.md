# CT06 — Can feedback gain move stable poles outside the unit circle?

**60 minutes · intermediate · prerequisites: FN06, CT03/CT04.**
Original sampled-loop experiment aligned with MIT root-locus teaching;
readings checked 2026-11-19. Current 0.2.0 development source on Linux,
Studio or headless C++; no hardware, plugin or network required.
Status: diagram-ready; see [development evidence](../../EVIDENCE.md).

Predict the closed poles as gain changes, distinguish marginal from
asymptotic stability, and connect the poles to actual state evolution.

## Predict the sampled loop

Two unit-time-constant lags, initially zero, are connected in cascade with
negative proportional feedback. All signals are normalized, unit **1**.
The actuator is deliberately **ideal and unlimited**: CT05's limiter is not
present. A growing command must remain visible in a linear stability experiment.

At h=0.1 s (10 Hz), the graph's exact difference equations are:

```text
r[n] = 0 for n<10, then 1
e[n] = r[n] − y[n]; u[n] = K e[n]
x[n+1] = 0.9 x[n] + 0.1 u[n]
y[n+1] = 0.9 y[n] + 0.1 x[n]
x[0] = y[0] = 0
```

Both updates use old states. The first changed reference is observed at
Scope time **1.1 s** (n=10). At n=11, plotted at 1.2 s, x=0.1K and y=0;
at n=12, plotted at 1.3 s, y=0.01K. These two stages are causal memory,
not an added communication or feedback delay.

The closed state matrix is `A=[[0.9,−0.1K],[0.1,0.9]]`.
Its characteristic equation and roots are:

`(z−0.9)²+0.01K=0`, hence **z=0.9±0.1i√K** for K>0.

| K | Closed poles | Pole magnitude | Interpretation |
| --- | --- | --- | --- |
| 4 | 0.9±0.2i | √0.85 ≈ 0.921954 | Decaying oscillation |
| 16 | 0.9±0.4i | √0.97 ≈ 0.984886 | Slower decay, more ringing |
| 19 | 0.9±i√0.19 | 1 | Marginal, no asymptotic decay |
| 25 | 0.9±0.5i | √1.06 ≈ 1.029563 | Growing oscillation |

The sampled stability condition is **|z|<1**, so K<19 for positive K.
Positive real part 0.9 is compatible with sampled stability. Do not apply
the continuous s-plane condition Re(s)<0 directly to these z-plane roots.
The unsaturated equilibrium for constant r=1 is x=y=K/(1+K); it is an
attractor only in the stable case. A smaller steady offset can accompany a
much worse transient as K approaches 19.

## Run and measure

Open [ct06_closed_loop_poles.s7diagram](ct06_closed_loop_poles.s7diagram)
through **Help → Education: start here**. Keep **32 s at 10 Hz**.

1. Follow error → command → first lag x → second lag y → error. Each lag
   is an explicit rate/integrator network with zero initial state.
2. Open **Step and both causal states** (`scope`). Compare reference,
   first_state and output. The trace segments connect discrete samples.
3. Open **Ideal command and tracking error** (`command_scope`). A large
   control demand is part of the model, not a hidden actuator limit.
4. Calculate the pole magnitude and predict decay before changing K.

Every sample at index n is observed after step n+1 at model time **(n+1)h**.
The plotted state represents source/state time nh. No wall-clock timing is
used. The continuous two-lag plant is only the intended analogy; the equations
above define the actual forward-Euler implementation.

## Change gain and explain

Select **gain**, press **F4**, change **value 4 → 16**, save a copy, reopen
and rerun. Predict a higher eventual output (16/17 instead of 4/5), but
slower oscillation decay. Finite 32 s data need not equal the asymptotic value.
Restore 4 before comparing the original response.

As deliberate failure/limit experiments, try **19** and **25**. The former
has sustained modal amplitude, the latter grows. Keep their axes visible:
auto-ranging can make growing and bounded curves look deceptively similar.
Do not call a finite, noncrashing run “stable.” Restore K=4 afterward.

A useful exact check, independent of a graph stepper, is the complex mode
`w[n]=(x[n]−s)+i√K(y[n]−s)`, s=K/(1+K), after the reference step.
Then `w[n+1]=(0.9+0.1i√K)w[n]`. Its magnitude changes by the pole magnitude
each step. This checks state order as well as the stability classification.

## Independent verification

The [test](../../../s7tests/education_loop_smoke.cpp) loads the exact saved
graph with the production loader/validator/compiler and observes every source,
error, command, rate and state. Its [reference](../../../s7tests/education_loop_reference.hpp)
uses a closed-form complex matrix power, not another S7 graph or integrator.
It checks K=0,1,4,16,19,25 and same-build fresh-run repeatability. The growing
case retains a relative numerical budget; it is not relabeled a stable result.

F64 budget: `1e−10×max(1,|reference|)` in signal units; time 1e−12 s with
separate nanosecond checks. Studio also checks literal bindings, every
journal/recording value, displayed points/extrema, units and the primary
edit/save/reopen/rerun workflow. These are development checks; native Windows,
installed packages and the final collection-wide viewer audit remain open.

```bash
cmake --build build-release --target s7tests_education_loop s7studio_gui_workflows_test --parallel 2
ctest --test-dir build-release -R '^(s7tests_education_loop|s7studio_gui_education_loop)$' --output-on-failure -V
```

## MIT reading and next experiment

[MIT 16.06, Fall 2012, Lecture 11: The Root Locus Method](https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_11/),
PDF pages 1–3, explains tracing characteristic roots as a scalar gain varies.
Its examples are continuous-time; this original S7 experiment explicitly
uses the z-plane/unit-circle criterion. No lecture figure, worked problem
or solution is copied. Diagram/generator: CC0-1.0; guide: CC-BY-4.0.

Next: [CT07 — stability margins](../CT07-stability-margins/README.md).
[Return to the learning route](../../index.md).
