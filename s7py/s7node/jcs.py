# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""RFC 8785 JSON Canonicalization Scheme and the S7 configuration digest.

S7_Control_Protocol_MVP.md section 10.1: configuration_digest is "sha256:" plus
the lower-case hex SHA-256 of the canonical JSON of the configuration object
without its configuration_digest member.
"""
import hashlib
import json
from decimal import Decimal

MAX_SAFE_INTEGER = 2**53


def _number(value: float) -> str:
    """ECMAScript Number::toString of a finite double (RFC 8785 section 3.2.2.3)."""
    if value != value or value in (float("inf"), float("-inf")):
        raise ValueError("canonical JSON does not allow NaN or infinite numbers")
    if value == 0:
        return "0"
    sign = "-" if value < 0 else ""
    # repr() gives the shortest digits that round-trip, as ECMAScript requires.
    _, digit_tuple, exponent = Decimal(repr(abs(value))).as_tuple()
    digits = "".join(map(str, digit_tuple)).rstrip("0") or "0"
    exponent += len(digit_tuple) - len(digits)
    k = len(digits)
    n = k + exponent  # value = 0.digits * 10^n
    if k <= n <= 21:
        text = digits + "0" * (n - k)
    elif 0 < n <= 21:
        text = digits[:n] + "." + digits[n:]
    elif -6 < n <= 0:
        text = "0." + "0" * -n + digits
    else:
        e = n - 1
        text = digits[0] + ("." + digits[1:] if k > 1 else "") + "e" + ("+" if e > 0 else "-") + str(abs(e))
    return sign + text


def canonicalize(value) -> str:
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        if abs(value) > MAX_SAFE_INTEGER:
            raise ValueError(f"integer {value} is outside the I-JSON range (±2^53)")
        return str(value)
    if isinstance(value, float):
        return _number(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, list):
        return "[" + ",".join(canonicalize(item) for item in value) + "]"
    if isinstance(value, dict):
        # Members sorted by their names' UTF-16 code units.
        keys = sorted(value, key=lambda key: key.encode("utf-16-be"))
        return "{" + ",".join(json.dumps(key, ensure_ascii=False) + ":" + canonicalize(value[key]) for key in keys) + "}"
    raise TypeError(f"cannot canonicalize {type(value).__name__}")


def configuration_digest(configuration: dict) -> str:
    """Digest of a configuration bundle; its own configuration_digest member is ignored."""
    content = {key: item for key, item in configuration.items() if key != "configuration_digest"}
    return "sha256:" + hashlib.sha256(canonicalize(content).encode("utf-8")).hexdigest()


def _reject_duplicates(pairs):
    result = {}
    for key, item in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON member '{key}'")
        result[key] = item
    return result


def loads(text):
    """Parses JSON as the protocol requires: no duplicate members, no NaN/Infinity."""

    def bad_constant(name):
        raise ValueError(f"'{name}' is not JSON")

    return json.loads(text, object_pairs_hook=_reject_duplicates, parse_constant=bad_constant)


def dumps(value) -> str:
    """Compact serialization, as sent on the wire and measured against size limits."""
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
