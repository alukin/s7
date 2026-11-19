# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Basic elements: constants, arithmetic, delays and accumulation."""
from s7core.element import Element
from s7core.params import ACCUMULATING, ARITHMETIC, GENERIC, _number, _numbers


class Constant(Element):
    params = GENERIC + ("value",)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.outputs[0] = _number(params, "value", 0.0)

    def step(self, inputs):
        pass


class Gain(Element):
    params = ARITHMETIC + ("factor",)
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.factor = _number(params, "factor", 1.0)

    def step(self, inputs):
        self.outputs[0] = inputs[0] * self.factor


class Adder(Element):
    params = ACCUMULATING + ("weights",)

    def __init__(self, element_id, params):
        self.weights = _numbers(params, "weights")
        self.input_count = len(self.weights)
        super().__init__(element_id, params)

    def step(self, inputs):
        total = 0.0  # summed in input order, as AdderT does: the same value to the last bit
        for x, weight in zip(inputs, self.weights):
            total += x * weight
        self.outputs[0] = total


class Multiply(Element):
    params = ACCUMULATING + ("factors",)

    def __init__(self, element_id, params):
        self.factors = _numbers(params, "factors")
        self.input_count = len(self.factors)
        super().__init__(element_id, params)

    def step(self, inputs):
        product = 1.0
        for x, factor in zip(inputs, self.factors):
            product *= x * factor
        self.outputs[0] = product


class Delay(Element):
    """Unit delay z^-1 ("Delay" and "PreviousValue" are the same element)."""

    params = GENERIC + ("initial_value",)
    memory = True
    stateful = True
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.outputs[0] = _number(params, "initial_value", 0.0)

    def step(self, inputs):
        self.outputs[0] = inputs[0]


class Accumulator(Element):
    params = ACCUMULATING + ("initial_value",)
    stateful = True
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.outputs[0] = _number(params, "initial_value", 0.0)

    def step(self, inputs):
        self.outputs[0] += inputs[0]
