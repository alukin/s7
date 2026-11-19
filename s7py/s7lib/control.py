# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Control elements (control_elements.hpp): switch, hysteresis relay, phase wrap."""
import math

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.params import GENERIC, _flag, _number


def _finite(value: float) -> float:
    """control_elements.hpp control_finite()."""
    if not math.isfinite(value):
        raise RuntimeError("Control element requires finite values.")
    return value


class Switch(Element):
    """Inputs off, on, control: control 0 selects off, any other finite value on."""

    input_count = 3

    def step(self, inputs):
        off, on, control = inputs
        self.outputs[0] = off if _finite(control) == 0.0 else on


class HysteresisRelay(Element):
    """On at or above upper_threshold, off at or below lower_threshold, unchanged in between;
    outputs high_value or low_value. Stateful, but the current input decides the current output."""

    params = GENERIC + ("lower_threshold", "upper_threshold", "low_value", "high_value", "initial_on")
    stateful = True
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.lower = _number(params, "lower_threshold", -1.0)
        self.upper = _number(params, "upper_threshold", 1.0)
        self.low = _number(params, "low_value", 0.0)
        self.high = _number(params, "high_value", 1.0)
        self.on = _flag(params, "initial_on", False)
        if not all(math.isfinite(v) for v in (self.lower, self.upper, self.low, self.high)):
            raise SchemaError("Control element requires finite values.")
        if not self.lower < self.upper:
            raise SchemaError("lower_threshold must be less than upper_threshold")
        self.outputs[0] = self.high if self.on else self.low

    def step(self, inputs):
        x = _finite(inputs[0])
        if x >= self.upper:
            self.on = True
        elif x <= self.lower:
            self.on = False
        self.outputs[0] = self.high if self.on else self.low


class PhaseWrap(Element):
    """Wraps a finite input into [lower, lower + period); by default radians in [-pi, pi)."""

    params = GENERIC + ("period", "lower")
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.period = _number(params, "period", 2 * math.pi)
        self.lower = _number(params, "lower", -math.pi)
        if not all(math.isfinite(v) for v in (self.period, self.lower, self.lower + self.period)):
            raise SchemaError("period, lower and lower + period must be finite")
        if not (self.period > 0 and self.lower + self.period > self.lower):
            raise SchemaError("PhaseWrap requires a positive representable period")

    def step(self, inputs):
        period, lower = self.period, self.lower
        # Each reduced first, so that a large input cannot overflow input - lower.
        phase = _finite(math.fmod(_finite(inputs[0]), period) - math.fmod(lower, period))
        if phase < 0.0:
            phase += period
        if phase >= period:
            phase -= period
        out = lower + phase
        self.outputs[0] = lower if out >= lower + period else out
