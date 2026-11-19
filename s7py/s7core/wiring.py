# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Schema -> element instances and their connections (validation.cpp's structural checks).

Every element is built from its type with checked parameters; every input has exactly one
driver. Causality and rates are checked afterwards, on the wiring this returns.
"""
from collections.abc import Mapping

from .element import Element
from .errors import SchemaError
from .params import _check_params


def _index(value, what: str) -> int:
    if value is None:
        return 0  # an omitted port is port 0, as in s7runtime's schema loader
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise SchemaError(f"{what} must be a non-negative integer")
    return value


class Wiring:
    """The elements of a schema, by id in declaration order, and the driver of each input."""

    def __init__(self, name: str, elements: dict[str, Element], drivers: dict[str, list[tuple[Element, int]]]):
        self.name = name
        self.elements = elements
        self.drivers = drivers  # drivers[element id][input port] = (driving element, its output port)

    def inputs(self, element: Element) -> list[float]:
        """The values currently on an element's inputs."""
        return [driver.outputs[port] for driver, port in self.drivers[element.id]]


def build_wiring(schema: dict, types: Mapping[str, type[Element]]) -> Wiring:
    """Instantiates every element of schema from types and connects them; raises SchemaError."""
    name = schema.get("name", "schema") if isinstance(schema.get("name"), str) else "schema"
    built: dict[str, Element] = {}
    elements = schema.get("elements", [])
    connections = schema.get("connections", [])
    if not isinstance(elements, list) or not isinstance(connections, list):
        raise SchemaError("'elements' and 'connections' must be arrays")

    for entry in elements:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str) or not entry["id"]:
            raise SchemaError("every element needs a non-empty string 'id'")
        element_id, type_name = entry["id"], entry.get("type")
        if element_id in built:
            raise SchemaError(f"duplicate element id '{element_id}'")
        if "schema" in entry:
            raise SchemaError(f"element '{element_id}': nested schemas are not supported by s7py")
        if type_name not in types:
            raise SchemaError(f"element '{element_id}': unknown element type '{type_name}'")
        params = entry.get("params", {})
        if not isinstance(params, dict):
            raise SchemaError(f"element '{element_id}': 'params' must be an object")
        element_type = types[type_name]
        try:
            _check_params(params, element_type.params)
            built[element_id] = element_type(element_id, params)
        except SchemaError as error:
            raise SchemaError(f"element '{element_id}': {error}") from None

    drivers: dict[str, list[tuple[Element, int] | None]] = {e.id: [None] * e.input_count for e in built.values()}
    for c in connections:
        if not isinstance(c, dict):
            raise SchemaError("a connection must be an object")
        source, target = built.get(c.get("from_element")), built.get(c.get("to_element"))
        if source is None or target is None:
            raise SchemaError(f"connection {c.get('from_element')} -> {c.get('to_element')}: unknown element")
        from_port = _index(c.get("from_port"), "from_port")
        if from_port >= source.output_count:
            raise SchemaError(f"connection from '{source.id}': output port {from_port} does not exist")
        port = _index(c.get("to_port"), "to_port")
        if port >= target.input_count:
            raise SchemaError(f"connection to '{target.id}': input port {port} does not exist")
        if drivers[target.id][port] is not None:
            raise SchemaError(f"input '{target.id}':{port} has more than one driver")
        drivers[target.id][port] = (source, from_port)
    for element_id, ports in drivers.items():
        for port, driver in enumerate(ports):
            if driver is None:
                raise SchemaError(f"input '{element_id}':{port} is not connected")
    return Wiring(name, built, drivers)
