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
   or actual printer hardware evidence. Windows printing is not a
   0.2.0 gate (see the Windows note under the test matrix).
6. **Physical printer:** opt-in manual verification on a named real
   device and platform, with printed-page inspection (geometry,
   labels, contrast and scaling). No physical printer required
   in normal CI; document gaps or explicit approved exclusions.

Keep a test matrix with IDs P01–P09, test commands, platform/Qt
renderer, candidate commit SHA, expected vs. actual, PDF artifacts
or appropriately retained check evidence, results and limitations.

## Implementation status

Implemented in stages; the matrix stays open until every row has evidence.

| ID | Status | Implementation | Automated evidence |
| --- | --- | --- | --- |
| **P01** | Implemented (Linux) | `src/diagram_print.*`, `src/diagram_print_dialog.*`; File → Print (preview first) / Export as PDF / Page Setup | `s7studio_diagram_print`, `s7studio_gui_workflows` (`printDiagramToPdf`) |
| **P02** | Implemented (Linux) | Scope: visible range or whole run, redrawn from the recording at print resolution with decimation and completeness notes | `s7studio_gui_workflows` (`printViewers`) |
| **P03** | Implemented (Linux) | Phase Portrait: the curve as shown | `s7studio_plot_print` (renderer) |
| **P04** | Implemented (Linux, Xvfb/llvmpipe) | 3D Phase Trajectory, Surface and the 3D tabs of Response/Time–Frequency print as an image of the Qt Quick 3D scene re-rendered at 3× its on-screen size; a blank capture is refused | `s7studio_gui_workflows` (`print3DTrajectory`) |
| **P05** | Implemented (Linux) | Live Oscilloscope: the current tab (Time, Spectrum, Correlation, XY), snapshot time and acquisition state; Freeze picks the frame | `s7studio_gui_workflows` (`printViewers`) |
| **P06** | Implemented (Linux) | Frequency Response (all tabs), Time–Frequency (map, trace, slice), Real/Complex Surface, Live Spectrum (vector analyzer picture on white paper) | `s7studio_plot_print`, `s7studio_live_spectrum_viewer` |
| **P07** | Diagram and all viewers | `src/printing/preview_dialog.*`: every print previews first; PDF export skips the preview; page setup, cancel and failure results | as P01, P02 |
| **P08/P09** | Implemented (Linux) | Waterfall and Video Player on the shared page layer (`viewers/viewer_print.*`): icon print button, preview first, Pause display as freeze; Waterfall row identity and gaps paginate as an appendix | `s7studio_array_persistence`, `s7studio_array_persistence_spectral` |

**Viewer plots** (`src/viewers/plot_print.*`, `viewer_print.*`): a
`PlotSnapshot` copies a `LinePlotView`'s visible series, ranges, titles,
legend, cursor, oscilloscope divisions and channel axis when the user prints;
`paintPlot` redraws it as vectors on white paper with the screen's tick rules
(LinePlot.qml), darkening pale trace colours and dashing comparison overlays.
Each viewer has an icon-only print button (click: preview; arrow: Save as PDF,
Page Setup) and, where it updates during a run, a freeze button; printing never
pauses acquisition. `s7studio_plot_print` checks tick steps, colour darkening,
the copied snapshot (and that later view changes leave it alone), painting
inside its area only, the viewer PDF text (title, notes, axis titles, legend,
tick labels) and the preview window in both modes.

**3D views** (`captureVisibleViews`): Qt Quick scenes outside line plots
are captured with `QQuickItem::grabToImage` at three times their on-screen
size (falling back to the widget framebuffer) and drawn aspect-correct with
their size noted in the header. A capture that comes back a single colour is
reported as an error rather than printed blank. Vector output for 3D scenes
is not attempted.

**Live Spectrum** paints its analyzer through one painter for screen and
paper: on paper the background is white, LED colours are darkened and unlit
segments are a faint tint. `s7studio_live_spectrum_viewer` checks that freeze
holds the picture while samples arrive and that the paper picture is white
with lit LEDs.

Shared page layer (`src/printing/print_document.*`): every page has a
provenance header (title, file, level or range, area and scale) and a footer
(Studio version and revision, print time, page n of m), and the content is
clipped to the area between them. Jobs are built for the page layout actually
used (PDF writer, chosen printer or preview printer).

**Coordinate system.** Pages paint in 1/96 inch (the scene unit) on a device
set to 96 dpi. Diagram items draw text with point-size fonts, which a painter
resolves against the device resolution; measured on a 300 and 600 dpi device,
such text grew relative to the drawing. `setResolution(96)` changes only the
coordinate system: PDF output stays vector and printers print at their own
resolution. A device that does not accept 96 dpi is refused with an error
instead of printing distorted text.

**P01 checks** (`s7studio_diagram_print`, on `double_pendulum_full.s7diagram`:
custom 490 × 320 mm sheet, four nested subsystems, Greek and math text):
building a job leaves the scene and undo stack unchanged; the default paper is
the diagram sheet; one fit page whose PDF MediaBox is the sheet; every element
lies inside the printed region and is drawn at its mapped position inside the
content area; `pdftotext` finds the title, footer and `δ = θ1 − θ2` as text
(skipped when poppler is absent); five Letter portrait pages with subsystems,
none blank; the same through a PDF `QPrinter` and through `QPrintPreviewWidget`;
tile count at 200 % on A4 matches an independent count; a selection prints as
"Selection"; a later edit leaves a built job's page unchanged; an empty job, an
unwritable path and a 300 dpi device fail without a file or false success.

### Test matrix (Linux, development checkpoint)

Commands run from the build directory; GUI suites through
`tools/run_gui_test.py` on a private Xvfb display (Mesa llvmpipe OpenGL).
Record the candidate SHA, Qt version and platform when re-running on the
release candidate.

| ID | Automated check | Command | Checked |
| --- | --- | --- | --- |
| P01 | page count/size, element positions, PDF text incl. Greek/math, subsystems, tiling, selection, snapshot isolation; File menu PDF and preview | `ctest -R s7studio_diagram_print`; `s7studio_gui_workflows_test printDiagramToPdf` | PDF pages and rendered PNGs reviewed |
| P02 | Scope visible range and whole run, decimation/completeness notes, axis titles | `s7studio_gui_workflows_test printViewers` | PDF reviewed |
| P03 | plot snapshot, tick rules, colours, painting bounds | `ctest -R s7studio_plot_print` | PNG reviewed |
| P04 | 3D trajectory PDF holds an embedded image of stated size | `s7studio_gui_workflows_test print3DTrajectory` | PDF reviewed |
| P05 | live oscilloscope preview of the current tab with acquisition state | `s7studio_gui_workflows_test printViewers` | preview reviewed |
| P06 | Live Spectrum freeze and paper picture; analysis views via the shared capture | `ctest -R s7studio_live_spectrum_viewer` | — |
| P07 | preview in both modes, cancelled file dialog, unwritable path, 300 dpi device refusal, printer path | `ctest -R "s7studio_(diagram\|plot)_print"` | — |
| P08/P09 | Waterfall and Video PDFs through the print button, cancel, appendix pagination, printer path | `ctest -R s7studio_array_persistence` | — |

Not yet covered: physical printer output, Windows runs and installed
package checks (acceptance items 5 and 6), and printing of the Surface,
Response and Time–Frequency 3D tabs beyond the shared capture path exercised
by the trajectory.

**Windows (maintainer decision, 2026-11-28):** Windows builds use the same
printing code and must keep building and working, but Windows printing is
**not a 0.2.0 release gate**: the maintainer has no Windows host. Report it
as untested on Windows unless someone runs the checklist below on a real
Windows machine (teren_ukr may). Wine results do not count as Windows
evidence.

Windows checklist (installed or relocated Studio, record Windows version,
printer driver and Studio revision):

1. Open `examples/double_pendulum_full.s7diagram` (or any saved diagram
   with subsystems). File ▸ Print… shows the preview; switch Fit/Tile,
   Selection and Include subsystems and check that the page count and
   detail line change. Print to "Microsoft Print to PDF" and to one real
   printer if available.
2. File ▸ Export as PDF…: the file opens in a PDF reader with the diagram
   sheet's size and orientation, readable Greek/maths labels and the footer
   ("S7 Studio <version> … printed …, Page n of m").
3. Run a model, open Scope, Phase Portrait, 3D Phase Trajectory and Live
   Oscilloscope; freeze each, use its printer button (preview, then Print)
   and its arrow menu (Save as PDF…, Page Setup…). Page Setup choices must
   be remembered for the next viewer print.
4. Cancel each dialog once (preview Close, file dialog Cancel): nothing is
   written and the status bar says it was cancelled. Save to a read-only
   folder: a clear error, no file.
5. Check high-DPI scaling (125 % / 150 % display scale): text and line
   widths on the PDF page match the 100 % result.

## Release decision and exclusions

All P01–P09 supported profiles and documented mandatory checks
are G4 release requirements for **0.2.0**, not 0.3.0 candidates.
Printer-specific capability gaps must be clearly identified and
approved by the maintainer; silently skipping required viewers is
not acceptable. The Studio signal generator, general video I/O
and new hardware drivers remain outside this printing gate.
