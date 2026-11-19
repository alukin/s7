# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Causal order: which direct-feedthrough element runs before which within a step.

An edge out of causal memory does not order anything (memory publishes at the start of the
step), so a cycle must pass through causal memory (doc/S7_Formal_Model.md, "Causal Memory").
"""
from .element import Element
from .errors import SchemaError
from .wiring import Wiring


def causal_order(wiring: Wiring) -> list[Element]:
    """Topological order of the feedthrough elements, drivers first; raises SchemaError on a cycle
    without causal memory. Ties keep declaration order."""
    order, state = [], {}

    def visit(element: Element, path: list[str]):
        if state.get(element.id) == "done":
            return
        if state.get(element.id) == "visiting":
            raise SchemaError("cycle without causal memory: " + " -> ".join(path + [element.id]))
        state[element.id] = "visiting"
        for driver, _ in wiring.drivers[element.id]:
            if not driver.memory:
                visit(driver, path + [element.id])
        state[element.id] = "done"
        order.append(element)

    for element in wiring.elements.values():
        if not element.memory:
            visit(element, [])
    return order
