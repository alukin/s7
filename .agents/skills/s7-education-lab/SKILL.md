---
name: s7-education-lab
description: Build or qualify S7 teaching labs with an original diagram, independent oracle, student exercises and Studio evidence.
---

# S7 teaching laboratory

Use for MIT-aligned EE, SDR and audio teaching labs, and for verifying their student workflows.

1. Read `AGENTS.md`, `s7l2/education/README.md`, `LAB_STANDARD.md`, `IMPLEMENTATION_PLAN.md`, `SOURCES.md` and the relevant curriculum track. Existing examples are references, not automatically qualified labs.
2. Identify the lab ID, exact original source/topic alignment, learning objectives, dependencies, status and intended platform. Do not imply MIT affiliation or reproduce MIT homework/figures. Attribute the original educational sources precisely.
3. Define the mathematics: equations/units, initial state, rates, timing convention, sample count, inputs and prediction **before executing**. Include a controlled failure or boundary case.
4. Create the **canonical editable saved diagram** under the appropriate `s7l2/education/labs/<ID>-<slug>/` directory using `s7-diagram-drawing` (or edit with `s7-diagram-editing`). Never claim a new lab merely by linking an existing example or duplicating a picture.
5. Execute that exact saved diagram headlessly and compare to an independently implemented oracle, with explicit tolerances. Include negative/error-path checks; independently inspect the Studio open/run/observe/edit/rerun experience using `s7-visual-validation`.
6. Write a student-facing README with prerequisites, outcomes, sources/rights, equations, prediction, construction, precise run/measurement instructions, independent verification, failure experiment, extension and limitations.
7. Update the lab's evidence/status only with the commands, results, commit SHA and review evidence. `headless-verified`, `studio-verified` and `release-qualified` are distinct; release qualification requires final candidate/package checks.

Report actual delivered materials, lab status, independent oracle, visible UI validation and remaining gates. Do not silently promote a planned lab.
