# Integrator review — 2026-11-17

The review preserves S7's local-block simulation model. The maintainer chose
a separate local backward-Euler element and explicit feedback realizations,
without adding a backward-Euler graph solver. The existing forward-Euler
recurrence, causal-memory publication and graph evaluation order are unchanged.

The [teaching guide](INTEGRATORS.md) contains the mathematical derivations,
original plots and delay diagrams. The [reference record](INTEGRATOR_REFERENCES.md)
identifies the MIT/Siebert sections and visual page inspection. Siebert's book
is recorded as the maintainer's original inspiration for S7; this work does
not claim to have read its entire text.

## Confirmed defects and changes

| Finding | Correction and evidence |
| --- | --- |
| Implicit trapezoid accepted inaccurate small-signal endpoints because of `8 epsilon (1+abs(candidate))` | Native component-relative checks replace the unit-sized absolute floor. The newly added amplitude-scaling regression failed before the change, then passed. Real/complex F16–F128 cases include F128 amplitudes near `2⁻²⁰⁰⁰`. |
| Summing finite endpoint samples could overflow although their weighted area was finite | Endpoint summation checks sign/magnitude before addition and scales the terms separately when required. Complex components are treated independently. A `1e308` input at `h=.001` gives the expected finite sampled area without raising `FE_OVERFLOW`. |
| A complex norm could overflow even though the actual state components and their updates were representable | Convergence checks each component without forming the vector norm; large-component differences are normalized before subtraction. A C64 decay with both initial components `1.5e308` matches the independent `15/17` one-step factor. |
| Several local integrator constructors/transitions accepted invalid timesteps, initial states or nonfinite results | Euler, backward Euler, trapezoid and symplectic integration validate the applicable values. Failed local updates preserve the pre-update output. Both symplectic candidates are checked before either is assigned. |

Implementation: `s7core/include/s7/elements.hpp`, especially
`detail::trapezoid_increment`, `TrapezoidIntegratorT::prepare_trapezoid_correction`
and the local constructors/transitions. Tests:
`s7core/tests/trapezoid_smoke.cpp` and `s7core/tests/integrators_smoke.cpp`.

## Separate Euler elements and feedback evidence

`EulerIntegrator` is displayed as **Forward Euler Integrator**.
`BackwardEulerIntegrator` is a separately registered, typed local element.
Its first successful call preserves prescribed `y[0]`; subsequent calls use
the current derivative sample. Its direct feedthrough is explicit, and an
unreduced algebraic cycle is rejected. Diagram instances follow their resolved
domain rate. Native real/complex startup and quadrature are tested separately.

Actual compiled delay/adder graphs implement independently derived forward,
backward and trapezoidal RC/oscillator recurrences. Tests compare their states
with independent modal solutions and closed forms, reverse element insertion
order, and check growth, damping or the relevant quadratic invariant. The
explicit realizations also exercise steps outside the existing trapezoid
iteration's convergence envelope.

MIT terminology is retained: the notes' §6.3 **Leapfrog** recurrence is tested
in its written position-first order, including its modified invariant and
the failure of bounded oscillation at `h=2` and above. It is distinguished
by equation/source from Lecture 7's **Trapezoidal rule**. S7's existing
velocity-first symplectic element is not silently relabelled or reordered.

## Help and visual verification

Studio Help contains **Integrators, delays, and s/z mappings**, **Integrator
references: Siebert and MIT**, and the trapezoid/coupled implementation guides.
The explanation covers prescribed initial values, unilateral-transform terms,
exact sampling, the numerical pole maps, stability regions, frequency warping
and the difference between stability, accuracy, physical energy and solver
convergence. Primary PDFs open through direct MIT links. Original explanations,
figures and references are embedded for offline use; textbook scans are not
redistributed.

The Qt/Xvfb test checks searchable references, article text after images,
relative source links and all three embedded image resources. Captured help
screenshots were visually inspected. This check caught and corrected Qt
Markdown's loss of prose after raw HTML image tags; standard Markdown image
syntax retains the complete article.

## Verification

Host: Fedora 45, GCC 16.2.1, Qt 6.11.2. Final results:

| Check | Result |
| --- | --- |
| Complete headless Debug `build-tests` and CTest suite | 74/74 passed |
| Fully instrumented core, integrator/trapezoid/rollback checks with ASan, UBSan and leak detection | 3/3 passed |
| Release Studio and GUI workflow build | Passed |
| Isolated Linux Xvfb `helpAndAbout` workflow | Passed, no skips |
| Figure inspection, article local/resource links, metadata uniqueness, Python syntax and whitespace checks | Passed |

Commands from `s7l2`:

```sh
cmake -S . -B /tmp/s7-hardware-build -DS7_BUILD_STUDIO=OFF \
  -DS7_PORTAUDIO=ON -DS7_PIPEWIRE=ON -DS7_SOAPYSDR=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/s7-hardware-build --target build-tests --parallel 3
ctest --test-dir /tmp/s7-hardware-build --output-on-failure --parallel 3

cmake -S . -B /tmp/s7-integrator-review/asan -DS7_BUILD_STUDIO=OFF \
  -DS7_PORTAUDIO=OFF -DS7_PIPEWIRE=OFF -DS7_SOAPYSDR=OFF -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/s7-integrator-review/asan --target s7core_integrators_smoke \
  s7core_trapezoid_smoke s7core_step_rollback_smoke --parallel 1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/s7-integrator-review/asan \
  -R '^s7core_(integrators|trapezoid|step_rollback)_smoke$' --output-on-failure

cmake --build build-release --target s7studio_gui_workflows_test s7studio \
  s7studio_gui_capture --parallel 3
python3 tools/run_gui_test.py --artifacts /tmp/s7-integrator-review/gui-final \
  --capture build-release/s7studio/s7studio_gui_capture --timeout 60 \
  -- build-release/s7studio/s7studio_gui_workflows_test helpAndAbout
MPLCONFIGDIR=/tmp/s7-integrator-review/mpl python3 -B tools/plot_integrator_help.py
```

The sanitizer build was retried serially after a parallel compiler process
was killed under memory pressure. An earlier interrupted link also left one
invalid test binary; it was relinked, and the final complete 74-test run passed.
Raw logs and screenshots are under `/tmp/s7-integrator-review` and are temporary
host artifacts. Normal tests opened no physical audio/RF devices.

## Remaining limits

The existing implicit trapezoid stage still has a 64-iteration fixed-point
solver. Its contraction requirement is stricter than the trapezoidal method's
linear stability region. A valid mathematical endpoint may therefore produce
an explicit nonconvergence error. This review fixes numerical correctness
defects; it does not add Newton iteration or claim arbitrary stiff/nonlinear
feedback support. The explicit linear realizations in the guide avoid that
iteration and are tested as such.

Native Windows/macOS execution and a full interactive simulation usability
matrix were not qualified by this pass. Higher precision, a small residual,
or bounded energy alone is not a physical-model accuracy guarantee.

## Diagram symbol follow-up

After the integrator checkpoint was committed and pushed, Studio gained
mathematical block shapes. [Basic block symbols](DIAGRAM_SYMBOLS.md) records
the equations and sources. In particular, Siebert's illustrated accumulator
uses earlier inputs, whereas S7's existing accumulator includes the current
input. The shape change preserves S7's recurrence and labels that distinction.
Rounded binary64 coefficient previews carry `≈`; only exact unit weights
become `+` or `−`. Product input factors are shown, and integrator method codes
remain visible at minimum size. The velocity-first two-state update is marked
`SE`, with position and velocity outputs.

The rendering/geometry and metadata CTests passed (2/2), as did the existing
Studio edit and bundled-demo checks (2/2). Six selected Xvfb GUI workflows passed
without skips (8 QtTest cases including initialization and cleanup). These cover
Help/resources, library/ports, parameter editing, save/reopen, typed keyboard
editing, edit integrity and nested clipboard/bends. The complete headless suite
and sanitizer results above belong to the preceding integrator checkpoint;
this follow-up changes presentation and metadata, not runtime execution.

```
cmake --build build-release --target s7studio_diagram_test \
  s7studio_gui_workflows_test s7studio s7core_metadata_json_smoke --parallel 2
ctest --test-dir build-release \
  -R '^(s7studio_diagram|s7core_metadata_json_smoke)$' --output-on-failure
ctest --test-dir build-release \
  -R '^(s7studio_edit_check|s7studio_bundled_demo)$' --output-on-failure
python3 tools/run_gui_test.py --artifacts /tmp/s7-integrator-review/symbols-final-gui \
  --capture build-release/s7studio/s7studio_gui_capture --timeout 150 \
  -- build-release/s7studio/s7studio_gui_workflows_test helpAndAbout \
  frameElementsLibraryAndPorts editParametersSaveOpen typedParametersAndKeyboard \
  editingIntegrity clipboardNestedAndBends
```

The actual node renderer produced both original gallery images embedded in
Help; normal and minimum sizes and the Help screenshot were visually inspected.
To regenerate the galleries after a symbol change:

```
S7_SYMBOL_GALLERY=/tmp/basic_symbols.png QT_QPA_PLATFORM=offscreen \
  build-release/s7studio/s7studio_diagram_test
```

The compact gallery is written to `/tmp/basic_symbols.png.compact.png`.
