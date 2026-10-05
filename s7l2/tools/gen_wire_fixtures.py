#!/usr/bin/env python3
"""Byte-exact fixtures for the S7 data stream wire format, version 1.

An independent reference implementation of S7_Network_Wire_Protocol_v1.md
(sections 7, 8 and 12.1), written from the specification rather than from the
C++ code. It writes s7transport/tests/fixtures/wire_v1.json; every
implementation must encode the valid cases to exactly these bytes, decode
them back to the same fields, and reject the invalid ones.

Usage: tools/gen_wire_fixtures.py [output.json]
"""
import json
import struct
import sys
import uuid
from pathlib import Path

TYPE_IDS = {
    "I8": 0x0001, "U8": 0x0002, "I12": 0x0003, "U12": 0x0004, "I16": 0x0005, "U16": 0x0006,
    "I32": 0x0007, "U32": 0x0008, "I64": 0x0009, "U64": 0x000A, "I128": 0x000B, "U128": 0x000C,
    "F16": 0x000D, "F32": 0x000E, "F64": 0x000F, "F128": 0x0010,
    "C16": 0x0101, "C32": 0x0102, "C64": 0x0103, "C128": 0x0104,
    "CI8": 0x0201, "CU8": 0x0202, "CI16": 0x0203, "CI32": 0x0204, "CI64": 0x0205, "CI128": 0x0206,
}
COMPONENT = {"C16": "F16", "C32": "F32", "C64": "F64", "C128": "F128", "CI8": "I8", "CU8": "U8",
             "CI16": "I16", "CI32": "I32", "CI64": "I64", "CI128": "I128"}
INT_BITS = {"I8": 8, "U8": 8, "I16": 16, "U16": 16, "I32": 32, "U32": 32, "I64": 64, "U64": 64,
            "I128": 128, "U128": 128}
ROLES = {"MODEL_TIME": 1, "SOURCE_TIME": 2, "PROCESSING_TIME": 3}


def binary128(value):
    """IEEE 754 binary128 bits of a float that a double holds exactly."""
    if value == 0:
        return 0
    sign = 1 if value < 0 else 0
    mantissa, exponent = abs(value).hex()[2:].split("p")  # '1.8000000000000p+1'
    whole, fraction = (mantissa.split(".") + [""])[:2]
    assert whole == "1"
    frac_bits = int(fraction or "0", 16) << (112 - 4 * len(fraction or "0"))
    return (sign << 127) | ((int(exponent) + 16383) << 112) | frac_bits


def component_bytes(component, value):
    if component in INT_BITS:
        bits = INT_BITS[component]
        return (int(value) % (1 << bits)).to_bytes(bits // 8, "little")
    if component == "F16":
        return struct.pack("<e", value)
    if component == "F32":
        return struct.pack("<f", value)
    if component == "F64":
        return struct.pack("<d", value)
    if component == "F128":
        return binary128(value).to_bytes(16, "little")
    raise ValueError(component)


def payload(type_name, values):
    if type_name in ("I12", "U12"):
        codes = [int(v) & 0x0FFF for v in values]
        out = bytearray()
        for i in range(0, len(codes) - 1, 2):
            word = codes[i] | (codes[i + 1] << 12)
            out += word.to_bytes(3, "little")
        if len(codes) % 2:
            out += codes[-1].to_bytes(2, "little")
        return bytes(out)
    out = bytearray()
    component = COMPONENT.get(type_name, type_name)
    for v in values:
        parts = v if type_name in COMPONENT else [v]
        for part in parts:
            out += component_bytes(component, int(part) if isinstance(part, str) else part)
    return bytes(out)


def data_packet(type_name, alias, sequence, values, timestamps=()):
    body = payload(type_name, values)
    flags = 1 if timestamps else 0
    out = bytearray(struct.pack("<BBHIQ", 1, flags, TYPE_IDS[type_name], alias, sequence))
    if timestamps:
        records = sorted(timestamps, key=lambda r: ROLES[r["role"]])
        value = bytes([len(records)]) + b"".join(struct.pack("<Bq", ROLES[r["role"]], r["ns"]) for r in records)
        tlv = struct.pack("<HH", 0x8001, len(value)) + value
        area = struct.pack("<H", len(tlv)) + tlv
        area += bytes((-len(area)) % 8)
        out += area
    return bytes(out + body)


def control(kind, alias, next_expected, stream_id=None):
    sid = uuid.UUID(stream_id).bytes if stream_id else bytes(16)
    return struct.pack("<BBHIQ", 0x81, kind, 0, alias, next_expected) + sid


VALID = [
    ("f64_scalar", "F64", 7, 0, [1.5], []),
    ("i16_block", "I16", 1, 42, [-2, 1000, -32768], []),
    ("i12_packed_odd", "I12", 2, 1, [-1, 2047, -2048], []),
    ("u12_pair", "U12", 2, 2, [0xABC, 0x123], []),
    ("u32_values", "U32", 3, 5, [0, 4294967295], []),
    ("i64_value", "I64", 3, 6, [-9223372036854775808], []),
    ("f16_values", "F16", 4, 3, [1.0, -0.5], []),
    ("f32_with_timestamps", "F32", 5, 10, [3.25],
     [{"role": "SOURCE_TIME", "ns": -5}, {"role": "MODEL_TIME", "ns": 0}]),
    ("f128_values", "F128", 6, 4, [1.0, 3.0, -0.25], []),
    ("i128_values", "I128", 6, 5, ["-1", str(1 << 100)], []),
    ("u128_max", "U128", 6, 6, [str((1 << 128) - 1)], []),
    ("c64_value", "C64", 8, 1, [[1.0, -2.0]], []),
    ("c16_value", "C16", 8, 2, [[0.5, 2.0]], []),
    ("ci8_iq", "CI8", 9, 100, [[1, -1], [127, -128]], []),
    ("cu8_iq", "CU8", 9, 101, [[128, 128], [0, 255]], []),
    ("ci16_iq", "CI16", 9, 102, [[-300, 300]], []),
    ("ci128_value", "CI128", 9, 103, [["-2", str(1 << 120)]], []),
]

STREAM = "0335c8e1-a3ab-4e8c-8b17-dc8170f5e370"
CONTROL = [
    ("ack", 1, 7, 12, None),
    ("resume", 2, 7, 0, STREAM),
    ("resume_no_state", 3, 9, 0, STREAM),
]


def invalid_cases():
    good = data_packet("F64", 1, 1, [1.0])
    # Two records: 16 + 2 + 23 bytes, so bytes 41..47 are padding.
    ts = data_packet("F32", 1, 1, [1.0], [{"role": "MODEL_TIME", "ns": 1}, {"role": "SOURCE_TIME", "ns": 2}])
    cases = [
        ("too_short", good[:15], "message shorter than the 16-byte header"),
        ("bad_version", bytes([2]) + good[1:], "unsupported version"),
        ("reserved_flags", good[:1] + bytes([0x02]) + good[2:], "reserved flag bits"),
        ("unknown_type", good[:2] + struct.pack("<H", 0x0999) + good[4:], "unknown data type"),
        ("partial_value", good[:-1], "payload not a whole number of values"),
        ("empty_payload", good[:16], "empty payload"),
        ("i12_bad_length", data_packet("I12", 1, 1, [1])[:16] + bytes([1, 0, 0, 0]), "4 bytes is not a 12-bit payload length"),
        ("i12_unused_bits", data_packet("I12", 1, 1, [1])[:16] + bytes([1, 0x10]), "unused bits of the odd 12-bit value"),
        ("complex_half", data_packet("CI8", 1, 1, [[1, 2]])[:-1], "incomplete complex value"),
        ("ext_padding_not_zero", ts[:41] + bytes([1]) + ts[42:], "extension padding not zero"),
        ("unknown_critical_ext", good[:1] + bytes([1]) + good[2:16] + struct.pack("<HHH", 4, 0x8099, 0) + bytes(2) + good[16:],
         "unknown critical extension"),
        ("roles_not_ascending", good[:1] + bytes([1]) + good[2:16]
         + struct.pack("<HHHB", 23, 0x8001, 19, 2) + struct.pack("<BqBq", 2, 0, 1, 0) + bytes(7) + good[16:],
         "timestamp roles not ascending"),
        ("control_reserved", control(1, 1, 1)[:2] + b"\x01\x00" + control(1, 1, 1)[4:], "control reserved field"),
        ("control_bad_kind", control(1, 1, 1)[:1] + bytes([4]) + control(1, 1, 1)[2:], "unknown control kind"),
        ("control_short", control(1, 1, 1)[:31], "control message length"),
    ]
    return [{"name": name, "hex": data.hex(), "reason": reason} for name, data, reason in cases]


def main():
    out_path = Path(sys.argv[1] if len(sys.argv) > 1 else
                    Path(__file__).resolve().parent.parent / "s7transport/tests/fixtures/wire_v1.json")
    fixtures = {
        "format": "S7 data stream wire format v1 (S7_Network_Wire_Protocol_v1.md)",
        "generator": "tools/gen_wire_fixtures.py",
        "data": [
            {"name": name, "type": t, "stream_alias": alias, "sequence": seq, "values": values,
             "timestamps": ts, "hex": data_packet(t, alias, seq, values, ts).hex()}
            for name, t, alias, seq, values, ts in VALID
        ],
        # Accepted, with the unknown noncritical extension ignored.
        "data_ignoring_extensions": [{
            "name": "noncritical_extension_ignored", "type": "F64", "stream_alias": 1, "sequence": 1, "values": [1.0],
            "hex": (bytes([1, 1]) + struct.pack("<HIQ", 0x000F, 1, 1) + struct.pack("<HHH", 4, 0x0042, 0) + bytes(2)
                    + struct.pack("<d", 1.0)).hex(),
        }],
        "control": [
            {"name": name, "kind": kind, "stream_alias": alias, "next_expected": nxt, "stream_id": sid,
             "hex": control(kind, alias, nxt, sid).hex()}
            for name, kind, alias, nxt, sid in CONTROL
        ],
        "invalid": invalid_cases(),
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(fixtures, indent=2) + "\n")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
