# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The built-in S7 elements, F64 only, computed as s7core computes them."""
from s7core.element import Element

from .basic import Accumulator, Adder, Constant, Delay, Gain, Multiply
from .control import HysteresisRelay, PhaseWrap, Switch
from .integrators import EulerIntegrator, TrapezoidIntegrator
from .mathops import Comparator, Divider, MathFunction, MinMax, Power, Saturation, Sign, Trig
from .sources import PWM, AnalogOscillator, StepGenerator

ELEMENT_TYPES: dict[str, type[Element]] = {
    "Constant": Constant,
    "Gain": Gain,
    "Adder": Adder,
    "Multiply": Multiply,
    "Delay": Delay,
    "PreviousValue": Delay,
    "Accumulator": Accumulator,
    "EulerIntegrator": EulerIntegrator,
    "TrapezoidIntegrator": TrapezoidIntegrator,
    "AnalogOscillator": AnalogOscillator,
    "StepGenerator": StepGenerator,
    "PWM": PWM,
    "Trig": Trig,
    "Divider": Divider,
    "MathFunction": MathFunction,
    "Power": Power,
    "Saturation": Saturation,
    "MinMax": MinMax,
    "Sign": Sign,
    "Comparator": Comparator,
    "Switch": Switch,
    "HysteresisRelay": HysteresisRelay,
    "PhaseWrap": PhaseWrap,
}
