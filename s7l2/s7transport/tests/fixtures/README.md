# Wire format fixtures

`wire_v1.json` is the conformance data for the S7 data stream wire format,
version 1 (`docs/S7_Network_Wire_Protocol_v1.md`). Any implementation, in any
language, should pass all of it:

| Section | An implementation must |
|---|---|
| `data` | decode `hex` to the given `type`, `stream_alias`, `sequence`, `timestamps` and `values`, and encode those fields to exactly `hex` |
| `data_ignoring_extensions` | decode `hex`, ignoring the unknown noncritical extension |
| `control` | decode and encode the `ACK` / `RESUME` / `RESUME_NO_STATE` messages |
| `invalid` | reject `hex` (`reason` says why) |
| `descriptors` | read the stream descriptor to `canonical` (every optional member at its value or default) |
| `invalid_descriptors` | refuse the descriptor |

Values: integers beyond 53 bits are decimal strings; complex values are
`[real, imaginary]`; floating values are exactly representable in binary64.
Every type of section 8 appears at least once.

The file is written by `tools/gen_wire_fixtures.py`, an encoder written from
the specification. Two decoders check it: the C++ codec
(`s7transport_wire_format_smoke`, and `s7net_diagram_deployment_smoke` for
descriptors) and `tools/check_wire_fixtures.py`, a short Python decoder
written from the specification that serves as the reference for decoders in
other languages. `s7transport_wire_fixtures_current` checks that the file is
what the generator writes.

Packets with `send_time_ns` carry the noncritical `SEND_TIME` extension
(section 7.2); `null` means none.
