# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Schema -> validate -> compile -> step.

Mirrors s7core (validation.cpp, rate_inference.cpp, runtime.cpp) for F64 signals only: every value is
a Python float. Elements with direct feedthrough run in causal order; causal-memory elements stage
what they compute and publish it at the start of the next step, so a cycle must pass through one of
them and every value read after a step belongs to that step (doc/S7_Formal_Model.md, "Causal Memory").
The element types come from the caller: the core knows no element library.
"""
from collections.abc import Mapping
from fractions import Fraction

from .element import Element
from .errors import SchemaError
from .params import _check_params
from .rate import rate_text

# The coupled trapezoid stage: at most this many fixed-point corrections per step
# (s7l2/docs/TRAPEZOID_INTEGRATION.md); each state's convergence test is its own.
TRAPEZOID_ITERATIONS = 64


def _index(value, what: str) -> int:
    if value is None:
        return 0  # an omitted port is port 0, as in s7runtime's schema loader
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise SchemaError(f"{what} must be a non-negative integer")
    return value


class Graph:
    """A compiled schema: elements in execution order, ready to step.

    rate seeds every element that pins no rate of its own (schema_rate(), or a
    deployment's model-time step); a connected set of elements with neither is refused.
    types maps each element type name to its class: the element library in use.
    """

    def __init__(self, schema: dict, rate: Fraction | None = None, *, types: Mapping[str, type[Element]]):
        self.name = schema.get("name", "schema") if isinstance(schema.get("name"), str) else "schema"
        self.elements: dict[str, Element] = {}
        elements = schema.get("elements", [])
        connections = schema.get("connections", [])
        if not isinstance(elements, list) or not isinstance(connections, list):
            raise SchemaError("'elements' and 'connections' must be arrays")

        for entry in elements:
            if not isinstance(entry, dict) or not isinstance(entry.get("id"), str) or not entry["id"]:
                raise SchemaError("every element needs a non-empty string 'id'")
            element_id, type_name = entry["id"], entry.get("type")
            if element_id in self.elements:
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
                self.elements[element_id] = element_type(element_id, params)
            except SchemaError as error:
                raise SchemaError(f"element '{element_id}': {error}") from None

        # drivers[element][port] = (driving element, its output port); each input has exactly one driver.
        self.drivers: dict[str, list[tuple[Element, int] | None]] = {
            e.id: [None] * e.input_count for e in self.elements.values()}
        for c in connections:
            if not isinstance(c, dict):
                raise SchemaError("a connection must be an object")
            source, target = self.elements.get(c.get("from_element")), self.elements.get(c.get("to_element"))
            if source is None or target is None:
                raise SchemaError(f"connection {c.get('from_element')} -> {c.get('to_element')}: unknown element")
            from_port = _index(c.get("from_port"), "from_port")
            if from_port >= source.output_count:
                raise SchemaError(f"connection from '{source.id}': output port {from_port} does not exist")
            port = _index(c.get("to_port"), "to_port")
            if port >= target.input_count:
                raise SchemaError(f"connection to '{target.id}': input port {port} does not exist")
            if self.drivers[target.id][port] is not None:
                raise SchemaError(f"input '{target.id}':{port} has more than one driver")
            self.drivers[target.id][port] = (source, from_port)
        for element_id, ports in self.drivers.items():
            for port, driver in enumerate(ports):
                if driver is None:
                    raise SchemaError(f"input '{element_id}':{port} is not connected")

        self.causal_order = self._causal_order()
        self.memory_order = [e for e in self.elements.values() if e.memory]
        self._resolve_rates(rate)
        # Causal memory that takes part in the coupled trapezoid stage instead of stepping alone.
        self.trapezoids = [e for e in self.memory_order if getattr(e, "trapezoid_stage", False)]
        self.trapezoid_rhs = self._trapezoid_rhs()
        self._staged: list[list[float]] | None = None  # memory outputs to publish at the next step
        self.steps = 0

    def _causal_order(self) -> list[Element]:
        """Topological order of feedthrough elements; an edge out of causal memory does not count."""
        order, state = [], {}

        def visit(element: Element, path: list[str]):
            if state.get(element.id) == "done":
                return
            if state.get(element.id) == "visiting":
                raise SchemaError("cycle without causal memory: " + " -> ".join(path + [element.id]))
            state[element.id] = "visiting"
            for driver, _ in self.drivers[element.id]:
                if not driver.memory:
                    visit(driver, path + [element.id])
            state[element.id] = "done"
            order.append(element)

        for element in self.elements.values():
            if not element.memory:
                visit(element, [])
        return order

    def _resolve_rates(self, seed: Fraction | None) -> None:
        """RateResolver: connected elements share one rate, pinned by an element or the seed."""
        root = {element_id: element_id for element_id in self.elements}

        def find(element_id: str) -> str:
            while root[element_id] != element_id:
                root[element_id] = root[root[element_id]]
                element_id = root[element_id]
            return element_id

        for element_id, ports in self.drivers.items():
            for driver, _ in ports:
                root[find(driver.id)] = find(element_id)

        pinned: dict[str, tuple[Fraction, str]] = {}
        members: dict[str, list[str]] = {}
        for element in self.elements.values():
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

    def require_model_time_rate(self, rate: Fraction) -> None:
        """The run's model-time rate must be every element's own (NodeRuntime::set_model_time_rate)."""
        for element in self.elements.values():
            declared = element.rate()
            if declared is not None and declared != rate:
                raise SchemaError(f"model-time rate {rate_text(rate)} conflicts with element '{element.id}' at "
                                  f"{rate_text(declared)}; match the diagram's dt and the element's discretization")

    def _trapezoid_rhs(self) -> list[Element]:
        """The stateless elements an implicit TrapezoidIntegrator's derivative depends on through
        a trial state; the solver reevaluates them, so none may keep state (validation.cpp)."""
        varying: dict[str, bool] = {}
        visiting: set[str] = set()

        def visit(element: Element) -> bool:
            if element in self.trapezoids:
                return True
            if element.memory:
                return False
            if element.id in varying:
                return varying[element.id]
            if element.id in visiting:
                raise SchemaError("cycle without causal memory in a TrapezoidIntegrator derivative graph")
            visiting.add(element.id)
            depends = False
            for driver, _ in self.drivers[element.id]:
                depends = visit(driver) or depends
            visiting.discard(element.id)
            varying[element.id] = depends
            if depends and element.stateful:
                raise SchemaError(f"an implicit TrapezoidIntegrator's derivative depends on stateful direct-feedthrough "
                                  f"element '{element.id}'; only stateless operations may be reevaluated")
            return depends

        for integrator in self.trapezoids:
            for driver, _ in self.drivers[integrator.id]:
                visit(driver)
        return [e for e in self.causal_order if varying.get(e.id)]

    def _inputs(self, element: Element) -> list[float]:
        return [driver.outputs[port] for driver, port in self.drivers[element.id]]

    def step(self) -> None:
        """One step, all or nothing for published outputs (NodeRuntime::step): a step that raises
        leaves every output as it was, and the next step retries it. Private element state
        (an Accumulator's sum, a TrapezoidIntegrator's previous sample) is not rolled back."""
        memory = self.memory_order
        staged, self._staged = self._staged, None
        if staged is not None:
            published = [e.outputs[:] for e in memory]
            for element, values in zip(memory, staged):
                element.outputs[:] = values
        causal = [e.outputs[:] for e in self.causal_order]
        try:
            for element in self.causal_order:
                element.step(self._inputs(element))
            solved = self._solve_trapezoids()
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
                element.step(self._inputs(element))
            finally:
                next_outputs.append(element.outputs[:])
                element.outputs[:] = current
        self._staged = next_outputs if memory else None
        self.steps += 1

    def _solve_trapezoids(self) -> dict[Element, list[float]]:
        """The implicit trapezoid stage: all implicit states solved together by fixed-point
        iteration, reevaluating their derivative cone; every trial output is restored after."""
        if not self.trapezoids:
            return {}
        trial = self.trapezoids + self.trapezoid_rhs
        base = [e.outputs[:] for e in trial]
        try:
            for integrator in self.trapezoids:
                integrator.prepare_trapezoid_step(self._inputs(integrator))
            for integrator in self.trapezoids:
                integrator.publish_trapezoid_candidate()
            for _ in range(TRAPEZOID_ITERATIONS):
                for element in self.trapezoid_rhs:
                    element.step(self._inputs(element))
                converged = True
                # Every correction reads the same iterate: publish only after all are formed.
                for integrator in self.trapezoids:
                    converged = integrator.correct_trapezoid_candidate(self._inputs(integrator)) and converged
                for integrator in self.trapezoids:
                    integrator.publish_trapezoid_candidate()
                if converged:
                    return {integrator: integrator.outputs[:] for integrator in self.trapezoids}
            raise RuntimeError(f"implicit TrapezoidIntegrator did not converge after {TRAPEZOID_ITERATIONS} "
                               "iterations; reduce dt (the fixed-point solver needs a contractive step)")
        finally:
            for element, values in zip(trial, base):
                element.outputs[:] = values

    def close(self) -> None:
        """Releases the graph's streams, so that a graph replacing it continues them."""
        for element in self.elements.values():
            close = getattr(element, "close", None)  # elements that hold a resource, such as a stream
            if close is not None:
                close()

    def sample(self, element_id: str, port: int = 0) -> float:
        return self.elements[element_id].outputs[port]
