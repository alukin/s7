# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Signal sources: oscillator, step and PWM."""
import math

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.params import GENERIC, _integer, _number
from s7core.rate import rate_from_hz


class AnalogOscillator(Element):
    params = GENERIC + ("amplitude", "frequency_hz", "sample_rate_hz", "phase")
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.amplitude = _number(params, "amplitude", 1.0)
        self.sample_rate = _number(params, "sample_rate_hz", 1.0)
        if not self.sample_rate > 0:
            raise SchemaError("AnalogOscillator requires a positive sample rate")
        self.increment = 2 * math.pi * _number(params, "frequency_hz", 1.0) / self.sample_rate
        self.phase = _number(params, "phase", 0.0)

    def rate(self):
        return rate_from_hz(self.sample_rate)

    def step(self, inputs):
        self.outputs[0] = self.amplitude * math.sin(self.phase)
        self.phase += self.increment
        if abs(self.phase) > 2 * math.pi:
            self.phase = math.fmod(self.phase, 2 * math.pi)


class StepGenerator(Element):
    """initial_value for the first start_sample steps (zero-based), final_value from then on."""

    params = GENERIC + ("sample_rate_hz", "start_sample", "initial_value", "final_value")
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.sample_rate = _number(params, "sample_rate_hz", 1000.0)
        if not (math.isfinite(self.sample_rate) and self.sample_rate > 0):
            raise SchemaError("Source sample_rate_hz must be finite and positive.")
        rate_from_hz(self.sample_rate)  # sources.hpp require_source_rate: refused at build, not only when rates are resolved
        self.remaining = _integer(params, "start_sample", 0, 0, 2**63 - 1)
        self.initial = _number(params, "initial_value", 0.0)
        self.final = _number(params, "final_value", 1.0)
        if not (math.isfinite(self.initial) and math.isfinite(self.final)):
            raise SchemaError("Source parameters must be finite.")

    def rate(self):
        return rate_from_hz(self.sample_rate)

    def step(self, inputs):
        if self.remaining:
            self.outputs[0] = self.initial
            self.remaining -= 1
        else:
            self.outputs[0] = self.final


class PWM(Element):
    """Trailing-edge PWM, gate 1.0 or 0.0: the duty input is read at each carrier boundary,
    clamped to [0, 1] and held for the period of period_samples steps."""

    params = GENERIC + ("period_samples", "sample_rate_hz")
    stateful = True
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.period = _integer(params, "period_samples", 100, 2, 65504)
        self.sample_rate = _number(params, "sample_rate_hz", 100000.0)
        if not (math.isfinite(self.sample_rate) and self.sample_rate > 0):
            raise SchemaError("PWM requires 2 <= period_samples <= 65504 and a finite positive sample_rate_hz.")
        self.sample = 0
        self.duty = 0.0

    def rate(self):
        return rate_from_hz(self.sample_rate)

    def step(self, inputs):
        if self.sample == 0:
            duty = inputs[0]
            if not math.isfinite(duty):
                raise RuntimeError("PWM duty must be finite.")
            self.duty = 0.0 if duty < 0.0 else (1.0 if duty > 1.0 else duty)
        self.outputs[0] = 1.0 if self.sample / self.period < self.duty else 0.0
        self.sample = (self.sample + 1) % self.period
