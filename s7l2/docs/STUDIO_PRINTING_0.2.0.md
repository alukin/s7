# S7 Studio printing — mandatory 0.2.0 G4/P acceptance contract

**Maintainer decisions: 2026-11-20 (printing); 2026-11-23 (new Waterfall and basic Video Player also require it).**
Diagram printing and viewer-output printing were previously deferred,
but are now **mandatory in 0.2.0**, under G4 Studio/package qualification.
This document defines intended behavior and tests; it does **not**
claim implementation or completed evidence. See the
[0.2.0 release contract](RELEASE_0.2.0.md) and
[live work plan](RELEASE_0.2.0_WORKPLAN.md).

## Purpose and principles

The user must be able to produce legible, reproducible *engineering
documents* from the same diagrams and observations used to validate
S7 models. Both **PDF** (no physical printer required) and operating-
system **print** workflows are first-class Studio actions.

- Printing is a presentation operation: it must not modify schema,
  change numerical results, advance simulation, pause execution,
  alter observer bindings, or consume authoritative capture.
- Use one coherent, immutable visualization snapshot for each
  print job. For live viewers, freeze the view **for printing only**
  while acquisition and the running model continue.
- Preserve titles, symbols, mathematical text, element labels, wire
  geometry, axis units/ticks, legends, channel names, selected
  ranges and view-specific metadata as applicable.
- Explicitly identify run/diagram, selected range, relevant model
  rate/time and completeness/gap warnings where available; never
  represent decimated display data as a complete raw recording.
- Favor vector elements/text in PDF when supported; Qt Quick 3D
  and other GPU scenes may use a suitably high-resolution raster
  snapshot, with documented limitations and legibility checks.
- Printer capabilities depend on the operating system. A missing
  printer must not prevent PDF creation; never silently discard
  requested pages or render a blank output as success.

## Mandatory print/export matrix

| ID | Source | Minimum supported output | Evidence |
| --- | --- | --- | --- |
| **P01** | Studio functional diagram (including nested-schema displays and annotations) | PDF and printer; entire diagram and chosen area, fit-to-page, legible multi-page or explicit scaling, orientation/paper size | Correct element positions/connections, mathematical labels, text frames and no clipping |
| **P02** | Recorded Scope / saved run and visible selected channels | PDF and printer; current range and full-run selection | Axes/units/legend, signal ordering, current visible trace and recorded completeness/decimation warning |
| **P03** | Phase Portrait and related 2D observation views | PDF and printer | Correct axes, labels, curves, orientation and visible selection |
| **P04** | 3D Phase Trajectory and supported 3D scene presentations | PDF and printer with high-resolution snapshot where vector output is not practical | Correct camera/view, readable axis labels/trajectory and nonblank output |
| **P05** | Live oscilloscope Time/Spectrum/XY/Correlation modes | PDF and printer from coherent captured view | One consistent frame/range; live execution and acquisition unaffected |
| **P06** | Other currently shipped visual analysis/viewer types, including available time-frequency/surface and control-analysis views | PDF and printer using a published view-by-view support matrix | Relevant scales/color legend, units, annotations and meaningful nonblank output |
| **P07** | Print preview, page setup, cancellation and failures | Supported for diagram and each print class | Selected page geometry actually used; cancel leaves no false success; unwritable PDF/printer failure reported |
| **P08** | New 0.2.0 Waterfall viewer (committed FFT/PSD array rows) | PDF and printer of one coherent selected time-frequency view | Frequency/time axes, amplitude scale/legend, precise row range and gaps; printing does not pause or change computed values |
| **P09** | New 0.2.0 basic Video Player (generated/recorded U8 frames) | PDF and printer of an immutable selected displayed whole frame | Exact frame/step identity and geometry, image pixels/color, fit/aspect and no tearing or time advance |

The development team should audit all currently shipped visual viewer
types and maintain a manifest mapping legacy viewers to P02–P06 and the new viewers to P08–P09. A new viewer
must either implement the same printing contract or be explicitly
identified in a maintainer-reviewed release exclusion. Do not confuse
CSV/data export or screenshot tools with printable PDF/printer output.

## P08/P09 array-snapshot retention and coherence (independent-review amendment)

The independent review, [array design](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#7-independent-review-architectural-reconciliation-2026-11-23) and revised AV plan require P08/P09 to print a **successfully committed** immutable array/viewer snapshot, not an in-flight direct candidate or mutable writable buffer. Snapshot capture retains the required leases under an explicit **bounded print-job budget** independent of the graph producer's execution-critical reserve. Do not hold optional viewer frames indefinitely: cancel/close/error releases reservations. A temporarily unavailable retained snapshot must be signaled, never silently substituted with a later frame.

For Waterfall (P08), atomically capture the presented selected row range, frequency and time axis definitions, range/palette, sample-rate provenance **if known**, row/frame IDs, discontinuities and skipped-presentation markers. Missing physical sample rate cannot be printed as invented hertz. For Video Player (P09), pin the selected complete U8 grayscale or RGB frame with its exact identity, supported stride/layout, aspect/fit and any valid PTS (not graph-derived). Neither path advances the model or pauses acquisition. A cross-domain report must identify the contributing domain evaluation positions; **no global multi-rate atomicity is claimed** by P08/P09. Retained snapshots must not starve the next producer candidate.

AV17a/b covers **P08/P09 integration** and package assets. **P01–P07 remain a separate mandatory G4/P workstream.** AV18c requires the complete P01–P09 candidate-specific matrix. Validate PDF page content, rendered pixels and axes, failed and cancelled jobs, print lifecycle, visible paper content and optional device output. Count Qt/media raster materialization as a copy or transfer. See the [array design register](ARRAY_SIGNALS_ZERO_COPY_DESIGN.md#open-decision-register--explicit-maintainer-approval-required) for decision status.

## Layout and behavior

- Studio exposes **Export as PDF**, **Print**, and appropriate page/
  printer settings from the diagram and individual viewer windows.
  Usable preview is preferred when supported by the platform.
- Fit-to-page and selected region should be deterministic; avoid
  rendering at current on-screen zoom so small labels become illegible.
- Page margins, paper size and orientation must be respected, with
  visible content bounds; large diagrams need an explicit fit/tile
  policy (do not silently truncate).
- Recorded and live viewers should state the print range or snapshot
  position. Snapshot rendering must use a stable view and must not
  mutate the recording or control a physical device.

## Acceptance tests and evidence

1. **Automated PDF output:** create representative saved diagrams
   (including large/nested and text-rich models), 2D plots, 3D views,
   oscilloscope modes and currently supported analysis views. Check
   file existence, nonzero length, expected page count/size/content
   where inspectable, and include rendered-page visual comparison
   for legibility and clipping. A successful API call with blank
   output is a failure.
2. **Layout matrix:** portrait/landscape, A4/Letter where supported,
   small/large diagrams, fit/region/tiling and high DPI.
   Include non-ASCII text, mathematical symbols and varied axis units.
3. **Snapshot correctness:** print live views while a known model
   progresses. Verify internally consistent time/step position and
   that model step count, authoritative recording and live acquisition
   were unaffected.
4. **Negative paths:** cancel print dialog, invalid/unwritable PDF
   path, print backend unavailable, stale/closed viewer, resize,
   missing Qt Quick 3D rendering and very large input. Report clear
   failure/unsupported behavior, no crash, no false success.
5. **Package/platform:** exercise in installed/relocated Linux
   Studio, and targeted supported Windows/Wine build checks.
   Attribute tests honestly: Wine rendering is not native Windows
   or actual printer hardware evidence.
6. **Physical printer:** opt-in manual verification on a named real
   device and platform, with printed-page inspection (geometry,
   labels, contrast and scaling). No physical printer required
   in normal CI; document gaps or explicit approved exclusions.

Keep a test matrix with IDs P01–P09, test commands, platform/Qt
renderer, candidate commit SHA, expected vs. actual, PDF artifacts
or appropriately retained check evidence, results and limitations.

## Release decision and exclusions

All P01–P09 supported profiles and documented mandatory checks
are G4 release requirements for **0.2.0**, not 0.3.0 candidates.
Printer-specific capability gaps must be clearly identified and
approved by the maintainer; silently skipping required viewers is
not acceptable. The Studio signal generator, general video I/O
and new hardware drivers remain outside this printing gate.
