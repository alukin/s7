---
name: s7-education-lab
description: Build or qualify S7 teaching labs with an original diagram, independent oracle, student exercises and Studio evidence.
---

# S7 teaching laboratory

Use for MIT-aligned EE, SDR and audio teaching labs, and for verifying their student workflows.

1. Read root `AGENTS.md` and the education `README.md`, `COURSE_MAP.md`, `LAB_STANDARD.md`, `IMPLEMENTATION_PLAN.md` and `SOURCES.md` under `s7l2/education/`. For audio or SDR, also read the relevant track. Check `s7l2/education/EVIDENCE.md` for existing runs and qualification.
2. Identify the lab ID, exact original source/topic alignment, learning objectives, dependencies, status and intended platform. Do not imply MIT affiliation or reproduce MIT homework/figures. Attribute the original educational sources precisely.
3. Define the mathematics: equations/units, initial state, rates, timing convention, sample count, inputs and prediction **before executing**. Include a controlled failure or boundary case.
4. Create the **canonical editable saved diagram** under the appropriate `s7l2/education/labs/<ID>-<slug>/` directory using `s7-diagram-drawing` (or edit with `s7-diagram-editing`). Never claim a new lab merely by linking an existing example or duplicating a picture.
5. For headless qualification, execute the exact saved graph, compare it to an independent oracle with explicit tolerances and run the negative or boundary check. For Studio qualification, also exercise the intended open/run/observe/edit/rerun workflow on the stated platform.
6. Use the student guide structure in `LAB_STANDARD.md`. Identify the saved run settings, named signals and measurement procedure students need to reproduce the prediction and failure experiment.
7. Record applicable evidence with the command, SHA, result and review in `s7l2/education/EVIDENCE.md`. Assign the supported status using `LAB_STANDARD.md`. Release qualification requires reproducing the required evidence on the final tagged source or artifacts and linking it in the release matrix.

Report actual delivered materials, lab status, independent oracle, visible UI validation and remaining gates. Do not silently promote a planned lab.
