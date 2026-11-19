# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Stateless math elements, computed with libm as s7core is."""
import math
import operator

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.params import GENERIC, _choice, _integer, _number


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
    params = GENERIC + ("function",)
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.function = TRIG_FUNCTIONS[_choice(params, "function", tuple(TRIG_FUNCTIONS))]

    def step(self, inputs):
        self.outputs[0] = self.function(inputs[0])


class Divider(Element):
    """numerator / denominator; a zero denominator is an execution error, not an infinity."""

    input_count = 2

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
    params = GENERIC + ("function", "exponent")
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        exponent = _number(params, "exponent", 2.0)
        self.function = {
            "sqrt": _sqrt, "exp": _ieee(math.exp), "log": _log,
            "pow": lambda x: _pow(x, exponent), "abs": abs, "reciprocal": _reciprocal,
        }[_choice(params, "function", ("sqrt", "exp", "log", "pow", "abs", "reciprocal"))]

    def step(self, inputs):
        self.outputs[0] = self.function(inputs[0])


class Power(Element):
    """base ^ exponent, both inputs."""

    input_count = 2

    def step(self, inputs):
        self.outputs[0] = _pow(inputs[0], inputs[1])


class Saturation(Element):
    """Clamps to [lower_limit, upper_limit]; NaN passes through."""

    params = GENERIC + ("lower_limit", "upper_limit")
    input_count = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.lower = _number(params, "lower_limit", 0.0)
        self.upper = _number(params, "upper_limit", 1.0)
        if not self.lower <= self.upper:
            raise SchemaError("upper_limit must be at least lower_limit")

    def step(self, inputs):
        x = inputs[0]
        self.outputs[0] = self.lower if x < self.lower else (self.upper if self.upper < x else x)


class MinMax(Element):
    """Minimum or maximum of 2 to 64 inputs. NaN is ignored while another input is a number;
    of equal values (0.0 and -0.0) the first is kept."""

    params = GENERIC + ("operation", "input_count")

    def __init__(self, element_id, params):
        self.maximum = _choice(params, "operation", ("min", "max")) == "max"
        self.input_count = _integer(params, "input_count", 2, 2, 64)
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

    input_count = 1

    def step(self, inputs):
        x = inputs[0]
        self.outputs[0] = x if math.isnan(x) else (1.0 if x > 0.0 else (-1.0 if x < 0.0 else 0.0))


COMPARATOR_OPERATIONS = {"lt": operator.lt, "le": operator.le, "gt": operator.gt,
                         "ge": operator.ge, "eq": operator.eq, "ne": operator.ne}


class Comparator(Element):
    """1.0 if lhs `operation` rhs holds, else 0.0; IEEE comparison, so only "ne" holds for NaN."""

    params = GENERIC + ("operation",)
    input_count = 2

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.operation = COMPARATOR_OPERATIONS[_choice(params, "operation", tuple(COMPARATOR_OPERATIONS))]

    def step(self, inputs):
        self.outputs[0] = 1.0 if self.operation(inputs[0], inputs[1]) else 0.0
