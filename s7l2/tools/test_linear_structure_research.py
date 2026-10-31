#!/usr/bin/env python3
"""Independent algebraic checks for the isolated linear research laboratory."""

import math
import unittest

import numpy as np

from linear_structure_research import (System, analytical_output, analyze, exact,
                                       frequency_response, continuous_frequency_response,
                                       lc, mechanical,
                                       rc, rlc, sequential_iteration_matrix,
                                       simultaneous_residual, simulate, trajectory, transfer,
                                       transition)
from numerical_feedback_research import Graph, model, transition as graph_transition


class LinearStructureTests(unittest.TestCase):
    def assert_matrix(self, actual, expected, tolerance=2e-13):
        np.testing.assert_allclose(actual, expected, rtol=tolerance, atol=tolerance)

    def test_rc_closed_forms_and_input_hold(self):
        system = rc(2.)
        h = 1.
        phi, gamma = exact(system, h)
        self.assert_matrix(phi, [[math.exp(-0.5)]])
        self.assert_matrix(gamma, [[1-math.exp(-0.5)]])
        expected = {"forward": (0.5, 0.5), "backward": (2/3, 1/3),
                    "trapezoid": (0.6, 0.4)}
        for method, (pole, gain) in expected.items():
            with self.subTest(method=method):
                M, G = transition(system, h, method)
                self.assert_matrix(M, [[pole]])
                self.assert_matrix(G, [[gain]])
                tf = transfer(system, M, G)
                self.assertEqual(len(tf["transfer_zeros"]), 0)
                self.assertAlmostEqual(tf["transfer_poles"][0].real, pole)

    def test_finite_polynomial_and_phase1_graph(self):
        h = 0.2
        system = lc()
        Q = h*system.A
        for k in (0, 1, 2, 4, 8, 16):
            with self.subTest(k=k):
                M, G = transition(system, h, "finite", k)
                self.assert_matrix(M, sum((np.linalg.matrix_power(Q, j) for j in range(k+2))))
                self.assert_matrix(G, h*sum((np.linalg.matrix_power(Q, j)@system.B for j in range(k+1))))
                graph, initial, _ = model("oscillator")
                actual, _, _ = graph_transition(graph, initial, h, "backward", k)
                self.assert_matrix(np.asarray(actual), M@initial)

    def test_convergence_and_failure_separate_from_stability(self):
        system = rc()
        M, _ = transition(system, 2., "backward")
        self.assertAlmostEqual(M[0, 0], 1/3)
        self.assertGreater(abs(2.*system.A[0, 0]), 1)
        residuals = [simultaneous_residual(system, np.array([1.]), 0., 2., k)[0] for k in (0, 1, 2, 4)]
        self.assertTrue(all(b > a for a, b in zip(residuals, residuals[1:])))
        stable = [simultaneous_residual(system, np.array([1.]), 0., 0.5, k)[0] for k in (0, 1, 2, 4, 8)]
        self.assertTrue(all(b < a for a, b in zip(stable, stable[1:])))
        self.assertLess(stable[-1], 0.002)
        self.assertAlmostEqual(simultaneous_residual(system, np.array([1.]), 0., 0.5, 2)[2], 4)
        self.assertEqual(analyze(system, 2., "forward")["poles"][0]["stability"], "marginal")
        self.assertEqual(analyze(system, 3., "forward")["poles"][0]["stability"], "unstable")
        self.assertEqual(analyze(system, 3., "backward")["poles"][0]["stability"], "stable")

    def test_oscillator_poles_energy_and_ranges(self):
        system = lc()
        h = 0.5
        expected = {"forward": math.sqrt(1+h*h),
                    "backward": 1/math.sqrt(1+h*h),
                    "trapezoid": 1., "symplectic": 1.}
        for method, radius in expected.items():
            poles = analyze(system, h, method)["poles"]
            self.assertAlmostEqual(poles[0]["magnitude"], radius, delta=2e-13)
        self.assertAlmostEqual(analyze(system, h, "symplectic")["poles"][0]["frequency"],
                               math.acos(1-h*h/2)/h)
        initial = np.array([1., 0.])
        for method, radius in (("forward", expected["forward"]), ("backward", expected["backward"])):
            result = simulate(system, h, method, 0, initial, 5.)
            self.assertAlmostEqual(result["energy_ratio"], radius**20, delta=1e-12)
        self.assertTrue(any(p["stability"] == "unstable" for p in analyze(system, 2.5, "symplectic")["poles"]))
        self.assertEqual(analyze(system, 1., "forward")["poles"][0]["stability"], "unstable")
        _, states, _ = trajectory(system, h, 100., "symplectic", 0, initial, "zero")
        modified_energy = 0.5*(states[:, 0]**2+states[:, 1]**2-h*states[:, 0]*states[:, 1])
        self.assertLess(np.max(np.abs(modified_energy-modified_energy[0])), 2e-12)
        physical_energy = 0.5*np.sum(states**2, axis=1)
        self.assertLess(np.max(physical_energy)-np.min(physical_energy), 0.5)

    def test_rlc_regimes_critical_multiplicity_and_equivalence(self):
        for R, classification in ((1., "underdamped"), (4., "critical"), (6., "overdamped")):
            sys = rlc(R)
            eigen = np.linalg.eigvals(sys.A)
            if classification == "underdamped":
                self.assertGreater(abs(eigen[0].imag), 1)
            elif classification == "critical":
                self.assert_matrix(np.linalg.matrix_power(sys.A+2*np.eye(2), 2), np.zeros((2, 2)))
                poles = analyze(sys, 0.2, "exact")["poles"]
                self.assertAlmostEqual(poles[0]["real"], math.exp(-0.4), delta=1e-12)
                self.assertAlmostEqual(poles[0]["real"], poles[1]["real"])
            else:
                self.assertTrue(all(abs(v.imag) < 1e-12 for v in eigen))
            mech = mechanical(b=R)
            self.assert_matrix(sys.A, mech.A)
            self.assert_matrix(sys.B, mech.B)
            self.assert_matrix(sys.C, mech.C)
            for method in ("exact", "forward", "backward", "trapezoid", "finite"):
                a = transition(sys, 0.2, method, 4)
                b = transition(mech, 0.2, method, 4)
                for x, y in zip(a, b):
                    self.assert_matrix(x, y)

    def test_rlc_closed_step_and_frequency_response(self):
        times = np.array([0., 0.2, 1., 2.])
        for R in (1., 4., 6.):
            system = rlc(R)
            analytical = analytical_output(system, times, np.zeros(2), "step")
            M, G = transition(system, 0.2, "exact")
            x = np.zeros(2)
            for step in range(11):
                if step in (0, 1, 5, 10):
                    index = {0: 0, 1: 1, 5: 2, 10: 3}[step]
                    self.assertAlmostEqual((system.C@x).item(), analytical[index], delta=2e-13)
                if step < 10:
                    x = M@x+G[:, 0]
            self.assertAlmostEqual(analytical[0], 0)
            self.assertGreater(analytical[-1], 0)
        system = rlc(1.)
        # Continuous transfer is 4/(s²+s+4); at omega=2, H=-2i.
        self.assertAlmostEqual(continuous_frequency_response(system, 2).real, 0., delta=1e-13)
        self.assertAlmostEqual(continuous_frequency_response(system, 2).imag, -2., delta=1e-13)
        sampled = frequency_response(system, 0.01, "exact", 2)
        self.assertLess(abs(abs(sampled)-2), 0.02)

    def test_exact_lc_and_singular_matrix(self):
        system = lc(2.)
        h = 0.3
        M, G = exact(system, h)
        c, s = math.cos(0.6), math.sin(0.6)
        self.assert_matrix(M, [[c, s/2], [-2*s, c]])
        self.assert_matrix(G, [[1-c], [2*s]])
        singular = System("integrator", np.array([[0.]]), np.array([[2.]]), np.array([[1.]]))
        M, G = exact(singular, h)
        self.assert_matrix(M, [[1.]])
        self.assert_matrix(G, [[0.6]])

    def test_transfer_zero_and_cancellation(self):
        sys = rlc(1.)
        M, G = transition(sys, 0.2, "trapezoid")
        tf = transfer(sys, M, G)
        self.assertAlmostEqual(tf["transfer_zeros"][0].real, -1., delta=1e-12)
        self.assertEqual(len(tf["transfer_poles"]), 2)
        self.assertEqual(len(transfer(sys, *transition(sys, 0.2, "forward"))["transfer_zeros"]), 0)
        hidden = System("hidden", np.diag([-1., -2.]), np.array([[1.], [0.]]), np.array([[1., 0.]]))
        M, G = transition(hidden, 0.2, "exact")
        tf = transfer(hidden, M, G)
        self.assertEqual(len(tf["internal_poles"]), 2)
        self.assertEqual(len(tf["cancellations"]), 1)
        self.assertEqual(len(tf["transfer_poles"]), 1)
        self.assertAlmostEqual(tf["transfer_poles"][0].real, math.exp(-0.2))
        weak = System("weak-mode", hidden.A, np.array([[1.], [1e-5]]),
                      np.array([[1., 1.]]))
        weak_tf = transfer(weak, *transition(weak, 0.2, "exact"))
        self.assertEqual(len(weak_tf["cancellations"]), 0)
        self.assertEqual(len(weak_tf["transfer_poles"]), 2)

    def test_similarity_and_sequential_order_dependence(self):
        system = rlc()
        P = np.array([[1., 1.], [0., 2.]])
        Pinv = np.linalg.inv(P)
        other = System("transformed", Pinv@system.A@P, Pinv@system.B, system.C@P)
        for method in ("exact", "forward", "backward", "trapezoid", "finite"):
            M, G = transition(system, 0.2, method, 4)
            Mt, Gt = transition(other, 0.2, method, 4)
            self.assert_matrix(Mt, Pinv@M@P)
            self.assert_matrix(Gt, Pinv@G)
            self.assert_matrix(other.C@Gt, system.C@G)
        M, _ = transition(system, 0.2, "sequential", 1)
        Mt, _ = transition(other, 0.2, "sequential", 1)
        self.assertGreater(np.linalg.norm(Mt-Pinv@M@P), 1e-4)
        self.assertLess(max(abs(np.linalg.eigvals(sequential_iteration_matrix(system.A, 0.2)))), 1)

    def test_pure_graph_regrouping_preserves_simultaneous_update(self):
        first = Graph(("q", "i"))
        q4 = first.add("q4", "gain", 0, value=-4)
        minus_i = first.add("minus_i", "gain", 1, value=-1)
        source = first.add("source", "constant", value=1)
        partial = first.add("partial", "sum", source, minus_i)
        first.bind(1, first.add("acceleration", "sum", partial, q4))
        second = Graph(("q", "i"))
        source = second.add("source", "constant", value=1)
        q4 = second.add("q4", "gain", 0, value=-4)
        minus_i = second.add("minus_i", "gain", 1, value=-1)
        partial = second.add("partial", "sum", q4, minus_i)
        second.bind(1, second.add("acceleration", "sum", source, partial))
        for k in (0, 1, 2, 4, 8):
            a = graph_transition(first, [0., 0.], 0.2, "backward", k)[0]
            b = graph_transition(second, [0., 0.], 0.2, "backward", k)[0]
            self.assert_matrix(a, b)
            M, G = transition(rlc(), 0.2, "finite", k)
            self.assert_matrix(a, (M@np.zeros(2)+G[:, 0]))

    def test_evaluation_counts_and_publication_convention(self):
        result = simulate(rc(), 0.1, "finite", 4, np.array([1.]), 1.)
        self.assertEqual(result["accepted_steps"], 10)
        self.assertEqual(result["base_derivative_evaluations"], 10)
        self.assertEqual(result["trial_derivative_evaluations"], 40)
        self.assertEqual(result["diagnostic_derivative_evaluations"], 10)
        self.assertEqual(result["total_function_evaluations"], 60)
        # First state is x_0; after ten updates x_10 is reported.
        self.assertAlmostEqual(result["final_state"][0], sum((-0.1)**j for j in range(6))**10,
                               delta=1e-12)


if __name__ == "__main__":
    unittest.main()
