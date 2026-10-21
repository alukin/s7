#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Byte-exact fixtures for the S7 data stream wire format, version 1.

An independent reference implementation of S7_Network_Wire_Protocol_v1.md
(sections 7, 8 and 12.1), written from the specification rather than from the
C++ code. It writes s7transport/tests/fixtures/wire_v1.json; every
implementation must encode the valid cases to exactly these bytes, decode
them back to the same fields, and reject the invalid ones; and read the valid
stream descriptors to their canonical form and refuse the invalid ones.

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


def data_packet(type_name, alias, sequence, values, timestamps=(), send_time=None):
    body = payload(type_name, values)
    flags = 1 if timestamps or send_time is not None else 0
    out = bytearray(struct.pack("<BBHIQ", 1, flags, TYPE_IDS[type_name], alias, sequence))
    if flags:
        # Extensions in ascending type order: SEND_TIME (0x0002), TIMESTAMPS (0x8001).
        tlv = b""
        if send_time is not None:
            tlv += struct.pack("<HHq", 0x0002, 8, send_time)
        if timestamps:
            records = sorted(timestamps, key=lambda r: ROLES[r["role"]])
            value = bytes([len(records)]) + b"".join(struct.pack("<Bq", ROLES[r["role"]], r["ns"]) for r in records)
            tlv += struct.pack("<HH", 0x8001, len(value)) + value
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
    ("i8_values", "I8", 10, 0, [-128, 127, 0], []),
    ("u8_values", "U8", 10, 1, [0, 255], []),
    ("u16_values", "U16", 10, 2, [0, 65535, 4660], []),
    ("u64_max", "U64", 10, 3, [str((1 << 64) - 1)], []),
    ("c32_value", "C32", 11, 0, [[-1.5, 0.25]], []),
    ("c128_value", "C128", 11, 1, [[2.0, -0.5]], []),
    ("ci32_iq", "CI32", 11, 2, [[-2147483648, 2147483647]], []),
    ("ci64_iq", "CI64", 11, 3, [["-9223372036854775808", "9223372036854775807"]], []),
    ("f64_processing_time", "F64", 12, 0, [-0.0], [{"role": "PROCESSING_TIME", "ns": 1759740000123456789}]),
    ("f64_all_roles", "F64", 12, 1, [2.5],
     [{"role": "PROCESSING_TIME", "ns": 3}, {"role": "MODEL_TIME", "ns": 1}, {"role": "SOURCE_TIME", "ns": -2}]),
]

# SEND_TIME (0x0002, noncritical): UTC nanoseconds at which the producer sealed the packet.
WITH_SEND_TIME = [
    ("f64_send_time", "F64", 13, 0, [1.0], [], 1759740000123456789),
    ("i16_send_time_and_timestamps", "I16", 13, 1, [7, -7], [{"role": "PROCESSING_TIME", "ns": 1759740000000000000}],
     1759740000000000123),
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

    def stamped(records, value_length=None):
        """A F64 packet whose TIMESTAMPS extension holds raw (role, ns) records."""
        value = bytes([len(records)]) + b"".join(struct.pack("<Bq", role, ns) for role, ns in records)
        tlv = struct.pack("<HH", 0x8001, len(value) if value_length is None else value_length) + value
        area = struct.pack("<H", len(tlv)) + tlv
        area += bytes((-len(area)) % 8)
        return good[:1] + bytes([1]) + good[2:16] + area + good[16:]

    one = bytes([1]) + struct.pack("<Bq", 1, 0)
    tlv = struct.pack("<HH", 0x8001, len(one)) + one
    area = struct.pack("<H", 2 * len(tlv)) + tlv + tlv
    area += bytes((-len(area)) % 8)
    twice = good[:1] + bytes([1]) + good[2:16] + area + good[16:]

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
        ("unknown_role", stamped([(4, 0)]), "unknown timestamp role"),
        ("repeated_role", stamped([(1, 0), (1, 5)]), "a timestamp role repeated"),
        ("timestamps_bad_length", stamped([(1, 0)], value_length=11), "TIMESTAMPS value length is not 1 + 9 * count"),
        ("timestamps_twice", twice, "TIMESTAMPS extension twice"),
        ("send_time_bad_length", good[:1] + bytes([1]) + good[2:16] + struct.pack("<HHHI", 8, 0x0002, 4, 0) + bytes(6) + good[16:],
         "SEND_TIME value is not 8 bytes"),
        ("send_time_twice", good[:1] + bytes([1]) + good[2:16] + struct.pack("<H", 24) + struct.pack("<HHq", 2, 8, 1) * 2
         + bytes(6) + good[16:], "SEND_TIME extension twice"),
        ("control_reserved", control(1, 1, 1)[:2] + b"\x01\x00" + control(1, 1, 1)[4:], "control reserved field"),
        ("control_bad_kind", control(1, 1, 1)[:1] + bytes([4]) + control(1, 1, 1)[2:], "unknown control kind"),
        ("control_short", control(1, 1, 1)[:31], "control message length"),
    ]
    return [{"name": name, "hex": data.hex(), "reason": reason} for name, data, reason in cases]


DESCRIPTOR_DEFAULTS = {
    "max_elements_per_packet": 1024,
    "max_batch_latency_ms": 10,
    "send_log": {"max_packets": 4096, "max_block_ms": 2000},
    "max_wait_ms": 2000,
}
NODE_A = "aaaaaaaa-0000-4000-8000-000000000001"
NODE_B = "bbbbbbbb-0000-4000-8000-000000000002"


def descriptor(**fields):
    """A stream descriptor (section 12.1 item 1) with the required members."""
    d = {
        "stream_id": STREAM,
        "producer": {"node_uuid": NODE_A, "element": "osc", "port": 0},
        "consumers": [{"node_uuid": NODE_B, "element": "gain", "port": 0}],
        "data_type": "F64",
        "timestamps": [{"role": "MODEL_TIME", "clock_domain": "model"}],
    }
    d.update(fields)
    return {k: v for k, v in d.items() if v is not None}


def canonical(d):
    """The descriptor with every optional member at its default."""
    out = dict(d)
    for key, value in DESCRIPTOR_DEFAULTS.items():
        if key == "send_log":
            out["send_log"] = {**value, **d.get("send_log", {})}
        else:
            out.setdefault(key, value)
    return out


def descriptor_cases():
    valid = [
        ("minimal", descriptor()),
        ("uniform_model_time", descriptor(sample_rate={"numerator": 48000, "denominator": 1})),
        ("rate_from_dt_ns", descriptor(sample_rate={"numerator": 1000000000, "denominator": 333333333})),
        ("explicit_processing_time", descriptor(data_type="CI16",
                                                timestamps=[{"role": "PROCESSING_TIME", "clock_domain": f"node:{NODE_A}:utc"}])),
        ("all_roles_fan_out", descriptor(
            consumers=[{"node_uuid": NODE_B, "element": "gain", "port": 0}, {"node_uuid": NODE_A, "element": "scope", "port": 2}],
            timestamps=[{"role": "MODEL_TIME", "clock_domain": "model"}, {"role": "SOURCE_TIME", "clock_domain": "sdr0"},
                        {"role": "PROCESSING_TIME", "clock_domain": f"node:{NODE_A}:utc"}])),
        ("all_settings", descriptor(max_elements_per_packet=64, max_batch_latency_ms=3,
                                    send_log={"max_packets": 128, "max_block_ms": 500}, max_wait_ms=250)),
        ("latency_limit", descriptor(max_latency_ms=50)),
    ]
    invalid = [
        ("missing_stream_id", descriptor(stream_id=None), "stream_id is required"),
        ("stream_id_not_uuid", descriptor(stream_id="not-a-uuid"), "stream_id is not a UUID"),
        ("no_consumers", descriptor(consumers=[]), "at least one consumer"),
        ("unknown_data_type", descriptor(data_type="F80"), "unknown data_type"),
        ("negative_port", descriptor(producer={"node_uuid": NODE_A, "element": "osc", "port": -1}), "a port is a non-negative integer"),
        ("no_timestamps", descriptor(timestamps=[]), "at least one timestamp role"),
        ("timestamps_missing", descriptor(timestamps=None), "timestamps are required"),
        ("unknown_role", descriptor(timestamps=[{"role": "WALL_TIME", "clock_domain": "x"}]), "unknown timestamp role"),
        ("roles_not_ascending", descriptor(timestamps=[{"role": "SOURCE_TIME", "clock_domain": "s"},
                                                       {"role": "MODEL_TIME", "clock_domain": "model"}]), "roles not ascending"),
        ("repeated_role", descriptor(timestamps=[{"role": "MODEL_TIME", "clock_domain": "model"},
                                                 {"role": "MODEL_TIME", "clock_domain": "model"}]), "a role repeated"),
        ("role_without_domain", descriptor(timestamps=[{"role": "MODEL_TIME"}]), "clock_domain is required"),
        ("zero_rate", descriptor(sample_rate={"numerator": 0, "denominator": 1}), "a sample rate is positive"),
        ("rate_without_denominator", descriptor(sample_rate={"numerator": 1000}), "denominator is required"),
        ("zero_batch", descriptor(max_elements_per_packet=0), "max_elements_per_packet is positive"),
        ("zero_latency_limit", descriptor(max_latency_ms=0), "max_latency_ms is positive"),
    ]
    return ([{"name": n, "descriptor": d, "canonical": canonical(d)} for n, d in valid],
            [{"name": n, "descriptor": d, "reason": r} for n, d, r in invalid])


def main():
    out_path = Path(sys.argv[1] if len(sys.argv) > 1 else
                    Path(__file__).resolve().parent.parent / "s7transport/tests/fixtures/wire_v1.json")
    fixtures = {
        "format": "S7 data stream wire format v1 (S7_Network_Wire_Protocol_v1.md)",
        "generator": "tools/gen_wire_fixtures.py",
        "data": [
            {"name": name, "type": t, "stream_alias": alias, "sequence": seq, "values": values,
             "timestamps": ts, "send_time_ns": None, "hex": data_packet(t, alias, seq, values, ts).hex()}
            for name, t, alias, seq, values, ts in VALID
        ] + [
            {"name": name, "type": t, "stream_alias": alias, "sequence": seq, "values": values,
             "timestamps": ts, "send_time_ns": sent, "hex": data_packet(t, alias, seq, values, ts, sent).hex()}
            for name, t, alias, seq, values, ts, sent in WITH_SEND_TIME
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
    # Stream descriptors (section 12.1 item 1): valid ones with every member
    # at its value or default, and descriptors a node must refuse.
    fixtures["descriptors"], fixtures["invalid_descriptors"] = descriptor_cases()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(fixtures, indent=2) + "\n")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
