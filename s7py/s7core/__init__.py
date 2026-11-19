# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The S7 core in Python: the element contract, validation, causal ordering and stepping.

It knows no element library and no network: element types are declared with s7core.spec,
collected in a Registry and passed to Graph (see s7lib for the built-in elements, s7node for a
node that serves them).
"""
from .element import Element
from .errors import DeclarationError, SchemaError
from .graph import Graph
from .registry import Registry
from .spec import GENERIC, Param, Port, T, TypeVariable, Variadic
from .trapezoid import TrapezoidIntegration
from .rate import rate_from_hz, rate_from_seconds, rate_from_step_ns, rate_text, schema_rate

__all__ = ["GENERIC", "DeclarationError", "Element", "Graph", "Param", "Port", "Registry", "SchemaError", "T",
           "TrapezoidIntegration", "TypeVariable", "Variadic", "rate_from_hz", "rate_from_seconds",
           "rate_from_step_ns", "rate_text", "schema_rate"]
