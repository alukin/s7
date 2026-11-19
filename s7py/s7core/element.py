# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The element contract: ports, flags and step()."""
from fractions import Fraction

from .params import GENERIC


class Element:
    """One instance in a graph. Subclasses set their ports and flags and implement step()."""

    params = GENERIC
    memory = False  # causal memory (no direct feedthrough): outputs depend only on earlier inputs
    stateful = False  # keeps state between steps; the trapezoid solver must not reevaluate it
    input_count = 0
    output_count = 1

    def __init__(self, element_id: str, params: dict):
        self.id = element_id
        self.outputs = [0.0] * self.output_count

    def rate(self) -> Fraction | None:
        """The rate this element pins by itself (Element::declared_rate), if any."""
        return None

    def step(self, inputs: list[float]) -> None:
        raise NotImplementedError
