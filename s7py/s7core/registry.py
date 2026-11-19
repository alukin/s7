# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The element types a graph may use, by schema type name (s7::ElementRegistry, without a global).

A registry is built by whoever runs graphs (a library, a node, a test) and passed to Graph; it
checks each type's declaration when it is registered, and describes every type it holds.
"""
from collections.abc import Iterator, Mapping

from .element import Element
from .errors import DeclarationError, SchemaError
from .spec import PARAMETER_TYPES
from .trapezoid import TrapezoidIntegration


class Registry(Mapping[str, type[Element]]):
    """Element types by name; a Mapping, so Graph(schema, rate, types=registry)."""

    def __init__(self, *sources: Mapping[str, type[Element]]):
        self._types: dict[str, type[Element]] = {}
        for source in sources:
            for name, element_type in source.items():
                self.register(element_type, name)

    def register(self, element_type: type[Element], name: str | None = None) -> type[Element]:
        """Adds element_type under name (its type_name by default) and returns it, so it can
        decorate a class; raises DeclarationError on a duplicate name or a bad declaration."""
        name = element_type.type_name if name is None and isinstance(element_type, type) else name
        if not isinstance(name, str) or not name:
            raise DeclarationError("an element type needs a non-empty name")
        if name in self._types:
            raise DeclarationError(f"element type '{name}' is already registered")
        check_declaration(element_type, name)
        self._types[name] = element_type
        return element_type

    def __getitem__(self, name: str) -> type[Element]:
        return self._types[name]

    def __iter__(self) -> Iterator[str]:
        return iter(self._types)

    def __len__(self) -> int:
        return len(self._types)

    def describe(self) -> list[dict]:
        """Every type as an element catalog entry, in registration order."""
        return [element_type.describe(name) for name, element_type in self._types.items()]


def check_declaration(element_type, name: str) -> None:
    """Raises DeclarationError unless element_type is an Element whose declaration holds together."""
    def fail(reason: str):
        raise DeclarationError(f"element type '{name}': {reason}")

    if not (isinstance(element_type, type) and issubclass(element_type, Element)):
        fail("must be a subclass of s7core.Element")
    if element_type.step is Element.step and not issubclass(element_type, TrapezoidIntegration):
        fail("does not implement step()")
    for what, names in (("parameter", [p.name for p in element_type.parameters]),
                        ("input port", [p.name for p in element_type.input_ports]),
                        ("output port", [p.name for p in element_type.output_ports]),
                        ("type variable", [v.name for v in element_type.type_variables])):
        repeated = sorted({n for n in names if names.count(n) > 1})
        if repeated:
            fail(f"{what} '{repeated[0]}' is declared twice")
    variables = {v.name for v in element_type.type_variables}
    parameters = {p.name: p for p in element_type.parameters}
    for variable in element_type.type_variables:
        pin = parameters.get(variable.pin_parameter)
        if pin is None or pin.type != "type":
            fail(f"type variable '{variable.name}' needs a \"type\" parameter '{variable.pin_parameter}'")
    variadic = element_type.variadic_inputs
    rules = [p.type_rule for p in element_type.input_ports + element_type.output_ports]
    rules += [variadic.type_rule] if variadic is not None else []
    for rule in rules:
        if rule and rule not in variables:
            fail(f"type rule '{rule}' names no declared type variable")
    for parameter in element_type.parameters:
        if parameter.type not in PARAMETER_TYPES:
            fail(f"parameter '{parameter.name}' has unknown type '{parameter.type}' "
                 f"(known: {', '.join(PARAMETER_TYPES)})")
        if parameter.type == "enum" and (not parameter.enum_values or parameter.default != parameter.enum_values[0]):
            fail(f"enum parameter '{parameter.name}' must list its values, its default first")
        if parameter.type == "int" and not (isinstance(parameter.min, int) and isinstance(parameter.max, int)):
            fail(f"int parameter '{parameter.name}' needs integer min and max")
        if parameter.type in ("value", "real", "int", "bool", "enum", "array") and parameter.default is not None:
            try:
                parameter.read({})
            except SchemaError as error:
                fail(f"default of parameter '{parameter.name}' is invalid: {error}")
    if variadic is not None:
        count = parameters.get(variadic.count_parameter)
        if count is None or count.type not in ("array", "int"):
            fail(f"variadic inputs need an \"array\" or \"int\" parameter '{variadic.count_parameter}'")
        if not 0 < variadic.min_count <= variadic.max_count:
            fail("variadic inputs need 0 < min_count <= max_count")
    elif element_type.input_count != len(element_type.input_ports):
        fail(f"declares {len(element_type.input_ports)} input ports but input_count {element_type.input_count}")
    if element_type.output_count != len(element_type.output_ports):
        fail(f"declares {len(element_type.output_ports)} output ports but output_count {element_type.output_count}")
