# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Signal sources: oscillator, step and PWM."""
import math

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.rate import rate_from_hz
from s7core.spec import GENERIC, Param, T

from ._ports import OUT, inp, out


def _sample_rate(default: float) -> Param:
    # s7py still pins the rate with sample_rate_hz; s7core's sources follow their domain's rate
    # (doc/S7_Sample_Rate_Model.md), where it is a legacy field.
    return Param("sample_rate_hz", "real", default, "Sample rate in Hz; pins the rate of everything connected to it")


class AnalogOscillator(Element):
    display_name = "Analog Oscillator"
    description = "Sinusoidal source, computed in its type"
    category = "sources"
    output_ports = OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("amplitude", "value", 1.0, "Signal amplitude"),
                            Param("frequency_hz", "value", 1.0, "Frequency in Hz"), _sample_rate(1.0),
                            Param("phase", "value", 0.0, "Initial phase (radians)"))
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.amplitude = self.read(params, "amplitude")
        self.sample_rate = self.read(params, "sample_rate_hz")
        if not self.sample_rate > 0:
            raise SchemaError("AnalogOscillator requires a positive sample rate")
        self.increment = 2 * math.pi * self.read(params, "frequency_hz") / self.sample_rate
        self.phase = self.read(params, "phase")

    def rate(self):
        return rate_from_hz(self.sample_rate)

    def step(self, inputs):
        self.outputs[0] = self.amplitude * math.sin(self.phase)
        self.phase += self.increment
        if abs(self.phase) > 2 * math.pi:
            self.phase = math.fmod(self.phase, 2 * math.pi)


class StepGenerator(Element):
    """initial_value for the first start_sample steps (zero-based), final_value from then on."""

    description = ("Sigma step at a specified model sample Source values must be finite; sample_rate_hz must be "
                   "finite and strictly positive. Phase is emitted before advancing to the next sample. Frequencies "
                   "beyond the sampling band alias; the generator does not band-limit the waveform.")
    category = "sources"
    output_ports = OUT
    type_variables = (T,)
    parameters = GENERIC + (_sample_rate(1000.0),
                            Param("start_sample", "int", 0, "Steps at initial_value before the step", 0, 2**63 - 1),
                            Param("initial_value", "value", 0.0, "Level before start_sample"),
                            Param("final_value", "value", 1.0, "Level at and after start_sample"))
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.sample_rate = self.read(params, "sample_rate_hz")
        if not (math.isfinite(self.sample_rate) and self.sample_rate > 0):
            raise SchemaError("Source sample_rate_hz must be finite and positive.")
        rate_from_hz(self.sample_rate)  # sources.hpp require_source_rate: refused at build, not only when rates are resolved
        self.remaining = self.read(params, "start_sample")
        self.initial = self.read(params, "initial_value")
        self.final = self.read(params, "final_value")
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

    description = "Trailing-edge PWM; duty latched at each carrier boundary"
    category = "sources"
    input_ports = (inp("duty"),)
    output_ports = (out("gate"),)
    type_variables = (T,)
    parameters = GENERIC + (Param("period_samples", "int", 100, "Carrier period in samples", 2, 65504),
                            _sample_rate(100000.0))
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.period = self.read(params, "period_samples")
        self.sample_rate = self.read(params, "sample_rate_hz")
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
