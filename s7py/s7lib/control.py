# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Control elements (control_elements.hpp): switch, hysteresis relay, phase wrap."""
import math

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.spec import GENERIC, Param, T

from ._ports import IN, OUT, inp


def _finite(value: float) -> float:
    """control_elements.hpp control_finite()."""
    if not math.isfinite(value):
        raise RuntimeError("Control element requires finite values.")
    return value


class Switch(Element):
    """Inputs off, on, control: control 0 selects off, any other finite value on."""

    description = ("Select off/on input with numeric control (zero/nonzero) All inputs are current-step signals; "
                   "control=0 selects off, otherwise on. No state or timestep is added.")
    category = "control"
    input_ports = (inp("off"), inp("on"), inp("control"))
    output_ports = OUT
    type_variables = (T,)
    parameters = GENERIC

    def step(self, inputs):
        off, on, control = inputs
        self.outputs[0] = off if _finite(control) == 0.0 else on


class HysteresisRelay(Element):
    """On at or above upper_threshold, off at or below lower_threshold, unchanged in between;
    outputs high_value or low_value. Stateful, but the current input decides the current output."""

    description = ("Relay with distinct on/off thresholds and retained state Requires finite lower_threshold < "
                   "upper_threshold. Between thresholds the previous on/off state is retained; current input is "
                   "evaluated in the same step.")
    category = "control"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("lower_threshold", "value", -1.0, "Turn off at/below this threshold"),
                            Param("upper_threshold", "value", 1.0, "Turn on at/above this threshold"),
                            Param("low_value", "value", 0.0, "Off output"),
                            Param("high_value", "value", 1.0, "On output"),
                            Param("initial_on", "bool", False, "Initial state for an input in the hysteresis band"))
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.lower = self.read(params, "lower_threshold")
        self.upper = self.read(params, "upper_threshold")
        self.low = self.read(params, "low_value")
        self.high = self.read(params, "high_value")
        self.on = self.read(params, "initial_on")
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

    description = ("Wrap phase into a half-open interval Output lies in [lower,lower+period). period must be finite,"
                   " strictly positive and representable at lower; inputs must be finite. No state or time delay.")
    category = "control"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("period", "value", 2 * math.pi, "Interval width"),
                            Param("lower", "value", -math.pi, "Lower interval endpoint"))

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.period = self.read(params, "period")
        self.lower = self.read(params, "lower")
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
