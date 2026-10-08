# Control protocol fixtures

`control_v1.json` is the conformance script of the S7 control protocol,
version 1 (`../doc/S7_Control_Protocol_MVP.md`, section 9). It describes one
node (`node`) and the peers requests come from (`peers`), then gives `steps`:
each a request and what its response must hold.

To check an implementation, start a node as `node` describes (its node UUID,
listen URL, `max_configuration_bytes`, a synchronized clock, and one consumer
stream that faulted with `UNDERRUN`), authorize the Studio certificate
`s7runtime/examples/certs/s7_studio.cert.pem`, and send the steps in order:

- replace each `"${name}"` in a request with the value captured earlier
  (`capture: {name: member}` of a previous step);
- the response must have the step's `type`, every member listed in `fields`
  (paths such as `a.b` and `a[0].b`) with the given JSON type (`string`,
  `integer`, `boolean`, `object`, `array`, `null`, or `x|y` for either), and
  the exact values of `equals`. Other members may be present.

The configuration digests in the requests are RFC 8785 (JCS) SHA-256 digests
computed by `tools/gen_control_fixtures.py`, which writes the file; a node
that canonicalizes differently refuses the prepare steps.
`s7runtime_control_fixtures_smoke` runs the script against the C++ control
plane.
