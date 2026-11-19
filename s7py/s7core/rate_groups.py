# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""One rate per connected set of elements (rate_inference.cpp, doc/S7_Sample_Rate_Model.md)."""
from fractions import Fraction

from .element import Element
from .errors import SchemaError
from .rate import rate_text
from .wiring import Wiring


def resolve_rates(wiring: Wiring, seed: Fraction | None) -> None:
    """Connected elements share one rate, pinned by an element or by seed; raises SchemaError
    on a conflict, or when seed is None and a connected set pins no rate."""
    root = {element_id: element_id for element_id in wiring.elements}

    def find(element_id: str) -> str:
        while root[element_id] != element_id:
            root[element_id] = root[root[element_id]]
            element_id = root[element_id]
        return element_id

    for element_id, ports in wiring.drivers.items():
        for driver, _ in ports:
            root[find(driver.id)] = find(element_id)

    pinned: dict[str, tuple[Fraction, str]] = {}
    members: dict[str, list[str]] = {}
    for element in wiring.elements.values():
        group = find(element.id)
        members.setdefault(group, []).append(element.id)
        try:
            declared = element.rate()
        except SchemaError as error:
            raise SchemaError(f"element '{element.id}': {error}") from None
        if declared is None:
            continue
        if group not in pinned:
            pinned[group] = (declared, element.id)
        elif pinned[group][0] != declared:
            rate, by = pinned[group]
            raise SchemaError(f"rate conflict: '{element.id}' declares {rate_text(declared)} but '{by}', "
                              f"connected to it, declares {rate_text(rate)}; insert a rate-changing element")
    if seed is None:
        for group, ids in members.items():
            if group not in pinned:
                raise SchemaError(f"the rate of '{ids[0]}' cannot be determined (connected to: {', '.join(ids)}); "
                                  "pin a rate on one of them (e.g. an integrator's \"dt\"), or set the schema's dt")


def require_model_time_rate(elements, rate: Fraction) -> None:
    """The run's model-time rate must be every element's own (NodeRuntime::set_model_time_rate)."""
    for element in elements:
        declared = element.rate()
        if declared is not None and declared != rate:
            raise SchemaError(f"model-time rate {rate_text(rate)} conflicts with element '{element.id}' at "
                              f"{rate_text(declared)}; match the diagram's dt and the element's discretization")
