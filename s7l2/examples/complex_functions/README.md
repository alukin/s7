# Complex function surfaces

Run at **1 kHz for 5 model seconds** and open `surface`. Inputs form the plane
z=x+iy over [-2,2]². Default height is |f(z)| and cyclic hue encodes phase
(radians). Select Re(w), Im(w), or log(1+|w|) as height, and Surface/Wireframe/
Heatmap as presentation. Zero has undefined phase and is gray. Missing or
nonfinite cells are holes. The phase legend wraps smoothly at ±π.

| Diagram | Reference behaviour |
|---|---|
| `identity.s7diagram` | f(z)=z: one phase winding about zero, conical magnitude |
| `square.s7diagram` | f(z)=z²: two phase windings; |w|=x²+y² |
| `inverse.s7diagram` | f(z)=1/z: magnitude rises near zero, opposite phase winding |
| `rational.s7diagram` | f(z)=(z−1)/(z+1): zero at 1 and pole at −1 |

The inverse/rational examples use even 64×64 coordinate counts to avoid
executing division by zero. They sample near, rather than at, their singular
points. Zoom/log height exposes the landscape around them; the displayed
finite peak is not a bound on the mathematical function. Identity/square use
65×65 coordinates and include their exact zero.

Function values are calculated in reusable, editable F64 schema graphs with
explicit real/imaginary outputs; phase/magnitude calculation is presentation.
Studio does not silently coerce complex ports into real samples. Native C64
outputs can be observed using the existing `ComplexToReIm` element.

See `../surfaces/README.md` for controls, recording semantics, regeneration and
current limits. `s7studio_surface` checks every sampled value against independent
complex arithmetic, not the production schema formulas.
