---
name: s7-numerical-verification
description: Verify S7 numerical integrators, DSP algorithms, stateful feedback and reference traces against independent math.
---

# S7 numerical verification

Use for integrator correctness, convergence, numerical regressions, DSP filters/DFT, feedback settling, and model accuracy.

1. Read `AGENTS.md`, `doc/S7_Formal_Model.md`, `s7l2/docs/SIMULATION_MODEL.md`, and affected algorithm documentation/tests. Treat causal publication, evaluation and state commit order as part of the mathematical definition.
2. Record the exact graph or element, input, initial state, step size, sample-index convention, units and arithmetic precision. Verify whether each recorded sample represents values before or after the relevant update.
3. Construct an oracle independent of the S7 code path under test: closed-form solution, explicit recurrence, independently computed direct DFT/convolution or separately implemented high-accuracy solver. A second S7 diagram using the same algorithm is not independent.
4. Report discrete-recurrence error, approximation to continuous time and physical measurement error separately when they apply. Run any independent solver as a test reference outside the S7 graph.
5. Choose cases that discriminate the claimed behavior. Use early steps for indexing and initialization, long runs for drift, and rate refinement for convergence. For DSP claims use the relevant phase, normalization, window, transient and finite-frame conventions.
6. Specify absolute/relative error definitions, the expected mathematical scaling/conditioning and justified tolerances. Do not assume bitwise equivalence where only numerical equivalence is required. For stochastic models use declared seeds and enough trials to support statistical claims.
7. Run the smallest relevant repository build/tests first, then broader regressions as warranted. Record executable commands, commit SHA, platform, plots/traces and results. A plausible plot or green unrelated CI job is insufficient.

Report tested claims, independent reference, maximum errors or other appropriate metrics, tolerance rationale, failures, and what was not verified. Route diagram rendering issues to `s7-visual-validation`.
