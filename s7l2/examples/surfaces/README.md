# Real surface gallery

Open a diagram, run at **1 kHz for 5 model seconds**, then double-click `surface`
(or open it while running). Select Surface, Wireframe or Heatmap. Drag the 3D
view to orbit, use the wheel to zoom and XY/XZ/YZ for projections. Hover a
heatmap cell or choose an explicit row/column to inspect original values.
Colors indicate height; the legend shows the actual range. Axes are scaled
independently into the scene cube. These mathematical coordinates and heights
are dimensionless; no physical unit is inferred from their names.

| Diagram | Function/domain | Expected features/reference points |
|---|---|---|
| `plane.s7diagram` | x+2y, [-2,2]², 65×49 | Asymmetric slope; (-2,-2)=-6, (0,0)=0, (2,2)=6 |
| `bowl.s7diagram` | x²+y², [-2,2]² | Minimum 0 at origin; corners 8; circular contours |
| `saddle.s7diagram` | x²−y², [-2,2]² | Positive along X, negative along Y; zero diagonals |
| `gaussian.s7diagram` | exp(−x²−y²), [-3,3]² | Peak 1 at origin; (1,0)=exp(-1) |
| `wave.s7diagram` | sin(x) cos(y), [-π,π]² | Alternating peaks/valleys; zeros where either factor is zero |
| `sinc.s7diagram` | sin(r)/r, [-10,10]², 64×64 | Circular rings, alternating lobes. Even coordinate counts exclude r=0; analytic extension there is 1 |

`GridScan` emits one cell per logical evaluation, X fastest. Its six outputs
are X, Y, cell index, columns, rows and active. The surface observer also reads
the function height at the same step. This is an explicitly indexed scan,
not triangulation based on arrival order. After the scan active becomes zero.
Missing/nonfinite cells remain holes and cannot generate triangles across them.
A partial scan is labelled partial. Close/reopen replays recorded source data;
Clear display does not delete it. Raw CSV exports include grid identities.

Functions are editable schema elements in the standard library, composed from
ordinary arithmetic and function blocks. No global simulation solver is used.
Run `python3 examples/surfaces/generate_examples.py` from the repository to
reproduce both real/complex galleries and their schema definitions. Numerical
references live separately in `s7studio/tests/surface_test.cpp`.

Fixed ranges, equal numeric aspect, heatmap contours and result Save/Open are
available. Fixed height ranges also fix real-value colors; values outside a
fixed 3D range are clipped. The portable `.s7analysis` format preserves axes,
complex values, masks and provenance. Result CSV can also be opened as a grid;
raw capture CSV is a separate source-record format.
