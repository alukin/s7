# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Declarations of an element's ports, parameters and type variables (s7/element_metadata.hpp).

An element class declares what it accepts; the core checks a schema's parameters against the
declaration, an element reads its values through it, and describe() reports it in the form of
s7core's element catalog (ElementCatalogService::to_json), so that tools see what a node offers.
"""
from dataclasses import dataclass
from typing import Any

from .errors import SchemaError
from .params import _choice, _flag, _integer, _number, _numbers

# Signal types: s7py carries F64 only, so a type variable allows F64 alone.
SIGNAL_TYPES = ("F64",)
# Parameter types of s7core's ParameterSpec that s7py reads; "type" pins a type variable,
# "string" and "object" are checked by the element itself.
PARAMETER_TYPES = ("value", "real", "int", "bool", "enum", "array", "type", "string", "object")


@dataclass(frozen=True)
class Port:
    """An input or output (PortSpec): a fixed type, or type_rule naming a type variable."""

    name: str
    description: str = ""
    type: str = "F64"
    type_rule: str = ""
    optional: bool = False

    def describe(self) -> dict:
        typed = {"type_rule": self.type_rule} if self.type_rule else {"type": self.type}
        return {"name": self.name, "description": self.description, **typed, "optional": self.optional}


@dataclass(frozen=True)
class TypeVariable:
    """A type variable of a generic element (TypeVariableSpec), pinned by a "type" parameter."""

    name: str
    pin_parameter: str = "type"
    allowed: tuple[str, ...] = SIGNAL_TYPES

    def describe(self) -> dict:
        return {"name": self.name, "allowed": list(self.allowed), "pin_parameter": self.pin_parameter}


@dataclass(frozen=True)
class Param:
    """A parameter (ParameterSpec). default is the value an omitted parameter takes; min and max
    bound an "int" (inclusive); enum_values lists an "enum"'s values, the default among them."""

    name: str
    type: str
    default: Any = None
    description: str = ""
    min: int | float | None = None
    max: int | float | None = None
    enum_values: tuple[str, ...] = ()
    required: bool = False
    legacy: bool = False

    def read(self, params: dict):
        """The value of this parameter in params, or its default; raises SchemaError."""
        if self.type in ("value", "real"):
            return _number(params, self.name, self.default)
        if self.type == "int":
            return _integer(params, self.name, self.default, self.min, self.max)
        if self.type == "bool":
            return _flag(params, self.name, self.default)
        if self.type == "enum":
            return _choice(params, self.name, self.enum_values)
        if self.type == "array":
            return _numbers(params, self.name, self.default)
        raise SchemaError(f"parameter '{self.name}' of type {self.type} is read by its element")

    def describe(self) -> dict:
        out = {"name": self.name, "type": self.type, "description": self.description,
               "default": list(self.default) if isinstance(self.default, tuple) else self.default,
               "required": self.required}
        if self.legacy:
            out["legacy"] = True
        if self.min is not None:
            out["min"] = self.min
        if self.max is not None:
            out["max"] = self.max
        if self.enum_values:
            out["enum_values"] = list(self.enum_values)
        return out


@dataclass(frozen=True)
class Variadic:
    """Inputs whose count a parameter sets (VariadicInputSpec): an "array" parameter's length or an
    "int" parameter's value, from min_count to max_count."""

    count_parameter: str
    min_count: int
    max_count: int
    port_name_template: str = "in_{index}"
    signal_type: str = "F64"
    type_rule: str = ""

    def describe(self) -> dict:
        return {"enabled": True, "min_count": self.min_count, "max_count": self.max_count,
                "port_name_template": self.port_name_template, "signal_type": self.signal_type,
                "type_rule": self.type_rule, "count_parameter": self.count_parameter}


# The pinning parameters of s7core's generic elements (builtin_element_registry.cpp).
TYPE = Param("type", "type", None, "Pins the element's type (otherwise inferred from connections)")
OVERFLOW = Param("overflow", "enum", "saturate",
                 "Integer arithmetic overflow: saturate clamps to the type's limits; wrap uses modular "
                 "arithmetic. Does not change floating-point arithmetic.", enum_values=("saturate", "wrap"))
ACCUMULATOR_TYPE = Param("accumulator_type", "type", None,
                         "Type of the arithmetic and the output; must hold every input value exactly "
                         "(default: the input type)")
T = TypeVariable("T")
A = TypeVariable("A", "accumulator_type")
GENERIC = (TYPE,)  # the parameters of an element generic over T, before its own
