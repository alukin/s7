# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The element API: declaring, registering and describing an element type."""
import sys
import unittest
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from s7core import (GENERIC, DeclarationError, Element, Graph, Param, Port, Registry, SchemaError, T,  # noqa: E402
                    TypeVariable, Variadic)
from s7lib import LIBRARY  # noqa: E402


class Offset(Element):
    """y = x + offset: a custom direct-feedthrough element."""

    description = "Adds a constant to its input"
    input_ports = (Port("in", "Input"),)
    output_ports = (Port("out", "Input plus offset"),)
    parameters = (Param("offset", "real", 0.5, "Added to the input"),)

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.offset = self.read(params, "offset")

    def step(self, inputs):
        self.outputs[0] = inputs[0] + self.offset


class Hold(Element):
    """A custom causal-memory element: outputs its previous input, so it may close a loop."""

    input_ports = (Port("in"),)
    memory = True
    stateful = True

    def step(self, inputs):
        self.outputs[0] = inputs[0]


def types(*extra):
    registry = Registry(LIBRARY)
    for element_type in extra:
        registry.register(element_type)
    return registry


def schema(elements, connections):
    return {"elements": elements,
            "connections": [{"from_element": a, "to_element": b, "to_port": port} for a, b, port in connections]}


class CustomElementTest(unittest.TestCase):
    def test_runs_with_the_built_in_elements(self):
        g = Graph(schema([{"id": "c", "type": "Constant", "params": {"value": 2}},
                          {"id": "o", "type": "Offset"}, {"id": "p", "type": "Offset", "params": {"offset": -3}}],
                         [("c", "o", 0), ("o", "p", 0)]), Fraction(1), types=types(Offset))
        g.step()
        self.assertEqual((g.sample("o"), g.sample("p")), (2.5, -0.5))

    def test_parameters_are_checked_against_the_declaration(self):
        registry = types(Offset, Hold)
        with self.assertRaisesRegex(SchemaError, r"element 'o': unknown parameter 'gain' \(known: offset\)"):
            Graph(schema([{"id": "o", "type": "Offset", "params": {"gain": 1}}], []), Fraction(1), types=registry)
        with self.assertRaisesRegex(SchemaError, r"element 'h': unknown parameter 'x' \(it has none\)"):
            Graph(schema([{"id": "h", "type": "Hold", "params": {"x": 1}}], []), Fraction(1), types=registry)
        with self.assertRaisesRegex(SchemaError, "parameter 'offset' must be a number"):
            Graph(schema([{"id": "o", "type": "Offset", "params": {"offset": "1"}}], []), Fraction(1), types=registry)

    def test_memory_closes_a_loop_and_feedthrough_does_not(self):
        registry = types(Offset, Hold)
        loop = [("o", "h", 0), ("h", "o", 0)]
        g = Graph(schema([{"id": "o", "type": "Offset", "params": {"offset": 1}}, {"id": "h", "type": "Hold"}], loop),
                  Fraction(1), types=registry)
        seen = []
        for _ in range(3):
            g.step()
            seen.append((g.sample("o"), g.sample("h")))
        self.assertEqual(seen, [(1.0, 0.0), (2.0, 1.0), (3.0, 2.0)])
        with self.assertRaisesRegex(SchemaError, "cycle without causal memory: o -> p -> o"):
            Graph(schema([{"id": "o", "type": "Offset"}, {"id": "p", "type": "Offset"}],
                         [("o", "p", 0), ("p", "o", 0)]), Fraction(1), types=registry)

    def test_registries_are_independent(self):
        types(Offset)
        self.assertNotIn("Offset", LIBRARY)
        with self.assertRaisesRegex(SchemaError, "unknown element type 'Offset'"):
            Graph(schema([{"id": "o", "type": "Offset"}], []), Fraction(1), types=LIBRARY)


def declare(name="Bad", **attributes):
    attributes.setdefault("step", lambda self, inputs: None)
    return type(name, (Element,), attributes)


class DeclarationTest(unittest.TestCase):
    def refused(self, element_type, reason, name=None):
        with self.assertRaisesRegex(DeclarationError, reason):
            Registry().register(element_type, name)

    def test_bad_declarations_are_refused(self):
        self.refused(int, "must be a subclass of s7core.Element", "Int")
        self.refused(type("NoStep", (Element,), {}), "'NoStep': does not implement step")
        self.refused(declare(parameters=(Param("a", "real", 0.0), Param("a", "int", 0, min=0, max=1))),
                     "parameter 'a' is declared twice")
        self.refused(declare(input_ports=(Port("x"), Port("x"))), "input port 'x' is declared twice")
        self.refused(declare(parameters=(Param("a", "complex"),)), "parameter 'a' has unknown type 'complex'")
        self.refused(declare(parameters=(Param("m", "enum", "b", enum_values=("a", "b")),)),
                     "enum parameter 'm' must list its values, its default first")
        self.refused(declare(parameters=(Param("n", "int", 1),)), "int parameter 'n' needs integer min and max")
        self.refused(declare(parameters=(Param("n", "int", 9, min=0, max=4),)),
                     "default of parameter 'n' is invalid: parameter 'n' must be at most 4")
        self.refused(declare(input_ports=(Port("in", type_rule="T"),)), "type rule 'T' names no declared type variable")
        self.refused(declare(type_variables=(TypeVariable("T"),)),
                     "type variable 'T' needs a \"type\" parameter 'type'")
        self.refused(declare(input_count=2), "declares 0 input ports but input_count 2")
        self.refused(declare(variadic_inputs=Variadic("n", 2, 4)),
                     "variadic inputs need an \"array\" or \"int\" parameter 'n'")
        with self.assertRaisesRegex(TypeError, "declare parameters"):
            declare(params=("type",))

    def test_a_name_is_registered_once(self):
        registry = types(Offset)
        with self.assertRaisesRegex(DeclarationError, "'Offset' is already registered"):
            registry.register(Offset)
        registry.register(Offset, "Shift")  # the same class under another name, as PreviousValue
        self.assertIs(registry["Shift"], Offset)

    def test_register_decorates(self):
        registry = Registry()

        @registry.register
        class Square(Element):
            input_ports = (Port("in"),)

            def step(self, inputs):
                self.outputs[0] = inputs[0] * inputs[0]

        self.assertIs(registry["Square"], Square)


# Every field of an s7core element catalog entry (element_catalog.cpp ElementCatalogService::to_json),
# plus category, which s7core groups entries by.
CATALOG_FIELDS = {"type_name", "display_name", "description", "version", "input_count", "output_count",
                  "schema_based", "input_ports", "output_ports", "type_variables", "parameters", "category"}


class DescribeTest(unittest.TestCase):
    def test_custom_element(self):
        self.assertEqual(Offset.describe(), {
            "type_name": "Offset", "display_name": "Offset", "description": "Adds a constant to its input",
            "version": "1.0.0", "input_count": 1, "output_count": 1, "schema_based": False,
            "input_ports": [{"name": "in", "description": "Input", "type": "F64", "optional": False}],
            "output_ports": [{"name": "out", "description": "Input plus offset", "type": "F64", "optional": False}],
            "type_variables": [],
            "parameters": [{"name": "offset", "type": "real", "description": "Added to the input", "default": 0.5,
                            "required": False}],
            "category": "custom"})

    def test_built_in_elements(self):
        entries = {e["type_name"]: e for e in LIBRARY.describe()}
        self.assertEqual(len(entries), len(LIBRARY))
        for name, entry in entries.items():
            self.assertLessEqual(CATALOG_FIELDS, set(entry), name)
            self.assertLessEqual(set(entry) - CATALOG_FIELDS, {"variadic_inputs"}, name)
            self.assertEqual([p["name"] for p in entry["parameters"]], list(LIBRARY[name].params), name)
        self.assertEqual(entries["PreviousValue"]["parameters"], entries["Delay"]["parameters"])
        adder = entries["Adder"]
        self.assertEqual((adder["input_count"], adder["variadic_inputs"]["count_parameter"]), (2, "weights"))
        self.assertEqual([v["allowed"] for v in adder["type_variables"]], [["F64"], ["F64"]])
        self.assertEqual(entries["Gain"]["parameters"][0]["name"], "type")


class GenericElementTest(unittest.TestCase):
    def test_a_generic_custom_element_accepts_only_F64(self):
        class Twice(Element):
            input_ports, output_ports = (Port("in", type_rule="T"),), (Port("out", type_rule="T"),)
            type_variables = (T,)
            parameters = GENERIC

            def step(self, inputs):
                self.outputs[0] = 2 * inputs[0]

        registry = types(Twice)
        with self.assertRaisesRegex(SchemaError, "type I16 is not supported"):
            Graph(schema([{"id": "t", "type": "Twice", "params": {"type": "I16"}}, {"id": "c", "type": "Constant"}],
                         [("c", "t", 0)]), Fraction(1), types=registry)


if __name__ == "__main__":
    unittest.main()
