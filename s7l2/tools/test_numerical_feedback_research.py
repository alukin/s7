#!/usr/bin/env python3
"""Contract checks for the isolated numerical research runner."""

import math
import unittest

from numerical_feedback_research import Graph, StepFailure, model, run_case, transition


class ResearchContractTests(unittest.TestCase):
    def test_forward_euler_is_one_update(self):
        graph, initial, _ = model("oscillator")
        endpoint, residual, used = transition(graph, initial, 0.1, "euler", 0)
        self.assertEqual(endpoint, [1.0, -0.1])
        self.assertTrue(math.isnan(residual))
        self.assertEqual((used, graph.derivative_evaluations), (0, 1))

    def test_backward_euler_is_distinct_implicit_equation(self):
        graph, initial, _ = model("linear")
        endpoint, residual, used = transition(graph, initial, 0.1, "backward", -1)
        self.assertAlmostEqual(endpoint[0], 1/12, delta=2e-12)
        self.assertLessEqual(residual, 1)
        self.assertGreater(used, 1)

    def test_trapezoid_matches_linear_closed_form(self):
        graph, initial, _ = model("linear")
        endpoint, residual, _ = transition(graph, initial, 0.1, "trapezoid", -1)
        self.assertAlmostEqual(endpoint[0], 1/11, delta=2e-12)
        self.assertLessEqual(residual, 1)

    def test_trial_region_excludes_held_source(self):
        graph, _, _ = model("linear")
        self.assertEqual([graph.ops[i].name for i in graph.region], ["minus_2x", "rhs"])

    def test_direct_cycle_and_unsafe_node_rejected(self):
        graph = Graph(("x",))
        with self.assertRaisesRegex(ValueError, "direct cycle"):
            graph.add("self", "sum", 1)
        with self.assertRaisesRegex(ValueError, "non-pure"):
            graph.add("history", "filter", 0)

    def test_noncontractive_iteration_rejects_step(self):
        graph, initial, _ = model("linear")
        with self.assertRaisesRegex(StepFailure, "nonconvergence"):
            transition(graph, initial, 1.0, "backward", -1)
        self.assertEqual(initial, [0.0])

    def test_multiple_and_absent_implicit_roots(self):
        graph = Graph(("x",))
        square = graph.add("square", "product", 0, 0)
        graph.bind(square)
        # z = z² has two roots, 0 and 1; the Euler predictor selects 0.
        endpoint, residual, _ = transition(graph, [0.0], 1.0, "backward", -1)
        self.assertEqual(endpoint, [0.0])
        self.assertEqual(residual, 0)
        # z = 1 + z² has no real root; bounded iteration must fail.
        with self.assertRaisesRegex((StepFailure, ArithmeticError), "non-finite|nonconvergence"):
            transition(graph, [1.0], 1.0, "backward", -1)

    def test_reevaluation_without_trial_state_is_identical(self):
        graph, initial, _ = model("oscillator")
        base = graph.evaluate(initial)
        self.assertEqual(graph.evaluate(initial, trial=True), base)
        self.assertEqual(graph.evaluate(initial, trial=True), base)

    def test_failed_step_reports_context_and_preserves_publication(self):
        result = run_case("linear", 1.0, 1.0, "backward", -1)
        self.assertEqual(result["accepted_steps"], 0)
        self.assertEqual(result["failure"]["step"], 1)
        self.assertEqual(result["failure"]["published"], [0.0])
        self.assertIn("nonconvergence", result["failure"]["message"])


if __name__ == "__main__":
    unittest.main()
