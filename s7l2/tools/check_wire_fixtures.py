#!/usr/bin/env python3
"""A second, independent decoder of the S7 data stream wire format v1.

Written from S7_Network_Wire_Protocol_v1.md (sections 7, 8, 12.1), not from
the C++ code or the fixture generator, it checks that the shared fixtures
(s7transport/tests/fixtures/wire_v1.json) are complete and unambiguous for a
non-C++ implementation: every valid packet and control message decodes to
the fixture's fields, every invalid one is rejected, and stream descriptors
read to their canonical form or are refused. It is the reference for
decoders in other languages.

Usage: tools/check_wire_fixtures.py [wire_v1.json]
"""
import json
import math
import struct
import sys
import uuid
from pathlib import Path

TYPES = {  # id -> (name, kind, component bits)
    0x0001: ("I8", "int", 8), 0x0002: ("U8", "uint", 8), 0x0003: ("I12", "int12", 12), 0x0004: ("U12", "uint12", 12),
    0x0005: ("I16", "int", 16), 0x0006: ("U16", "uint", 16), 0x0007: ("I32", "int", 32), 0x0008: ("U32", "uint", 32),
    0x0009: ("I64", "int", 64), 0x000A: ("U64", "uint", 64), 0x000B: ("I128", "int", 128), 0x000C: ("U128", "uint", 128),
    0x000D: ("F16", "float", 16), 0x000E: ("F32", "float", 32), 0x000F: ("F64", "float", 64), 0x0010: ("F128", "float", 128),
    0x0101: ("C16", "cfloat", 16), 0x0102: ("C32", "cfloat", 32), 0x0103: ("C64", "cfloat", 64), 0x0104: ("C128", "cfloat", 128),
    0x0201: ("CI8", "cint", 8), 0x0202: ("CU8", "cuint", 8), 0x0203: ("CI16", "cint", 16), 0x0204: ("CI32", "cint", 32),
    0x0205: ("CI64", "cint", 64), 0x0206: ("CI128", "cint", 128),
}
TYPE_NAMES = {name for name, _, _ in TYPES.values()}
ROLES = {1: "MODEL_TIME", 2: "SOURCE_TIME", 3: "PROCESSING_TIME"}
TIMESTAMPS = 0x8001
CRITICAL = 0x8000


class Invalid(Exception):
    pass


def f128_to_float(bits):
    sign = -1.0 if bits >> 127 else 1.0
    exponent = (bits >> 112) & 0x7FFF
    fraction = bits & ((1 << 112) - 1)
    if exponent == 0:
        return sign * math.ldexp(fraction, -16382 - 112)
    if exponent == 0x7FFF:
        return sign * math.inf if fraction == 0 else math.nan
    return sign * math.ldexp((1 << 112) | fraction, exponent - 16383 - 112)


def component(data, kind, bits):
    width = bits // 8
    raw = int.from_bytes(data[:width], "little")
    if kind in ("int", "cint"):
        return raw - (1 << bits) if raw >> (bits - 1) else raw
    if kind in ("uint", "cuint"):
        return raw
    if bits == 16:
        return struct.unpack("<e", data[:2])[0]
    if bits == 32:
        return struct.unpack("<f", data[:4])[0]
    if bits == 64:
        return struct.unpack("<d", data[:8])[0]
    return f128_to_float(raw)


def decode_payload(type_id, payload):
    if not payload:
        raise Invalid("empty payload")
    _, kind, bits = TYPES[type_id]
    if kind in ("int12", "uint12"):
        if len(payload) % 3 == 1:
            raise Invalid("not a 12-bit payload length")
        codes = []
        for i in range(0, len(payload) - len(payload) % 3, 3):
            word = int.from_bytes(payload[i:i + 3], "little")
            codes += [word & 0xFFF, word >> 12]
        if len(payload) % 3 == 2:
            last = int.from_bytes(payload[-2:], "little")
            if last >> 12:
                raise Invalid("unused bits of an odd 12-bit value")
            codes.append(last)
        return [c - 4096 if kind == "int12" and c >> 11 else c for c in codes]
    parts = 2 if kind.startswith("c") else 1
    element = parts * bits // 8
    if len(payload) % element:
        raise Invalid("payload not a whole number of elements")
    base_kind = kind[1:] if parts == 2 else kind
    values = []
    for i in range(0, len(payload), element):
        items = [component(payload[i + p * bits // 8:], base_kind, bits) for p in range(parts)]
        values.append(items if parts == 2 else items[0])
    return values


def decode_timestamps(value):
    if not value or len(value) != 1 + 9 * value[0]:
        raise Invalid("TIMESTAMPS value length")
    records = []
    for i in range(value[0]):
        role, ns = struct.unpack_from("<Bq", value, 1 + 9 * i)
        if role not in ROLES:
            raise Invalid("unknown timestamp role")
        if records and role <= records[-1][0]:
            raise Invalid("timestamp roles not ascending or repeated")
        records.append((role, ns))
    return [{"role": ROLES[role], "ns": ns} for role, ns in records]


def decode_data(message):
    if len(message) < 16:
        raise Invalid("shorter than the header")
    version, flags, type_id, alias, sequence = struct.unpack_from("<BBHIQ", message)
    if version != 1:
        raise Invalid("unsupported version")
    if flags & ~1:
        raise Invalid("reserved flag bits")
    if type_id not in TYPES:
        raise Invalid("unknown data type")
    offset = 16
    timestamps = []
    if flags & 1:
        if len(message) < 18:
            raise Invalid("truncated extension area")
        (length,) = struct.unpack_from("<H", message, 16)
        area_end = 18 + length
        padded_end = 16 + ((2 + length + 7) // 8) * 8  # the area is a multiple of 8 bytes
        if padded_end > len(message):
            raise Invalid("extension area beyond the message")
        if any(message[area_end:padded_end]):
            raise Invalid("extension padding not zero")
        position, seen = 18, set()
        while position < area_end:
            if position + 4 > area_end:
                raise Invalid("truncated extension")
            ext_type, ext_length = struct.unpack_from("<HH", message, position)
            value = message[position + 4:position + 4 + ext_length]
            if position + 4 + ext_length > area_end:
                raise Invalid("extension beyond the area")
            if ext_type in seen:
                raise Invalid("extension repeated")
            seen.add(ext_type)
            if ext_type == TIMESTAMPS:
                timestamps = decode_timestamps(value)
            elif ext_type & CRITICAL:
                raise Invalid("unknown critical extension")
            position += 4 + ext_length
        offset = padded_end
    values = decode_payload(type_id, message[offset:])
    return {"type": TYPES[type_id][0], "stream_alias": alias, "sequence": sequence, "timestamps": timestamps, "values": values}


def decode_control(message):
    if len(message) != 32:
        raise Invalid("control message length")
    version, kind, reserved, alias, next_expected = struct.unpack_from("<BBHIQ", message)
    if version != 0x81 or kind not in (1, 2, 3) or reserved:
        raise Invalid("control header")
    stream = message[16:]
    return {"kind": kind, "stream_alias": alias, "next_expected": next_expected,
            "stream_id": None if kind == 1 else str(uuid.UUID(bytes=bytes(stream)))}


def decode(message):
    return decode_control(message) if message and message[0] == 0x81 else decode_data(message)


def same_value(expected, actual):
    if isinstance(expected, list):
        return isinstance(actual, list) and len(expected) == len(actual) and all(map(same_value, expected, actual))
    if isinstance(expected, str):
        return int(expected) == actual
    if isinstance(expected, float) or isinstance(actual, float):
        return struct.pack("<d", float(expected)) == struct.pack("<d", float(actual))  # sign of zero included
    return expected == actual


DEFAULTS = {"max_elements_per_packet": 1024, "max_batch_latency_ms": 10, "max_wait_ms": 2000}
LOG_DEFAULTS = {"max_packets": 4096, "max_block_ms": 2000}
ROLE_IDS = {name: rid for rid, name in ROLES.items()}


def read_descriptor(d):
    """Section 12.1 item 1: the descriptor with defaults, or Invalid."""
    def positive(value, what):
        if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
            raise Invalid(what + " must be a positive integer")
        return value

    def end(e):
        if not isinstance(e, dict) or not isinstance(e.get("node_uuid"), str) or not isinstance(e.get("element"), str):
            raise Invalid("stream end")
        if not isinstance(e.get("port"), int) or e["port"] < 0:
            raise Invalid("port")
        return {"node_uuid": e["node_uuid"], "element": e["element"], "port": e["port"]}

    try:
        stream_id = str(uuid.UUID(d["stream_id"]))
    except (KeyError, ValueError, TypeError, AttributeError):
        raise Invalid("stream_id")
    if d.get("data_type") not in TYPE_NAMES:
        raise Invalid("data_type")
    consumers = d.get("consumers")
    if not isinstance(consumers, list) or not consumers:
        raise Invalid("consumers")
    out = {"stream_id": stream_id, "producer": end(d.get("producer")), "consumers": [end(c) for c in consumers],
           "data_type": d["data_type"]}
    for key, default in DEFAULTS.items():
        out[key] = positive(d.get(key, default), key)
    log = d.get("send_log", {})
    out["send_log"] = {key: positive(log.get(key, default), key) for key, default in LOG_DEFAULTS.items()}
    out = {k: out[k] for k in ("stream_id", "producer", "consumers", "data_type", "max_elements_per_packet",
                               "max_batch_latency_ms", "send_log", "max_wait_ms")}
    if "sample_rate" in d:
        rate = d["sample_rate"]
        if not isinstance(rate, dict) or "denominator" not in rate:
            raise Invalid("sample_rate")
        out["sample_rate"] = {"numerator": positive(rate.get("numerator"), "numerator"),
                              "denominator": positive(rate["denominator"], "denominator")}
    timestamps = d.get("timestamps")
    if not isinstance(timestamps, list) or not timestamps:
        raise Invalid("timestamps")
    previous = 0
    for t in timestamps:
        if not isinstance(t, dict) or t.get("role") not in ROLE_IDS or not isinstance(t.get("clock_domain"), str):
            raise Invalid("timestamp declaration")
        if ROLE_IDS[t["role"]] <= previous:
            raise Invalid("roles not ascending")
        previous = ROLE_IDS[t["role"]]
    out["timestamps"] = [{"role": t["role"], "clock_domain": t["clock_domain"]} for t in timestamps]
    return out


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "s7transport/tests/fixtures/wire_v1.json")
    fixtures = json.loads(path.read_text())
    failures = []

    for case in fixtures["data"] + fixtures["data_ignoring_extensions"]:
        try:
            got = decode(bytes.fromhex(case["hex"]))
        except Invalid as error:
            failures.append(f"{case['name']}: rejected ({error})")
            continue
        expected_ts = case.get("timestamps", [])
        expected_ts = sorted(expected_ts, key=lambda r: ROLE_IDS[r["role"]])
        if (got["type"], got["stream_alias"], got["sequence"]) != (case["type"], case["stream_alias"], case["sequence"]) \
                or got["timestamps"] != expected_ts or not same_value(case["values"], got["values"]):
            failures.append(f"{case['name']}: decoded {got}")
    for case in fixtures["control"]:
        got = decode(bytes.fromhex(case["hex"]))
        if got != {k: case[k] for k in ("kind", "stream_alias", "next_expected", "stream_id")}:
            failures.append(f"{case['name']}: decoded {got}")
    for case in fixtures["invalid"]:
        try:
            decode(bytes.fromhex(case["hex"]))
            failures.append(f"{case['name']}: accepted ({case['reason']})")
        except Invalid:
            pass
    for case in fixtures["descriptors"]:
        try:
            got = read_descriptor(case["descriptor"])
        except Invalid as error:
            failures.append(f"descriptor {case['name']}: refused ({error})")
            continue
        if got != case["canonical"]:
            failures.append(f"descriptor {case['name']}: read as {got}")
    for case in fixtures["invalid_descriptors"]:
        try:
            read_descriptor(case["descriptor"])
            failures.append(f"descriptor {case['name']}: accepted ({case['reason']})")
        except Invalid:
            pass

    total = sum(len(fixtures[k]) for k in ("data", "data_ignoring_extensions", "control", "invalid", "descriptors", "invalid_descriptors"))
    for failure in failures:
        print("FAIL", failure)
    print(f"{total - len(failures)} of {total} fixtures pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
