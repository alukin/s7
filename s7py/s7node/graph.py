# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compatibility module: the former single-file graph runtime.

The code now lives in s7core (contract, validation, stepping), s7lib (built-in elements) and
s7node.net (stream ends). This module keeps the old names importable; Graph defaults to the
node's element types, looked up when a graph is built, so patching ELEMENT_TYPES still works.
"""
from fractions import Fraction

from s7core import graph as _core
from s7core.element import Element
from s7core.errors import SchemaError
from s7core.trapezoid import TRAPEZOID_ITERATIONS
from s7core.wiring import _index
from s7core.params import (ACCUMULATING, ARITHMETIC, GENERIC, _check_params, _choice, _flag, _integer, _number,
                           _numbers)
from s7core.rate import (_long_double, rate_from_hz, rate_from_seconds, rate_from_step_ns, rate_text,
                         schema_rate)
from s7lib.basic import Accumulator, Adder, Constant, Delay, Gain, Multiply
from s7lib.control import HysteresisRelay, PhaseWrap, Switch, _finite
from s7lib.integrators import TRAPEZOID_TOLERANCE, EulerIntegrator, TrapezoidIntegrator
from s7lib.mathops import (COMPARATOR_OPERATIONS, TRIG_FUNCTIONS, Comparator, Divider, MathFunction, MinMax, Power,
                           Saturation, Sign, Trig, _ieee, _log, _odd_integer, _pow, _reciprocal, _sqrt)
from s7lib.sources import PWM, AnalogOscillator, StepGenerator

from .elements import NODE_ELEMENT_TYPES as ELEMENT_TYPES
from .net import NET, NET_PARAMS, NetContext, NetSink, NetSource, NetStreamElement, _net_config


class Graph(_core.Graph):
    """s7core.Graph over this node's element types unless others are given."""

    def __init__(self, schema: dict, rate: Fraction | None = None, *, types=None):
        super().__init__(schema, rate, types=ELEMENT_TYPES if types is None else types)
