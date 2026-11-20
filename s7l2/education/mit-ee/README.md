# MIT-aligned Electrical Engineering — course landing page

**Target course path:** `s7l2/education/mit-ee/`.
This is the *existing* MIT-aligned signals, systems, DSP, modeling and
control teaching collection (FN, SP, CT, IN), **not the new SDR curriculum**.

## Transitional source navigation (2026-11-20)

The current **24 developed core teaching diagrams and guides** still live in
[`../labs/`](../labs/) with their generators in
[`../generators/`](../generators/). Their canonical
[student index](../index.md), [MIT-aligned course map](../COURSE_MAP.md),
[MIT sources](../SOURCES.md), [implementation plan](../IMPLEMENTATION_PLAN.md),
[core implementation manifest](../CORE_MANIFEST.md) and
[development evidence](../EVIDENCE.md) retain their working historical
paths in this planning branch.

This document provides the new **`mit-ee` name** without misrepresenting
the old course files as already migrated. Physical relocation into
`mit-ee/labs/` and `mit-ee/generators/` is a **separate mechanical
refactoring**, with full link/test/package compatibility checks before
the old paths are removed. Do not relocate just the Markdown files while
leaving broken saved diagrams, Qt help resources or tests.

Shared cross-course verification rules remain in
[`../LAB_STANDARD.md`](../LAB_STANDARD.md). The sibling new curriculum
is [MIT-aligned SDR and Digital Communications](../mit-sdr/README.md).

## Required migration acceptance (one coherent PR)

1. Move all old FN/SP/CT/IN guides, diagrams, course records and generators
   into the new `mit-ee` tree; preserve stable lab IDs and content.
2. Update generated diagram resource links, relative Markdown links,
   Studio help topics and diagram launch actions, CMake tests, packages,
   top-level documentation, teacher assets validators and scripts.
3. Update the existing `AGENTS.md` educational document entry points and
   cross-course index. Keep the shared lab standard at `education/`.
4. Test **all 24 saved diagrams** through production loader/headless
   reference suites, exercise generated diagrams, then verify Studio
   open -> run -> view and the installed/relocated help links.
5. Check package contents and absence of both old dead paths and duplicate
   stale guides. Record exact test commands and commit SHA.
6. Preserve links for existing installations where practical, or publish
   an explicit path migration note; do not break old exercise IDs.

This landing document is *not* an assertion that the migration has
already been implemented or accepted.
