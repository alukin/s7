# Codex Task: S7 Integrator and Feedback Dynamics Research Laboratory

## Mission

Implement a reproducible research and educational laboratory for studying discrete integrators in interconnected linear dynamical systems.

Repository: https://github.com/alukin/s7

The goal is to investigate the mathematical and computational properties of integrators used in the S7 analog-computer architecture.

This is **not a request to replace the current S7 integration methods**, introduce a general-purpose ODE solver into the runtime, or redesign the causal execution model.

The immediate objective is to reconstruct and experimentally test several integrator realizations, particularly an early historical approach involving Euler integration and iterative feedback settling within each discrete step.

**Work autonomously, step by step. Update documentation first, then implement the experimental infrastructure and execute the tests.**

Do not stop merely after writing a plan. Complete the experiment as far as the existing architecture and available environment permit.
## 0. Mandatory Git branch and worktree isolation

Before making any changes, create a dedicated research branch:

`research/integrator-feedback`

Requirements:

1. Check the current repository status, branch, and remote tracking information.
2. Fetch the latest changes from `origin`.
3. Create the research branch from the latest `origin/main`, unless the repository has a different designated integration branch.
4. **Prefer a separate Git worktree** for this research so that ongoing S7 0.1.1 development remains completely undisturbed.
5. Do not overwrite, discard, stash, or modify unrelated local changes.
6. If the branch already exists, inspect it and resume work there rather than resetting or recreating it.
7. Perform all research implementation, documentation changes, tests, and commits exclusively on the research branch.
8. Make small, logically separated commits as each implementation phase is completed and verified.
9. Do not merge the research branch into `main` or any release branch.
10. Do not modify the 0.1.1 release work unless explicitly authorized.
11. Do not push, force-push, rebase published commits, or create a pull request without explicit authorization.

Suggested setup, assuming a clean and available branch name:

```bash
git fetch origin
git worktree add -b research/integrator-feedback \
    ../s7-integrator-research origin/main
```

Use a separate build directory inside the research worktree. Keep all experimental reports, generated traces, plots, and temporary files local and outside tracked source directories.

The research branch should remain independently buildable and testable.

### Additional completion requirements

At the end of the task:

- Report the research branch name and worktree path.
- Report the base commit and final HEAD commit.
- Confirm that the original working tree and 0.1.1 development branch were not modified.
- List the research commits and verification results.
- Document any proposed production-runtime changes separately, without implementing architectural changes that require approval.
- Leave the branch ready for review, with no automatic merge.

**The entire integrator research project must remain isolated until its mathematical and experimental results have been reviewed.**

## 1. Background and motivation

S7 originated in 1993–1994 as a Turbo Pascal graphical analog-computer simulator, inspired by William McC. Siebert's *Circuits, Signals, and Systems*.

The original implementation was based on independent computational blocks interconnected into functional graphs. Integrators supplied memory, while arithmetic and functional elements determined instantaneous signal relationships.

Early experiments investigated the accuracy of interconnected linear systems, particularly the effect of integration algorithms on poles, zeros, and feedback behavior.

One historically interesting approach employed Euler-type integration combined with repeated feedback evaluation within each discrete simulation step.

Feedback signals were iterated toward a settled solution before proceeding to the next time step.

The original source code has been lost, and the exact numerical algorithm cannot be reconstructed with certainty.

We therefore want to investigate plausible mathematical interpretations rather than claim to have recovered the original implementation.

### Central research questions

1. What discrete dynamics arise from different integrator realizations?
2. Under which conditions does iterative feedback settling converge?
3. How does finite settling accuracy affect numerical simulation?
4. Do different realizations preserve the number and character of dynamical modes?
5. What poles and zeros arise after discretization, and which are introduced by the selected numerical representation?
6. How do integration methods affect frequency, phase, damping, energy, and transient response?
7. Can two mathematically equivalent continuous systems produce different discrete behavior because of implementation topology or evaluation order?
8. Which approaches are most compatible with S7's modular causal execution model?

Distinguish mathematical facts, experimentally supported conclusions, and hypotheses.

## 2. Inspect the existing S7 implementation

Read the repository's `AGENTS.md` files and relevant architectural documentation.

In particular, examine:

- `s7l2/docs/SIMULATION_MODEL.md`
- `s7l2/docs/TRAPEZOID_INTEGRATION.md`
- `s7l2/docs/ELEMENT_DEV_GUIDE.md`
- `s7l2/docs/NUMERICAL_VALIDATION_MATRIX.md`
- `s7l2/examples/linear/`
- `s7l2/examples/pendulum/`
- `s7l2/examples/double_pendulum/`
- `s7l2/s7core/`
- `s7l2/s7tests/`

Inspect actual implementations and tests rather than relying solely on documentation.

Current S7 already provides:

- EulerIntegrator
- SymplecticIntegrator
- TrapezoidIntegrator, including implicit coupled evaluation
- A causal graph runtime
- Independent numerical tests
- Educational diagrams
- Scope and phase visualization

Reuse the existing infrastructure where appropriate.

Identify any differences between the documented mathematical method and the actual implementation.

Do not modify existing numerical semantics merely to make the experiment easier.

## 3. Documentation first

Before implementing code, create a research specification at an appropriate location, suggested:

` s7l2/docs/INTEGRATOR_FEEDBACK_RESEARCH.md `

Use the repository's actual documentation structure and conventions.

The document must specify:

- Research objectives.
- Mathematical definitions.
- Integrator state conventions.
- Feedback iteration semantics.
- The continuous reference systems.
- Discrete transition equations.
- Validation methods.
- Numerical measurements.
- Test configurations.
- Expected mathematical properties.
- Experimental limitations.
- Implementation phases and acceptance criteria.

Explicitly separate the historical hypothesis from the algorithms we can derive and implement.

The document should be sufficiently precise that another engineer could independently reproduce the experiments.

Update the relevant documentation index and cross-references.

## 4. Integration methods to investigate

Implement or reuse the following algorithms in a **headless experimental framework**.

### 4.1 Forward Euler

For

\[
\dot{x}=f(x,u)
\]

use:

\[
x_{n+1}=x_n+h f(x_n,u_n)
\]

This is the baseline.

### 4.2 Backward Euler

\[
x_{n+1}=x_n+h f(x_{n+1},u_{n+1})
\]

For linear systems:

\[
\dot{x}=Ax+Bu
\]

the exact implicit step is:

\[
x_{n+1}=(I-hA)^{-1}(x_n+hBu_{n+1})
\]

Use this as an independent reference.

### 4.3 Euler with iterative feedback settling

This is the historically motivated experimental method.

Investigate the fixed-point iteration:

\[
x_{n+1}^{(k+1)}
=x_n+h f(x_{n+1}^{(k)},u_{n+1})
\]

Keep the committed previous-step state fixed throughout the iterations.

Tentative values must not accidentally become committed physical state.

Provide configurable:

- Initial guess.
- Iteration count.
- Convergence tolerance.
- Maximum iterations.
- Absolute and relative residual criteria.
- Failure policy.
- Optional relaxation parameter.

Study both Jacobi-style simultaneous iteration and Gauss–Seidel-style sequential iteration where meaningful.

If they converge to the same unique implicit solution, their final results should agree within appropriate tolerances.

Their convergence rates and intermediate iterates may differ.

For linear simultaneous fixed-point iteration, analyze the iteration matrix \(hA\). In particular, its spectral radius determines asymptotic convergence.

Do not confuse the convergence of the feedback solver with the stability of the discrete dynamical system.

Implement clear nonconvergence detection.

### 4.4 Trapezoidal integration

\[
x_{n+1}
=x_n+\frac h2
\left[f(x_n,u_n)+f(x_{n+1},u_{n+1})\right]
\]

Reuse S7's existing implementation where possible.

Also implement an independent mathematical reference for comparison.

### 4.5 Symplectic Euler

Investigate partitioned systems where symplectic Euler or Euler–Cromer is applicable.

Explicitly document the update ordering.

Do not describe a general damped or dissipative system as symplectic merely because it uses the same C++ element.

### 4.6 Exact discrete linear reference

For autonomous linear systems, use:

\[
x_{n+1}=e^{Ah}x_n
\]

This is the exact sampled state transition, up to reference-computation precision.

For driven systems, define the input-hold assumption explicitly.

For zero-order-held inputs, use:

\[
x_{n+1}=e^{Ah}x_n+
\left(\int_0^h e^{A\tau}d\tau\right)Bu_n
\]

Do not silently compare methods using different input-sampling conventions.

For the small reference systems, prefer analytical closed-form expressions or independently verified matrix-exponential calculations.

## 5. Experimental systems

Implement three core experiments.

### Experiment A: First-order RC circuit

Use:

\[
\dot{x}=-\frac1\tau x+\frac1\tau u
\]

Start with \(R=C=1\), so \(\tau=1\).

Test charging, discharging, and a step input.

Compare:

- Exact exponential response.
- Forward Euler.
- Backward Euler.
- Iterative-feedback Euler.
- Trapezoidal integration.

Measure settling behavior, numerical error, discrete pole position, and stability.

Include tests with very different time constants and step sizes.

In particular, include configurations where backward Euler is stable but simple fixed-point iteration fails to converge.

### Experiment B: Undamped LC oscillator

Use normalized equations:

\[
\dot{x}=v
\]

\[
\dot{v}=-\omega_0^2 x
\]

Start with \(\omega_0=1\), \(x(0)=1\), and \(v(0)=0\).

Investigate:

- Eigenvalue magnitude.
- Numerical oscillation frequency.
- Phase error.
- Amplitude drift.
- Energy drift.
- Long-term behavior.
- Convergence of feedback settling.

Use the conserved energy:

\[
E=\frac12(v^2+\omega_0^2x^2)
\]

For the standard methods, independently verify their known eigenvalue mappings.

Forward Euler:

\[
z=1\pm ih\omega_0
\]

Backward Euler:

\[
z=\frac1{1\mp ih\omega_0}
\]

Trapezoidal:

\[
z=\frac{1\pm ih\omega_0/2}
        {1\mp ih\omega_0/2}
\]

Symplectic Euler should be analyzed separately using its actual state-transition matrix.

Include the expected behavior that trapezoidal integration preserves this oscillator's quadratic energy in exact arithmetic.

Do not extend that claim to arbitrary nonlinear systems.

### Experiment C: Damped RLC circuit

Use:

\[
\dot q=i
\]

\[
\dot i=\frac{u-Ri-q/C}{L}
\]

Start with:

- \(R=1\)
- \(L=1\)
- \(C=0.25\)

Test underdamped, critically damped, and overdamped configurations by changing resistance.

Measure:

- Damped natural frequencies.
- Discrete pole locations.
- Decay rates.
- Step response.
- Overshoot.
- Resonant response.
- Steady-state amplitude.
- Phase response.

Use an independent analytical reference.

For forced responses, keep the forcing and sample/hold definitions consistent across methods.

## 6. Pole-zero and structural analysis

This is one of the most important parts of the project.

For each applicable system and numerical method, derive or compute the discrete state-transition matrix.

Determine:

- Continuous eigenvalues.
- Exact sampled eigenvalues.
- Numerical discrete eigenvalues.
- Pole magnitudes.
- Pole arguments.
- Numerical damping.
- Numerical frequency.
- Stability margins.

Also determine the input-output transfer function where the input and output are explicitly defined.

Compute and report poles and zeros, including their multiplicities when practical.

Pay careful attention to the distinction between:

1. State-transition eigenvalues.
2. Actual transfer-function poles after possible cancellations.
3. Transfer-function zeros.
4. Zeros introduced by a particular integrator representation.
5. Additional persistent state variables or dynamical modes.

Do not assume that every extra transfer-function zero represents an additional dynamical mode.

Do not claim that higher-order integration necessarily introduces extra poles.

Use the book-inspired observation about extra poles and zeros as a **hypothesis to test**, not an established universal principle.

Include at least one analytical comparison of an individual forward-Euler integrator and a trapezoidal integrator in the Z-domain.

Clearly state the input/output timing convention when deriving transfer functions.

## 7. Topology and feedback experiments

Construct at least two distinct realizations of mathematically equivalent continuous linear systems.

Possible choices:

- RLC circuit and its mechanical mass–spring–damper analogue.
- Two-integrator canonical realization and an equivalent transformed state-space realization.
- A direct state-space formulation and an explicitly wired integrator/summer/gain formulation.

Compare how the discrete realizations behave under the same numerical method.

Investigate:

- Evaluation order.
- Simultaneous versus sequential feedback iteration.
- Convergence tolerance.
- Finite iteration count.
- Initial guess.
- Equivalent state-variable transformations.
- Observable input-output equivalence.

Separate effects caused by the integrator from those caused by the feedback solver and graph execution order.

Use exact algebraic results whenever possible.

The experiment must not silently insert additional unit-delay elements merely to make a feedback loop executable.

## 8. Numerical quality and precision

Investigate a useful range of normalized timestep values.

For oscillatory systems, use the dimensionless quantity:

\[
\alpha=h\omega_0
\]

For dissipative systems, use the relevant timestep-to-time-constant ratios.

Include both ordinary and deliberately challenging configurations.

Compare timestep refinement and finite feedback-iteration refinement separately.

At minimum, measure:

- Maximum absolute state error.
- Relative state error where meaningful.
- Global phase error.
- Frequency error.
- Energy error.
- Pole-location error.
- Feedback fixed-point residual.
- Iteration count.
- Nonconvergence.
- Execution cost.

For long-running experiments, measure error as a function of simulated time, not just at the final sample.

Use appropriate numerical precision and explain the limitations of floating-point arithmetic.

Start with the repository's standard binary64 path. Investigate higher precision only if it answers a specific numerical question.

Avoid arbitrary tolerances. Derive acceptance criteria from analytical expectations and documented error budgets.

## 9. Experimental architecture

Implement the laboratory as a separate research/test component.

Suggested locations:

- `s7l2/examples/integrator_feedback/`
- `s7l2/s7tests/`
- `s7l2/docs/INTEGRATOR_FEEDBACK_RESEARCH.md`

Adjust paths if repository conventions suggest a better structure.

### Important architectural restrictions

Do not modify the production S7 runtime's causal semantics.

Do not add global ODE solvers, unrestricted algebraic-loop execution, or hidden graph reevaluation.

Do not change the existing EulerIntegrator, SymplecticIntegrator, or TrapezoidIntegrator behavior merely to accommodate the experiment.

Where the current runtime supports a method, exercise the actual S7 implementation through the existing schema/runtime harness.

Where it does not, implement a clearly identified experimental reference method in the research harness.

Never present a standalone reference calculation as though it were produced by an actual S7 graph.

If supporting a method inside S7 would require new runtime semantics, document the necessary architectural extension and its tests, but do not silently implement that extension.

Preserve existing graph validation and state-publication semantics.

Use C++ and existing repository facilities. Avoid introducing heavyweight numerical dependencies unnecessarily.

Keep all computational research functional in headless builds.

## 10. Reproducible reports and visualization

Produce machine-readable experimental results.

At minimum:

- CSV traces.
- Summary tables.
- Method/configuration metadata.
- Solver convergence diagnostics.
- Numerical error statistics.

Provide a reproducible plotting procedure, preferably using existing tools such as gnuplot where appropriate.

Generate plots for:

1. Pole locations in the complex Z-plane, including the unit circle.
2. Step responses.
3. Oscillator phase portraits.
4. Energy versus simulation time.
5. Frequency and phase error versus timestep.
6. Convergence error versus feedback iteration count.
7. Error versus computational cost.

Use logarithmic axes when suitable.

Keep generated reports, images, large traces, and temporary test artifacts in build directories or ignored local research-output directories.

Do not commit generated numerical data unless a small, stable fixture is necessary for reproducibility.

Provide a single documented command or CMake/CTest workflow that reproduces the core experiment.

## 11. Educational S7 diagrams

Use the existing editable S7 diagram format.

Create or adapt a compact set of example diagrams demonstrating:

- RC feedback and settling.
- LC oscillation.
- RLC damping and resonance.
- Existing Euler, Symplectic, and Trapezoid behavior as applicable.

Reuse the existing linear laboratory where possible rather than duplicating all its diagrams.

Each educational example should state:

- Equations.
- Initial conditions.
- Parameters.
- Integrator method.
- Suggested timestep.
- Expected behavior.
- Observable signals.
- Numerical limitations.
- Suggested parameter changes.

Provide short learning tasks following:

**Predict → Construct → Simulate → Measure → Explain.**

For example, ask students to predict energy behavior before running an undamped oscillator with forward Euler.

Do not add fake feedback-settling diagrams that rely on hidden external calculations or extra delays.

If the historically motivated settling algorithm cannot yet be represented by native S7 elements, present it in the research runner and document the limitation.

Do not extend Studio GUI features just for this experiment.

## 12. Verification and automated tests

Add focused tests to the existing CMake/CTest infrastructure.

Verify:

- Correct initialization.
- State-update equations.
- Linear closed-form discrete references.
- Numerical convergence order.
- Pole positions.
- Expected oscillator damping/growth.
- Quadratic energy preservation where mathematically applicable.
- Fixed-point convergence and failure cases.
- Correct iteration stopping.
- Failure without accidental state commitment.
- Insertion/evaluation-order independence where guaranteed.
- Correct state publication and time alignment.
- Numerical finiteness and tolerance behavior.

Run appropriate existing regression tests, including the linear examples, oscillator, and trapezoidal tests.

Use the repository's documented build workflow.

Do not claim that tests passed unless they were actually executed.

Where feasible, verify independent S7 runtime output against the standalone analytical reference.

**Do not weaken existing regression thresholds to accommodate the new experiment.**

Avoid modifying existing release test expectations unless a genuine defect is independently demonstrated.

## 13. Execution plan

Work in the following phases.

### Phase 1 — Audit and mathematical specification

Inspect the repository, current numerical methods, and test framework.

Write the research document.

Derive the analytical formulas and identify which techniques are already implemented.

Record uncertainties and architectural constraints.

### Phase 2 — Independent numerical laboratory

Implement reference calculations and the candidate integration methods, including feedback settling.

Implement the RC, LC, and RLC systems.

Keep this stage independent of Qt and Studio.

### Phase 3 — Structural analysis

Implement pole-zero calculations, fixed-point convergence tests, topology comparisons, and analytical verification.

Ensure output semantics are well defined.

### Phase 4 — S7 integration

Reuse actual S7 graph execution for supported methods.

Add educational examples and appropriate existing-runtime comparisons.

Do not alter production runtime semantics without an explicit architectural decision.

### Phase 5 — Reports and tests

Implement repeatable experiment execution, CSV summaries, plots, and regression checks.

Run the experiments at multiple time resolutions.

Record measured results and limitations.

### Phase 6 — Final review

Verify documentation and results.

Explain what the experiments establish and what they do not.

Identify the most promising candidate methods and unresolved research questions.

Do not automatically implement further integration methods based on those findings.

## 14. Development discipline

Follow repository `AGENTS.md` instructions.

Check the working tree and preserve unrelated changes.

Keep patches small and logically separated.

Do not perform unrelated refactoring.

Do not change licensing, release version numbers, deployment protocols, or packaging.

Do not modify vendored code.

Do not commit temporary screenshots, plotting artifacts, generated traces, or local GUI-test files.

Keep this research separate from the ongoing 0.1.1 Studio interface work.

If a proposed change requires a fundamental modification of S7's causal execution contract, document the proposal and request an architectural decision rather than silently introducing it.

Other phases should proceed autonomously.

## 15. Completion criteria

The task is complete when:

- The mathematical research specification exists.
- All three linear systems have independent continuous references.
- Forward Euler, backward Euler, iterative-settling Euler, and trapezoidal integration have been compared.
- Symplectic Euler is included in applicable oscillator comparisons.
- Discrete poles and relevant transfer-function zeros have been analyzed.
- Feedback-settling convergence and nonconvergence are demonstrated.
- Phase, frequency, damping, energy, and state errors are measured.
- At least two equivalent system realizations have been compared.
- Reproducible test and report commands exist.
- Educational S7 examples are available for methods supported by the current runtime.
- Focused automated tests pass, or failures and their causes are documented.
- No production runtime behavior has been changed without authorization.

## 16. Final report

After implementation, provide:

1. A concise summary of implemented components.
2. The mathematical formulas used.
3. The exact files created or modified.
4. The build and test commands executed and their results.
5. A comparison table of the numerical methods.
6. Links or paths to the generated numerical reports.
7. Observed convergence and stability behavior.
8. Differences between S7 execution and independent references.
9. Limitations and unresolved questions.
10. Recommendations for the next research phase.

Distinguish theoretical predictions from measured results.

In particular, answer:

**Does Euler integration with converged feedback settling offer any structural or numerical advantage over the existing S7 integration methods?**

If the answer is no for the tested configurations, state that clearly.

Also answer:

**Which differences are intrinsic to the discrete integrator, and which arise from the method used to resolve feedback?**

Do not optimize for a preferred conclusion.

The purpose of the experiment is to obtain reliable evidence, not to confirm a historical hypothesis.
