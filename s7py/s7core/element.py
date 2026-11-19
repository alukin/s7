# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The element contract: declarations, flags and step().

A subclass declares its ports (input_ports, output_ports, or variadic_inputs), its parameters
and, for a generic element, its type variables; reads its parameter values in __init__ with
self.read(); and computes its outputs from its inputs in step(). Flags:

- memory: causal memory, no direct feedthrough. step() computes the next outputs from this step's
  inputs; the graph publishes them at the start of the next step, so it can close a feedback loop.
- stateful: keeps state between steps (an implicit trapezoid's solver must not reevaluate it).

rate() returns the rate the element pins, if any. An element holding a resource defines close().
"""
from fractions import Fraction
from typing import ClassVar

from .spec import Param, Port, TypeVariable, Variadic


class Element:
    """One instance in a graph."""

    type_name: ClassVar[str] = ""  # the schema's "type"; the class name unless set
    display_name: ClassVar[str] = ""  # the type name unless set
    description: ClassVar[str] = ""
    category: ClassVar[str] = "custom"
    version: ClassVar[str] = "1.0.0"
    input_ports: ClassVar[tuple[Port, ...]] = ()
    output_ports: ClassVar[tuple[Port, ...]] = (Port("out", "Output"),)
    variadic_inputs: ClassVar[Variadic | None] = None
    parameters: ClassVar[tuple[Param, ...]] = ()
    type_variables: ClassVar[tuple[TypeVariable, ...]] = ()

    memory = False
    stateful = False
    # Port counts: from the declared ports unless the class sets them; a variadic element sets
    # its input_count in __init__ from its count parameter, before calling Element.__init__.
    input_count = 0
    output_count = 1
    params: ClassVar[tuple[str, ...]] = ()  # the declared parameter names, in order
    _parameter: ClassVar[dict[str, Param]] = {}

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if "params" in cls.__dict__:
            raise TypeError(f"{cls.__name__}: declare parameters (s7core.spec.Param), not params")
        if "type_name" not in cls.__dict__:
            cls.type_name = cls.__name__
        if "input_count" not in cls.__dict__ and "input_ports" in cls.__dict__:
            cls.input_count = len(cls.input_ports)
        if "output_count" not in cls.__dict__ and "output_ports" in cls.__dict__:
            cls.output_count = len(cls.output_ports)
        cls.params = tuple(p.name for p in cls.parameters)
        cls._parameter = {p.name: p for p in cls.parameters}

    def __init__(self, element_id: str, params: dict):
        self.id = element_id
        self.outputs = [0.0] * self.output_count

    @classmethod
    def read(cls, params: dict, name: str):
        """The value of declared parameter name in params, or its declared default."""
        return cls._parameter[name].read(params)

    def rate(self) -> Fraction | None:
        """The rate this element pins by itself (Element::declared_rate), if any."""
        return None

    def step(self, inputs: list[float]) -> None:
        raise NotImplementedError

    @classmethod
    def describe(cls, type_name: str | None = None) -> dict:
        """This element type in the form of an s7core element catalog entry (element_catalog.cpp),
        plus its category; type_name: the name it is registered under, if not its own."""
        name = type_name or cls.type_name
        variadic = cls.variadic_inputs
        inputs = cls.input_count
        if variadic is not None:
            default = cls._parameter[variadic.count_parameter].default
            inputs = len(default) if isinstance(default, (list, tuple)) else default
        out = {"type_name": name, "display_name": cls.display_name or name, "description": cls.description,
               "version": cls.version, "input_count": inputs, "output_count": cls.output_count,
               "schema_based": False,
               "input_ports": [p.describe() for p in cls.input_ports],
               "output_ports": [p.describe() for p in cls.output_ports],
               "type_variables": [v.describe() for v in cls.type_variables],
               "parameters": [p.describe() for p in cls.parameters]}
        if variadic is not None:
            out["variadic_inputs"] = variadic.describe()
        out["category"] = cls.category
        return out
