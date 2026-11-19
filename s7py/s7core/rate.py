# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Rates: samples per second, exact rationals (s7core rate.hpp)."""
import math
from fractions import Fraction

from .errors import SchemaError


def rate_from_step_ns(step_ns: int) -> Fraction:
    return Fraction(1_000_000_000, step_ns)


_UINT64_MAX = 2**64 - 1
_PERIOD_TOLERANCE = 2 * Fraction(2) ** -52  # twice binary64 machine epsilon, relative


def _long_double(x: Fraction) -> Fraction:
    """x rounded to the nearest x86 long double (64-bit significand, ties to even), the type
    Rate::from_seconds computes in; near its tolerance the rounding decides the convergent."""
    if x == 0:
        return x
    magnitude = abs(x)
    exponent = magnitude.numerator.bit_length() - magnitude.denominator.bit_length()
    if Fraction(2) ** exponent > magnitude:
        exponent -= 1
    scaled = magnitude / Fraction(2) ** (exponent - 63)  # in [2^63, 2^64)
    significand = round(scaled)  # Fraction rounds half to even
    return (1 if x > 0 else -1) * significand * Fraction(2) ** (exponent - 63)


def rate_from_seconds(dt: float) -> Fraction:
    """The simplest rational period within binary64 precision of dt, as s7::Rate::from_seconds:
    the first continued-fraction convergent of 1/(1/dt) within twice machine epsilon, so that
    dt and an equivalent 1/dt in Hz give the same rate. Rounded like its long double arithmetic."""
    if not (dt > 0) or not math.isfinite(dt):
        raise SchemaError("a model-time step must be a positive, finite number of seconds")
    if dt < 1e-9 or dt > 9e9:
        raise SchemaError("a model-time step must be between 1 ns and 9e9 s")
    period = Fraction(1.0 / (1.0 / dt))
    remainder = period
    p0, p1, q0, q1 = 0, 1, 1, 0  # convergents p/q of the period
    while True:
        whole = math.floor(remainder)
        if whole >= _UINT64_MAX or (p1 and whole > (_UINT64_MAX - p0) // p1) or (q1 and whole > (_UINT64_MAX - q0) // q1):
            break
        p, q = whole * p1 + p0, whole * q1 + q0
        if p and q and abs(_long_double(_long_double(Fraction(p, q)) - period)) <= period * _PERIOD_TOLERANCE:
            return Fraction(q, p)
        p0, p1, q0, q1 = p1, p, q1, q
        fraction = remainder - whole  # exact in long double too
        if fraction == 0:
            break
        remainder = _long_double(1 / fraction)
    raise SchemaError("a model-time step that cannot be represented as a rational rate")


def rate_from_hz(hz: float) -> Fraction:
    if not (hz > 0) or not math.isfinite(hz):
        raise SchemaError("a sample rate must be a positive, finite number of Hz")
    return rate_from_seconds(1.0 / hz)


def rate_text(rate: Fraction) -> str:
    if rate.denominator == 1:
        return f"{rate.numerator} Hz"
    return f"{float(rate):g} Hz ({rate.numerator}/{rate.denominator})"


def schema_rate(schema: dict, step_rate_hz: float = 0.0) -> Fraction | None:
    """The rate seed of a schema (s7runtime build_schema_from_definition): its own dt, else the node's step rate."""
    if "dt" in schema:
        dt = schema["dt"]
        if isinstance(dt, bool) or not isinstance(dt, (int, float)):
            raise SchemaError("'dt' must be a positive number of seconds")
        return rate_from_seconds(dt)
    return rate_from_hz(step_rate_hz) if step_rate_hz > 0 else None
