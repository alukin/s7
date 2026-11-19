# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Basic elements: constants, arithmetic, delays and accumulation."""
from s7core.element import Element
from s7core.spec import ACCUMULATOR_TYPE, GENERIC, OVERFLOW, A, Param, T, Variadic

from ._ports import IN, OUT, inp, out


class Constant(Element):
    description = "Outputs a fixed value on every causal step; has no inputs."
    category = "basic"
    output_ports = OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("value", "value", 0.0, "Output value"),)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.outputs[0] = self.read(params, "value")

    def step(self, inputs):
        pass


class Gain(Element):
    description = ("Multiplies the current input signal by a configurable factor. "
                   "The output is available in the same step.")
    category = "basic"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("factor", "value", 1.0, "Gain factor, of the element's type"), OVERFLOW)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.factor = self.read(params, "factor")

    def step(self, inputs):
        self.outputs[0] = inputs[0] * self.factor


class Adder(Element):
    description = "Weighted sum of N inputs"
    category = "basic"
    input_ports = (inp("lhs", "Summation input 0 (weighted by weights[0])."),
                   inp("rhs", "Summation input 1 (weighted by weights[1])."))
    output_ports = (out("out", "Weighted sum output.", "A"),)
    variadic_inputs = Variadic("weights", 2, 64, type_rule="T")
    type_variables = (T, A)
    parameters = GENERIC + (Param(
        "weights", "array", (1, 1), "Array of 2..64 weights in accumulator_type (A). Input count follows "
        "array length; y = sum(weights[i] * input[i]). Example: [1,-1] subtracts the second input."),
        OVERFLOW, ACCUMULATOR_TYPE)

    def __init__(self, element_id, params):
        self.weights = self.read(params, "weights")
        self.input_count = len(self.weights)
        super().__init__(element_id, params)

    def step(self, inputs):
        total = 0.0  # summed in input order, as AdderT does: the same value to the last bit
        for x, weight in zip(inputs, self.weights):
            total += x * weight
        self.outputs[0] = total


class Multiply(Element):
    description = "Product of N inputs"
    category = "basic"
    input_ports = (inp("lhs", "Multiplier input 0 (scaled by factors[0])."),
                   inp("rhs", "Multiplier input 1 (scaled by factors[1])."))
    output_ports = (out("out", "Product output.", "A"),)
    variadic_inputs = Variadic("factors", 2, 64, type_rule="T")
    type_variables = (T, A)
    parameters = GENERIC + (Param(
        "factors", "array", (1, 1), "Array of 2..64 factors in accumulator_type (A). Input count follows "
        "array length; y = product(factors[i] * input[i]). Example: [1,1] multiplies two unscaled inputs."),
        OVERFLOW, ACCUMULATOR_TYPE)

    def __init__(self, element_id, params):
        self.factors = self.read(params, "factors")
        self.input_count = len(self.factors)
        super().__init__(element_id, params)

    def step(self, inputs):
        product = 1.0
        for x, factor in zip(inputs, self.factors):
            product *= x * factor
        self.outputs[0] = product


class Delay(Element):
    """Unit delay z^-1 ("Delay" and "PreviousValue" are the same element)."""

    description = ("Outputs the input from the previous causal step. Publishes initial_value first; "
                   "supplies causal memory to close feedback loops.")
    category = "filters"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("initial_value", "value", 0.0, "Output before the first input"),)
    memory = True
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.outputs[0] = self.read(params, "initial_value")

    def step(self, inputs):
        self.outputs[0] = inputs[0]


class Accumulator(Element):
    display_name = "Accumulator (running sum)"
    description = ("Adds the current input to its running total: y[n] = y[n-1] + x[n]. No timestep factor. "
                   "Stateful direct feedthrough: its new total is visible in the same step, so it cannot "
                   "alone close a feedback loop.")
    category = "filters"
    input_ports = IN
    output_ports = (out(rule="A"),)
    type_variables = (T, A)
    parameters = GENERIC + (Param("initial_value", "value", 0.0,
                                  "Running total before the first addition, in accumulator_type (A)"),
                            OVERFLOW, ACCUMULATOR_TYPE)
    stateful = True

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.outputs[0] = self.read(params, "initial_value")

    def step(self, inputs):
        self.outputs[0] += inputs[0]
