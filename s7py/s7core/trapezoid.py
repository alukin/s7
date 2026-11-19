# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The coupled implicit-trapezoid stage (s7/trapezoid.hpp, runtime.cpp; s7l2/docs/TRAPEZOID_INTEGRATION.md).

Every enabled state is solved together by fixed-point iteration, reevaluating only the stateless
elements its derivative depends on. Candidates exist only inside solve(); causal-memory
publication is unchanged.
"""
from abc import ABC, abstractmethod

from .element import Element
from .errors import SchemaError
from .wiring import Wiring

# At most this many fixed-point corrections per step; each state's convergence test is its own.
TRAPEZOID_ITERATIONS = 64


class TrapezoidIntegration(ABC):
    """An element whose state the stage solves (s7::TrapezoidIntegration). While enabled, the
    element is causal memory and its step() is not called: the stage steps it, as in s7core.
    Preparation and publication are separate, so coupled states always read the same iterate."""

    @abstractmethod
    def implicit_trapezoid_enabled(self) -> bool: ...

    @abstractmethod
    def prepare_trapezoid_step(self, inputs: list[float]) -> None:
        """Forms the first candidate from the published state and its current input."""

    @abstractmethod
    def correct_trapezoid_candidate(self, inputs: list[float]) -> bool:
        """Forms the next candidate from the input at the published one; True once converged."""

    @abstractmethod
    def publish_trapezoid_candidate(self) -> None:
        """Shows the current candidate on the element's outputs."""


class TrapezoidStage:
    """The implicit states of a graph and the derivative cone the stage reevaluates."""

    def __init__(self, wiring: Wiring, memory: list[Element], order: list[Element]):
        self.wiring = wiring
        self.members = [e for e in memory if isinstance(e, TrapezoidIntegration) and e.implicit_trapezoid_enabled()]
        self.rhs = self._rhs(order)

    def _rhs(self, order: list[Element]) -> list[Element]:
        """The feedthrough elements a member's derivative depends on through a trial state, in causal
        order; the stage reevaluates them, so none may keep state (validation.cpp)."""
        varying: dict[str, bool] = {}
        visiting: set[str] = set()

        def visit(element: Element) -> bool:
            if element in self.members:
                return True
            if element.memory:
                return False
            if element.id in varying:
                return varying[element.id]
            if element.id in visiting:
                raise SchemaError("cycle without causal memory in a TrapezoidIntegrator derivative graph")
            visiting.add(element.id)
            depends = False
            for driver, _ in self.wiring.drivers[element.id]:
                depends = visit(driver) or depends
            visiting.discard(element.id)
            varying[element.id] = depends
            if depends and element.stateful:
                raise SchemaError(f"an implicit TrapezoidIntegrator's derivative depends on stateful direct-feedthrough "
                                  f"element '{element.id}'; only stateless operations may be reevaluated")
            return depends

        for member in self.members:
            for driver, _ in self.wiring.drivers[member.id]:
                visit(driver)
        return [e for e in order if varying.get(e.id)]

    def solve(self) -> dict[Element, list[float]]:
        """Each member's solved next outputs; every trial output is restored afterwards."""
        if not self.members:
            return {}
        inputs = self.wiring.inputs
        trial = self.members + self.rhs
        base = [e.outputs[:] for e in trial]
        try:
            for member in self.members:
                member.prepare_trapezoid_step(inputs(member))
            for member in self.members:
                member.publish_trapezoid_candidate()
            for _ in range(TRAPEZOID_ITERATIONS):
                for element in self.rhs:
                    element.step(inputs(element))
                converged = True
                # Every correction reads the same iterate: publish only after all are formed.
                for member in self.members:
                    converged = member.correct_trapezoid_candidate(inputs(member)) and converged
                for member in self.members:
                    member.publish_trapezoid_candidate()
                if converged:
                    return {member: member.outputs[:] for member in self.members}
            raise RuntimeError(f"implicit TrapezoidIntegrator did not converge after {TRAPEZOID_ITERATIONS} "
                               "iterations; reduce dt (the fixed-point solver needs a contractive step)")
        finally:
            for element, values in zip(trial, base):
                element.outputs[:] = values
