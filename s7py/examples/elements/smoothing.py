# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0

"""An example element library: two smoothing filters, one with direct feedthrough, one causal memory.

    python3 -m s7node.trace examples/smoothing.json --lib examples/elements/smoothing.py
    python3 -m s7node --schema node.json --lib examples/elements/smoothing.py
"""
from s7core import GENERIC, Element, Param, Port, SchemaError, T


class MovingAverage(Element):
    """y[n] = mean of the last `length` inputs, x[n] included (of all of them while there are fewer).
    Direct feedthrough: y[n] depends on x[n], so it cannot close a feedback loop alone. Stateful: it
    keeps the window between steps."""

    description = "Mean of the last length inputs, the current one included"
    category = "filters"
    input_ports = (Port("in", "Input", type_rule="T"),)
    output_ports = (Port("out", "Mean of the window", type_rule="T"),)
    type_variables = (T,)
    parameters = GENERIC + (Param("length", "int", 4, "Number of inputs averaged", 1, 64),)
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.length = self.read(params, "length")
        self.window: list[float] = []

    def step(self, inputs):
        self.window.append(inputs[0])
        if len(self.window) > self.length:
            del self.window[0]
        total = 0.0
        for x in self.window:  # summed oldest first each step: no drift from a running sum
            total += x
        self.outputs[0] = total / len(self.window)


class ExponentialSmoother(Element):
    """y[n+1] = y[n] + alpha * (x[n] - y[n]). Causal memory: y[n+1] is computed from step n's input
    and published at the start of step n+1, so it can close a feedback loop."""

    description = "First-order exponential smoothing; causal memory, can close a loop"
    category = "filters"
    input_ports = (Port("in", "Input", type_rule="T"),)
    output_ports = (Port("out", "Smoothed value, one step behind", type_rule="T"),)
    type_variables = (T,)
    parameters = GENERIC + (Param("alpha", "real", 0.5, "Smoothing factor, 0 < alpha <= 1"),
                            Param("initial_value", "value", 0.0, "Output before the first input"))
    memory = True
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.alpha = self.read(params, "alpha")
        if not 0.0 < self.alpha <= 1.0:
            raise SchemaError("alpha must be in (0, 1]")
        self.outputs[0] = self.read(params, "initial_value")

    def step(self, inputs):
        y = self.outputs[0]
        self.outputs[0] = y + self.alpha * (inputs[0] - y)
