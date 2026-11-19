# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Stateless math elements, computed with libm as s7core is."""
import math
import operator

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.spec import GENERIC, Param, T, Variadic

from ._ports import IN, OUT, inp


# ---- math and control: stateless, direct feedthrough --------------------------------
#
# s7core computes these with libm, as Python's math module does, so finite results agree
# bit for bit. Where libm returns NaN or an infinity, Python's math raises instead; the
# wrappers below return the libm value. A NaN's sign and payload are not reproduced:
# they are platform-specific (glibc gives -NaN for sin(inf) and +NaN for asin(2)).

def _ieee(function, odd: bool = False):
    """function(x) as libm returns it: NaN for a domain error (sin(inf), asin(2)) and an
    infinity on overflow (exp(1000)); odd: an odd function, whose infinity has the sign of x."""
    def call(x: float) -> float:
        try:
            return function(x)
        except ValueError:
            return math.nan
        except OverflowError:
            return math.copysign(math.inf, x) if odd else math.inf
    return call


def _odd_integer(y: float) -> bool:
    return math.isfinite(y) and y.is_integer() and math.fmod(y, 2.0) != 0.0


def _pow(x: float, y: float) -> float:
    """C pow: an infinity at the pole of 0 and on overflow, NaN for a negative base with a non-integer exponent."""
    try:
        return math.pow(x, y)
    except ValueError:
        if x == 0.0:
            return math.copysign(math.inf, x) if _odd_integer(y) else math.inf
        return math.nan
    except OverflowError:
        return -math.inf if x < 0 and _odd_integer(y) else math.inf


TRIG_FUNCTIONS = {
    "sin": _ieee(math.sin), "cos": _ieee(math.cos), "tan": _ieee(math.tan),
    "asin": _ieee(math.asin), "acos": _ieee(math.acos), "atan": math.atan,
    "sinh": _ieee(math.sinh, odd=True), "cosh": _ieee(math.cosh), "tanh": math.tanh,
}


class Trig(Element):
    display_name = "Trigonometric Function"
    description = "y = f(x): sin, cos, tan, asin, acos, atan, sinh, cosh or tanh of an arbitrary input"
    category = "filters"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("function", "enum", "sin", "Which function to apply",
                                  enum_values=tuple(TRIG_FUNCTIONS)),)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.function = TRIG_FUNCTIONS[self.read(params, "function")]

    def step(self, inputs):
        self.outputs[0] = self.function(inputs[0])


class Divider(Element):
    """numerator / denominator; a zero denominator is an execution error, not an infinity."""

    description = "Divide numerator by denominator"
    category = "basic"
    input_ports, output_ports = (inp("numerator"), inp("denominator")), OUT
    type_variables = (T,)
    parameters = GENERIC

    def step(self, inputs):
        numerator, denominator = inputs
        if denominator == 0.0:
            raise RuntimeError("Divider denominator is zero.")
        self.outputs[0] = numerator / denominator


def _sqrt(x: float) -> float:
    if x < 0.0:
        raise RuntimeError("MathFunction sqrt input is negative.")
    return math.sqrt(x)


def _log(x: float) -> float:
    if x <= 0.0:
        raise RuntimeError("MathFunction log input must be positive.")
    return math.log(x)


def _reciprocal(x: float) -> float:
    if x == 0.0:
        raise RuntimeError("MathFunction reciprocal input is zero.")
    return 1.0 / x


class MathFunction(Element):
    display_name = "Math Function"
    description = "Apply a scalar math function"
    category = "basic"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("function", "enum", "sqrt", "Function applied to the input",
                                  enum_values=("sqrt", "exp", "log", "pow", "abs", "reciprocal")),
                            Param("exponent", "value", 2.0, "Exponent used when function is pow"))

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        exponent = self.read(params, "exponent")
        self.function = {
            "sqrt": _sqrt, "exp": _ieee(math.exp), "log": _log,
            "pow": lambda x: _pow(x, exponent), "abs": abs, "reciprocal": _reciprocal,
        }[self.read(params, "function")]

    def step(self, inputs):
        self.outputs[0] = self.function(inputs[0])


class Power(Element):
    """base ^ exponent, both inputs."""

    description = "Raise the base input to the exponent input"
    category = "basic"
    input_ports, output_ports = (inp("base"), inp("exponent")), OUT
    type_variables = (T,)
    parameters = GENERIC

    def step(self, inputs):
        self.outputs[0] = _pow(inputs[0], inputs[1])


class Saturation(Element):
    """Clamps to [lower_limit, upper_limit]; NaN passes through."""

    description = "Clamp an input between inclusive limits"
    category = "basic"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC + (
        Param("lower_limit", "value", 0.0, "Inclusive lower output limit in input units; must not exceed upper_limit"),
        Param("upper_limit", "value", 1.0, "Inclusive upper output limit in input units; must be at least lower_limit"))

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.lower = self.read(params, "lower_limit")
        self.upper = self.read(params, "upper_limit")
        if not self.lower <= self.upper:
            raise SchemaError("upper_limit must be at least lower_limit")

    def step(self, inputs):
        x = inputs[0]
        self.outputs[0] = self.lower if x < self.lower else (self.upper if self.upper < x else x)


class MinMax(Element):
    """Minimum or maximum of 2 to 64 inputs. NaN is ignored while another input is a number;
    of equal values (0.0 and -0.0) the first is kept."""

    description = "Select the minimum or maximum of its inputs"
    category = "basic"
    input_ports, output_ports = (inp("in0"), inp("in1")), OUT
    variadic_inputs = Variadic("input_count", 2, 64, type_rule="T")
    type_variables = (T,)
    parameters = GENERIC + (Param("operation", "enum", "min", "Select minimum or maximum", enum_values=("min", "max")),
                            Param("input_count", "int", 2, "Number of input ports", 2, 64))

    def __init__(self, element_id, params):
        self.maximum = self.read(params, "operation") == "max"
        self.input_count = self.read(params, "input_count")
        super().__init__(element_id, params)

    def step(self, inputs):
        result = inputs[0]
        for value in inputs[1:]:
            if math.isnan(value):
                continue
            if math.isnan(result) or (result < value if self.maximum else value < result):
                result = value
        self.outputs[0] = result


class Sign(Element):
    """-1.0, 0.0 or 1.0 (for -0.0 too); NaN passes through."""

    description = "Return -1, 0 or 1 according to the input sign"
    category = "basic"
    input_ports, output_ports = IN, OUT
    type_variables = (T,)
    parameters = GENERIC

    def step(self, inputs):
        x = inputs[0]
        self.outputs[0] = x if math.isnan(x) else (1.0 if x > 0.0 else (-1.0 if x < 0.0 else 0.0))


COMPARATOR_OPERATIONS = {"lt": operator.lt, "le": operator.le, "gt": operator.gt,
                         "ge": operator.ge, "eq": operator.eq, "ne": operator.ne}


class Comparator(Element):
    """1.0 if lhs `operation` rhs holds, else 0.0; IEEE comparison, so only "ne" holds for NaN."""

    description = "Output 1 if lhs 'operation' rhs holds, else 0"
    category = "basic"
    input_ports, output_ports = (inp("lhs"), inp("rhs")), OUT
    type_variables = (T,)
    parameters = GENERIC + (Param("operation", "enum", "lt", "Relational operation applied to lhs and rhs",
                                  enum_values=tuple(COMPARATOR_OPERATIONS)),)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.operation = COMPARATOR_OPERATIONS[self.read(params, "operation")]

    def step(self, inputs):
        self.outputs[0] = 1.0 if self.operation(inputs[0], inputs[1]) else 0.0
