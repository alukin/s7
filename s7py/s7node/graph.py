"""Schema -> validate -> compile -> step, for a subset of the built-in elements.

Mirrors s7core (elements.hpp, validation.cpp, runtime.cpp) for F64 signals only:
every value is a Python float. Elements with direct feedthrough run in causal
order; causal-memory elements (Delay, PreviousValue, EulerIntegrator) publish
their outputs one step later, so a cycle must pass through one of them.
"""
import math


class SchemaError(ValueError):
    pass


def _number(params: dict, name: str, default: float) -> float:
    value = params.get(name, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SchemaError(f"parameter '{name}' must be a number (s7py supports F64 signals only)")
    return float(value)


def _numbers(params: dict, name: str) -> list[float]:
    values = params.get(name, [1, 1])
    if not isinstance(values, list) or not values:
        raise SchemaError(f"parameter '{name}' must be a non-empty array of numbers")
    if len(values) == 1 or len(values) > 64:
        raise SchemaError(f"parameter '{name}' must have 2 to 64 entries")
    return [_number({name: value}, name, 0.0) for value in values]


class Element:
    """One instance in a graph. Subclasses set inputs/outputs and implement step()."""

    memory = False  # causal memory: the output depends only on earlier inputs
    inputs = 0

    def __init__(self, element_id: str, params: dict):
        self.id = element_id
        self.output = 0.0

    def step(self, inputs: list[float]) -> None:
        raise NotImplementedError


class Constant(Element):
    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.output = _number(params, "value", 0.0)

    def step(self, inputs):
        pass


class Gain(Element):
    inputs = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.factor = _number(params, "factor", 1.0)

    def step(self, inputs):
        self.output = inputs[0] * self.factor


class Adder(Element):
    def __init__(self, element_id, params):
        self.weights = _numbers(params, "weights")
        self.inputs = len(self.weights)
        super().__init__(element_id, params)

    def step(self, inputs):
        self.output = math.fsum(x * w for x, w in zip(inputs, self.weights))


class Multiply(Element):
    def __init__(self, element_id, params):
        self.factors = _numbers(params, "factors")
        self.inputs = len(self.factors)
        super().__init__(element_id, params)

    def step(self, inputs):
        product = 1.0
        for x, factor in zip(inputs, self.factors):
            product *= x * factor
        self.output = product


class Delay(Element):
    """Unit delay z^-1 ("Delay" and "PreviousValue" are the same element)."""

    memory = True
    inputs = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.output = _number(params, "initial_value", 0.0)

    def step(self, inputs):
        self.output = inputs[0]


class Accumulator(Element):
    inputs = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.output = _number(params, "initial_value", 0.0)

    def step(self, inputs):
        self.output += inputs[0]


class EulerIntegrator(Element):
    """y[n+1] = y[n] + dt * x[n]; causal memory, so it can close a loop."""

    memory = True
    inputs = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.dt = _number(params, "dt", 1.0)
        self.output = _number(params, "initial_value", 0.0)

    def step(self, inputs):
        self.output = self.output + inputs[0] * self.dt


class TrapezoidIntegrator(Element):
    """y[n] = y[n-1] + dt/2 * (x[n] + x[n-1]); direct feedthrough."""

    inputs = 1

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.half_dt = _number(params, "dt", 1.0) / 2
        self.output = _number(params, "initial_value", 0.0)
        self.previous = None

    def step(self, inputs):
        x = inputs[0]
        if self.previous is not None:
            self.output += (x + self.previous) * self.half_dt
        self.previous = x


class AnalogOscillator(Element):
    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.amplitude = _number(params, "amplitude", 1.0)
        sample_rate = _number(params, "sample_rate_hz", 1.0)
        if not sample_rate > 0:
            raise SchemaError("AnalogOscillator requires a positive sample rate")
        self.increment = 2 * math.pi * _number(params, "frequency_hz", 1.0) / sample_rate
        self.phase = _number(params, "phase", 0.0)

    def step(self, inputs):
        self.output = self.amplitude * math.sin(self.phase)
        self.phase += self.increment
        if abs(self.phase) > 2 * math.pi:
            self.phase = math.fmod(self.phase, 2 * math.pi)


ELEMENT_TYPES: dict[str, type[Element]] = {
    "Constant": Constant,
    "Gain": Gain,
    "Adder": Adder,
    "Multiply": Multiply,
    "Delay": Delay,
    "PreviousValue": Delay,
    "Accumulator": Accumulator,
    "EulerIntegrator": EulerIntegrator,
    "TrapezoidIntegrator": TrapezoidIntegrator,
    "AnalogOscillator": AnalogOscillator,
}

# Data-plane elements a node fragment may contain; s7py has no data plane yet.
STREAM_ELEMENTS = ("NetSink", "NetSource")


def _index(value, what: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise SchemaError(f"{what} must be a non-negative integer")
    return value


class Graph:
    """A compiled schema: elements in execution order, ready to step."""

    def __init__(self, schema: dict):
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
            if type_name in STREAM_ELEMENTS:
                raise SchemaError(f"element '{element_id}': data streams ({type_name}) are not supported by s7py")
            if type_name not in ELEMENT_TYPES:
                raise SchemaError(f"element '{element_id}': unknown element type '{type_name}'")
            params = entry.get("params", {})
            if not isinstance(params, dict):
                raise SchemaError(f"element '{element_id}': 'params' must be an object")
            if params.get("type", "F64") != "F64":
                raise SchemaError(f"element '{element_id}': type {params['type']} is not supported (F64 only)")
            try:
                self.elements[element_id] = ELEMENT_TYPES[type_name](element_id, params)
            except SchemaError as error:
                raise SchemaError(f"element '{element_id}': {error}") from None

        # drivers[element][port] = driving element; each input has exactly one driver.
        self.drivers: dict[str, list[Element | None]] = {e.id: [None] * e.inputs for e in self.elements.values()}
        for c in connections:
            if not isinstance(c, dict):
                raise SchemaError("a connection must be an object")
            source, target = self.elements.get(c.get("from_element")), self.elements.get(c.get("to_element"))
            if source is None or target is None:
                raise SchemaError(f"connection {c.get('from_element')} -> {c.get('to_element')}: unknown element")
            if _index(c.get("from_port"), "from_port") != 0:
                raise SchemaError(f"connection from '{source.id}': output port {c['from_port']} does not exist")
            port = _index(c.get("to_port"), "to_port")
            if port >= target.inputs:
                raise SchemaError(f"connection to '{target.id}': input port {port} does not exist")
            if self.drivers[target.id][port] is not None:
                raise SchemaError(f"input '{target.id}':{port} has more than one driver")
            self.drivers[target.id][port] = source
        for element_id, ports in self.drivers.items():
            for port, driver in enumerate(ports):
                if driver is None:
                    raise SchemaError(f"input '{element_id}':{port} is not connected")

        self.causal_order = self._causal_order()
        self.memory_order = [e for e in self.elements.values() if e.memory]
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
            for driver in self.drivers[element.id]:
                if not driver.memory:
                    visit(driver, path + [element.id])
            state[element.id] = "done"
            order.append(element)

        for element in self.elements.values():
            if not element.memory:
                visit(element, [])
        return order

    def _inputs(self, element: Element) -> list[float]:
        return [driver.output for driver in self.drivers[element.id]]

    def step(self) -> None:
        for element in self.causal_order:
            element.step(self._inputs(element))
        # Causal memory reads this step's values, then publishes for the next step.
        staged = [(element, self._inputs(element)) for element in self.memory_order]
        for element, inputs in staged:
            element.step(inputs)
        self.steps += 1

    def sample(self, element_id: str) -> float:
        return self.elements[element_id].output
