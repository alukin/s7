# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Schema -> validate -> compile -> step.

Mirrors s7core (validation.cpp, rate_inference.cpp, runtime.cpp) for F64 signals only: every value is
a Python float. Elements with direct feedthrough run in causal order; causal-memory elements stage
what they compute and publish it at the start of the next step, so a cycle must pass through one of
them and every value read after a step belongs to that step (doc/S7_Formal_Model.md, "Causal Memory").
The element types come from the caller: the core knows no element library.

Building is split by concern: wiring.py (elements, connections), schedule.py (causal order),
rate_groups.py (rates), trapezoid.py (the coupled stage). This module steps the result.
"""
from collections.abc import Mapping
from fractions import Fraction

from .element import Element
from .rate_groups import require_model_time_rate, resolve_rates
from .schedule import causal_order
from .trapezoid import TrapezoidStage
from .wiring import build_wiring


class Graph:
    """A compiled schema: elements in execution order, ready to step.

    rate seeds every element that pins no rate of its own (schema_rate(), or a
    deployment's model-time step); a connected set of elements with neither is refused.
    types maps each element type name to its class: the element library in use.
    """

    def __init__(self, schema: dict, rate: Fraction | None = None, *, types: Mapping[str, type[Element]]):
        self.wiring = build_wiring(schema, types)
        self.name, self.elements, self.drivers = self.wiring.name, self.wiring.elements, self.wiring.drivers
        self.causal_order = causal_order(self.wiring)
        self.memory_order = [e for e in self.elements.values() if e.memory]
        resolve_rates(self.wiring, rate)
        self.trapezoid_stage = TrapezoidStage(self.wiring, self.memory_order, self.causal_order)
        self._staged: list[list[float]] | None = None  # memory outputs to publish at the next step
        self.steps = 0

    @property
    def trapezoids(self) -> list[Element]:
        return self.trapezoid_stage.members

    @property
    def trapezoid_rhs(self) -> list[Element]:
        return self.trapezoid_stage.rhs

    def require_model_time_rate(self, rate: Fraction) -> None:
        """The run's model-time rate must be every element's own (NodeRuntime::set_model_time_rate)."""
        require_model_time_rate(self.elements.values(), rate)

    def step(self) -> None:
        """One step, all or nothing for published outputs (NodeRuntime::step): a step that raises
        leaves every output as it was, and the next step retries it. Private element state
        (an Accumulator's sum, a TrapezoidIntegrator's previous sample) is not rolled back."""
        memory, inputs = self.memory_order, self.wiring.inputs
        staged, self._staged = self._staged, None
        if staged is not None:
            published = [e.outputs[:] for e in memory]
            for element, values in zip(memory, staged):
                element.outputs[:] = values
        causal = [e.outputs[:] for e in self.causal_order]
        try:
            for element in self.causal_order:
                element.step(inputs(element))
            solved = self.trapezoid_stage.solve()
        except Exception:
            for element, values in zip(self.causal_order, causal):
                element.outputs[:] = values
            if staged is not None:
                for element, values in zip(memory, published):
                    element.outputs[:] = values
                self._staged = staged
            raise
        # Causal memory computes its next outputs from this step's inputs, then shows
        # this step's outputs again until the next step publishes them.
        next_outputs = []
        for element in memory:
            if element in solved:
                next_outputs.append(solved[element])
                continue
            current = element.outputs[:]
            try:
                element.step(inputs(element))
            finally:
                next_outputs.append(element.outputs[:])
                element.outputs[:] = current
        self._staged = next_outputs if memory else None
        self.steps += 1

    def close(self) -> None:
        """Releases the graph's streams, so that a graph replacing it continues them."""
        for element in self.elements.values():
            close = getattr(element, "close", None)  # elements that hold a resource, such as a stream
            if close is not None:
                close()

    def sample(self, element_id: str, port: int = 0) -> float:
        return self.elements[element_id].outputs[port]
