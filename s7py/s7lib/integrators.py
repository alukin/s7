# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Integrators: forward Euler and the trapezoidal rule (implicit or sampled)."""
import math

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.params import GENERIC, _choice, _integer, _number
from s7core.rate import rate_from_seconds
from s7core.trapezoid import TrapezoidIntegration


class EulerIntegrator(Element):
    """y[n+1] = y[n] + dt * x[n]; causal memory, so it can close a loop."""

    params = GENERIC + ("dt", "initial_value", "substeps")
    memory = True
    stateful = True
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        if _integer(params, "substeps", 1, 1, 32) != 1:
            raise SchemaError("EulerIntegrator substeps greater than 1 are not supported by s7py")
        self.dt = _number(params, "dt", 1.0)
        self.outputs[0] = _number(params, "initial_value", 0.0)

    def rate(self):
        return rate_from_seconds(self.dt)

    def step(self, inputs):
        self.outputs[0] = self.outputs[0] + inputs[0] * self.dt


# Implicit trapezoid: each state converged within 8 machine epsilons
# (s7l2/docs/TRAPEZOID_INTEGRATION.md); the iteration limit belongs to the graph's coupled stage.
TRAPEZOID_TOLERANCE = math.ldexp(1.0, 4 - 53)


class TrapezoidIntegrator(Element, TrapezoidIntegration):
    """mode "implicit" (default): y[n+1] = y[n] + dt/2 * (f(y[n]) + f(y[n+1])), solved by
    Graph together with every other implicit state; causal memory.
    mode "sampled": y[n] = y[n-1] + dt/2 * (x[n] + x[n-1]) of supplied samples; direct feedthrough.
    """

    params = GENERIC + ("dt", "initial_value", "mode")
    stateful = True
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.implicit = _choice(params, "mode", ("implicit", "sampled")) == "implicit"
        self.memory = self.implicit
        self.half_dt = _number(params, "dt", 1.0) / 2
        self.outputs[0] = _number(params, "initial_value", 0.0)
        self.previous = None
        self.base_value = self.base_input = self.candidate = 0.0

    def rate(self):
        return rate_from_seconds(self.half_dt * 2)

    def step(self, inputs):
        if self.implicit:  # the graph's coupled stage steps it, as s7core's NodeRuntime does
            raise RuntimeError("an implicit TrapezoidIntegrator is stepped by the graph's coupled numerical stage")
        x = inputs[0]
        if self.previous is not None:
            self.outputs[0] = self.outputs[0] + (x + self.previous) * self.half_dt
        self.previous = x

    def implicit_trapezoid_enabled(self):
        return self.implicit

    def prepare_trapezoid_step(self, inputs):
        """Forward-Euler predictor from the published state and its current derivative."""
        self.base_value, self.base_input = self.outputs[0], inputs[0]
        self.candidate = self.base_value + self.base_input * (self.half_dt * 2)

    def correct_trapezoid_candidate(self, inputs) -> bool:
        """Trapezoidal correction from the derivative at the published candidate; True once converged."""
        self.candidate = self.base_value + (self.base_input + inputs[0]) * self.half_dt
        magnitude, difference = abs(self.candidate), abs(self.candidate - self.outputs[0])
        if not (math.isfinite(magnitude) and math.isfinite(difference)):
            raise RuntimeError("TrapezoidIntegrator produced a non-finite candidate")
        return difference <= TRAPEZOID_TOLERANCE * (1 + magnitude)

    def publish_trapezoid_candidate(self):
        self.outputs[0] = self.candidate
