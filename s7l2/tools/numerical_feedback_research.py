#!/usr/bin/env python3
"""Separate F64 research graph; does not modify the S7 runtime.

Run: python3 tools/numerical_feedback_research.py --suite gate --output /tmp/gate.json
     python3 tools/numerical_feedback_research.py --suite full --output /tmp/full.json
See docs/NUMERICAL_FEEDBACK_RESEARCH_N0.md for the frozen contract.
"""

from __future__ import annotations

import argparse
import json
import math
import platform
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np
from scipy.integrate import solve_ivp


@dataclass(frozen=True)
class Op:
    name: str
    kind: str
    args: tuple[int, ...]
    value: float = 0.0


class Graph:
    """Scalar, pure topological function graph with explicit memory outputs."""

    def __init__(self, states: tuple[str, ...]):
        self.states = states
        self.ops: list[Op] = []
        self.derivatives: tuple[int, ...] = ()
        self.region: tuple[int, ...] = ()
        self.base: list[float] = []
        self.pure_evaluations = 0
        self.derivative_evaluations = 0
        self.diagnostic_pure_evaluations = 0
        self.diagnostic_derivative_evaluations = 0

    def state(self, name: str) -> int:
        return self.states.index(name)

    def add(self, name: str, kind: str, *args: int, value: float = 0.0) -> int:
        if any(arg < 0 or arg >= len(self.states) + len(self.ops) for arg in args):
            raise ValueError(f"{name}: direct cycle, future node or unbound input")
        if kind not in {"constant", "gain", "sum", "product", "sin", "saturate"}:
            raise ValueError(f"{name}: non-pure or unsupported element {kind}")
        self.ops.append(Op(name, kind, tuple(args), value))
        return len(self.states) + len(self.ops) - 1

    def bind(self, *derivatives: int) -> Graph:
        if len(derivatives) != len(self.states):
            raise ValueError("one derivative input per integrator required")
        if any(d < 0 or d >= len(self.states) + len(self.ops) for d in derivatives):
            raise ValueError("unbound derivative input")
        self.derivatives = tuple(derivatives)
        # Intersection: downstream from trial memory and upstream of an RHS.
        downstream = set(range(len(self.states)))
        for i, op in enumerate(self.ops, len(self.states)):
            if any(arg in downstream for arg in op.args):
                downstream.add(i)
        upstream = set(derivatives)
        for i in range(len(self.ops) - 1, -1, -1):
            ref = len(self.states) + i
            if ref in upstream:
                upstream.update(self.ops[i].args)
        self.region = tuple(i for i in range(len(self.ops))
                            if len(self.states) + i in downstream & upstream)
        self.base = [0.0] * (len(self.states) + len(self.ops))
        return self

    @staticmethod
    def _calculate(op: Op, values: list[float]) -> float:
        a = op.args
        if op.kind == "constant":
            return op.value
        if op.kind == "gain":
            return op.value * values[a[0]]
        if op.kind == "sum":
            return sum(values[j] for j in a)
        if op.kind == "product":
            return values[a[0]] * values[a[1]]
        if op.kind == "sin":
            return math.sin(values[a[0]])
        return min(op.value, max(-op.value, values[a[0]]))

    def evaluate(self, states: list[float], trial: bool = False,
                 diagnostic: bool = False) -> list[float]:
        values = self.base.copy() if trial else self.base
        values[:len(states)] = states
        indices = self.region if trial else range(len(self.ops))
        for i in indices:
            values[len(states) + i] = self._calculate(self.ops[i], values)
        if diagnostic:
            self.diagnostic_pure_evaluations += len(indices)
            self.diagnostic_derivative_evaluations += 1
        else:
            self.pure_evaluations += len(indices)
            self.derivative_evaluations += 1
        result = [values[i] for i in self.derivatives]
        if not all(map(math.isfinite, result)):
            raise ArithmeticError("non-finite derivative")
        return result


def model(name: str, mu: float = 1.0, saturated: bool = False) -> tuple[Graph, list[float], Callable]:
    if name == "oscillator":
        g = Graph(("x", "v"))
        x, v = 0, 1
        minus_x = g.add("minus_x", "gain", x, value=-1)
        return g.bind(v, minus_x), [1.0, 0.0], lambda t, z: [z[1], -z[0]]
    if name == "linear":
        g = Graph(("x",))
        one = g.add("one", "constant", value=1)
        minus_2x = g.add("minus_2x", "gain", 0, value=-2)
        rhs = g.add("rhs", "sum", one, minus_2x)
        return g.bind(rhs), [0.0], lambda t, z: [1 - 2*z[0]]
    if name == "pendulum":
        g = Graph(("theta", "omega"))
        sine = g.add("sin_theta", "sin", 0)
        accel = g.add("gravity", "gain", sine, value=-9.81)
        return g.bind(1, accel), [1.5, 0.0], lambda t, z: [z[1], -9.81*math.sin(z[0])]
    if name == "vdp":
        g = Graph(("x", "v"))
        square = g.add("x_squared", "product", 0, 0)
        neg_square = g.add("neg_square", "gain", square, value=-1)
        one = g.add("one", "constant", value=1)
        factor = g.add("factor", "sum", one, neg_square)
        damping = g.add("nonlinear_damping", "product", factor, 1)
        damping_gain = g.add("mu", "gain", damping, value=mu)
        neg_x = g.add("neg_x", "gain", 0, value=-1)
        accel = g.add("accel", "sum", damping_gain, neg_x)
        return g.bind(1, accel), [0.1, 0.0], lambda t, z: [z[1], mu*(1-z[0]**2)*z[1]-z[0]]
    if name == "lorenz":
        g = Graph(("x", "y", "z"))
        neg_x = g.add("neg_x", "gain", 0, value=-1)
        y_minus_x = g.add("y_minus_x", "sum", 1, neg_x)
        dx = g.add("sigma", "gain", y_minus_x, value=10)
        neg_z = g.add("neg_z", "gain", 2, value=-1)
        rho = g.add("rho", "constant", value=28)
        rho_minus_z = g.add("rho_minus_z", "sum", rho, neg_z)
        x_times = g.add("x_times", "product", 0, rho_minus_z)
        neg_y = g.add("neg_y", "gain", 1, value=-1)
        dy = g.add("dy", "sum", x_times, neg_y)
        xy = g.add("xy", "product", 0, 1)
        neg_beta_z = g.add("neg_beta_z", "gain", 2, value=-8/3)
        dz = g.add("dz", "sum", xy, neg_beta_z)
        return g.bind(dx, dy, dz), [1, 1, 1], lambda t, s: [10*(s[1]-s[0]), s[0]*(28-s[2])-s[1], s[0]*s[1]-(8/3)*s[2]]
    if name in {"p", "pi", "saturated_pi"}:
        states = ("y",) if name == "p" else ("y", "q")
        g = Graph(states)
        one = g.add("reference", "constant", value=1)
        neg_y = g.add("neg_y", "gain", 0, value=-1)
        error = g.add("error", "sum", one, neg_y)
        p = g.add("p", "gain", error, value=2)
        u = p if name == "p" else g.add("u", "sum", p, 1)
        if saturated or name == "saturated_pi":
            u = g.add("saturation", "saturate", u, value=1)
        dy = g.add("dy", "sum", neg_y, u)
        rhs = (dy,) if name == "p" else (dy, error)
        def reference(t, z):
            e = 1-z[0]
            v = 2*e + (0 if name == "p" else z[1])
            if name == "saturated_pi":
                v = min(1, max(-1, v))
            return [v-z[0]] if name == "p" else [v-z[0], e]
        return g.bind(*rhs), [0.0]*len(states), reference
    raise ValueError(name)


class StepFailure(Exception):
    pass


def residual(g: Graph, base: list[float], z: list[float], h: float) -> float:
    f = g.evaluate(z, trial=True, diagnostic=True)
    return max(abs(z[i]-base[i]-h*f[i]) /
               (1e-12 + 1e-12*max(abs(z[i]), abs(base[i]))) for i in range(len(z)))


def transition(g: Graph, z: list[float], h: float, method: str, trials: int) -> tuple[list[float], float, int]:
    f0 = g.evaluate(z)
    candidate = [z[i] + h*f0[i] for i in range(len(z))]
    if method == "symplectic":
        # Applicable only to x'=v, v'=a(x); acceleration uses published x.
        candidate = [z[0]+h*candidate[1], candidate[1]]
        return candidate, math.nan, 0
    if method == "euler":
        return candidate, math.nan, 0
    if method not in {"backward", "trapezoid"}:
        raise ValueError(method)
    limit = 64 if trials == -1 else trials
    used = 0
    for _ in range(limit):
        ft = g.evaluate(candidate, trial=True)
        if method == "backward":
            candidate = [z[i]+h*ft[i] for i in range(len(z))]
        else:
            candidate = [z[i]+h*(f0[i]+ft[i])/2 for i in range(len(z))]
        used += 1
        if not all(map(math.isfinite, candidate)):
            raise StepFailure("non-finite candidate")
        if trials == -1:
            r = residual(g, z, candidate, h) if method == "backward" else trapezoid_residual(g,z,candidate,h,f0)
            if r <= 1:
                return candidate, r, used
    r = residual(g, z, candidate, h) if method == "backward" else trapezoid_residual(g,z,candidate,h,f0)
    if trials == -1:
        raise StepFailure(f"nonconvergence: residual={r:.17g}, iterations={used}")
    return candidate, r, used


def trapezoid_residual(g: Graph, z: list[float], candidate: list[float], h: float,
                       f0: list[float]) -> float:
    ft = g.evaluate(candidate, trial=True, diagnostic=True)
    return max(abs(candidate[i]-z[i]-h*(f0[i]+ft[i])/2) /
               (1e-12 + 1e-12*max(abs(candidate[i]), abs(z[i]))) for i in range(len(z)))


def crossing_period(times: np.ndarray, values: np.ndarray, after: float) -> float | None:
    crossings = []
    for i in range(1, len(times)):
        if values[i-1] < 0 <= values[i]:
            fraction = -values[i-1] / (values[i]-values[i-1])
            crossing = float(times[i-1] + fraction*(times[i]-times[i-1]))
            if crossing >= after:
                crossings.append(crossing)
    return float(np.mean(np.diff(crossings))) if len(crossings) >= 2 else None


_REFERENCE_CACHE: dict[tuple, tuple[np.ndarray, float]] = {}


def independent_reference(rhs: Callable, initial: list[float], duration: float,
                          times: np.ndarray, name: str) -> tuple[np.ndarray, float]:
    if name == "oscillator":
        return np.stack((np.cos(times), -np.sin(times)), axis=1), 0.0
    if name == "linear":
        return ((1-np.exp(-2*times))/2)[:, None], 0.0
    if name == "p":
        return ((2/3)*(1-np.exp(-3*times)))[:, None], 0.0
    kwargs = dict(method="DOP853", rtol=2e-13, atol=2e-14, dense_output=True)
    first = solve_ivp(rhs, (0, duration), initial, **kwargs)
    if not first.success:
        raise RuntimeError(first.message)
    reference = first.sol(times).T
    # Independent accuracy check for smooth, nonchaotic trajectories. For
    # Lorenz, solver refinements diverge over 40 s; compare only t<=2 s.
    second = solve_ivp(rhs, (0, duration), initial, max_step=0.025, **kwargs)
    if not second.success:
        raise RuntimeError(second.message)
    if name == "lorenz":
        mask = times <= 2 + 1e-12
    else:
        mask = slice(None)
    discrepancy = float(np.max(np.linalg.norm(reference[mask]-second.sol(times[mask]).T, axis=1)))
    return reference, discrepancy


def run_case(name: str, h: float, duration: float, method: str, trials: int,
             mu: float = 1.0, initial: list[float] | None = None) -> dict:
    g, start, rhs = model(name, mu)
    if initial is not None:
        start = initial
    steps = round(duration/h)
    if not math.isclose(steps*h, duration, abs_tol=1e-10):
        raise ValueError("duration is not a whole number of steps")
    stride = max(1, steps//1000)
    sample_indices = np.array(sorted(set(range(0, steps+1, stride)) | {steps}), dtype=int)
    times = sample_indices*h
    reference_key = (name, tuple(start), mu, h, duration)
    if reference_key not in _REFERENCE_CACHE:
        _REFERENCE_CACHE[reference_key] = independent_reference(rhs, start, duration, times, name)
    reference, reference_discrepancy = _REFERENCE_CACHE[reference_key]
    z = start.copy()
    samples = [z.copy()]
    upward_crossings: list[float] = []
    previous_first = z[0]
    next_sample = 1
    largest_residual = 0.0
    total_trials = 0
    failure = None
    started = time.perf_counter_ns()
    for n in range(1, steps+1):
        try:
            staged, r, used = transition(g, z, h, method, trials)
        except (StepFailure, ArithmeticError, OverflowError, ValueError) as exc:
            failure = dict(step=n, message=str(exc), published=z.copy())
            break
        # One commit; staged endpoint is next step's published value.
        z = staged
        if name in {"oscillator", "pendulum", "vdp"} and previous_first < 0 <= z[0]:
            crossing = ((n-1) + (-previous_first)/(z[0]-previous_first))*h
            if crossing >= duration/2:
                upward_crossings.append(crossing)
        previous_first = z[0]
        total_trials += used
        if math.isfinite(r):
            largest_residual = max(largest_residual, r)
        if next_sample < len(sample_indices) and n == sample_indices[next_sample]:
            samples.append(z.copy())
            next_sample += 1
    elapsed = time.perf_counter_ns() - started
    result = dict(model=name, mu=mu if name == "vdp" else None, initial=start,
                  method=method, refinements=trials, h=h, duration=duration,
                  accepted_steps=n if failure is None else n-1,
                  failure=failure, derivative_evaluations=g.derivative_evaluations,
                  pure_element_evaluations=g.pure_evaluations,
                  diagnostic_derivative_evaluations=g.diagnostic_derivative_evaluations,
                  diagnostic_pure_element_evaluations=g.diagnostic_pure_evaluations,
                  trial_region=[g.ops[i].name for i in g.region],
                  mean_trial_refinements=total_trials/max(1,n if failure is None else n-1),
                  max_normalized_residual=(largest_residual if method in {"backward", "trapezoid"} else None),
                  ns_per_accepted_step=elapsed/max(1,n if failure is None else n-1),
                  reference_discrepancy=reference_discrepancy)
    if failure:
        return result
    actual = np.asarray(samples)
    errors = np.linalg.norm(actual-reference, axis=1)
    result.update(final_error=float(errors[-1]), max_error=float(np.max(errors)),
                  rms_error=float(np.sqrt(np.mean(errors**2))), final_state=z)
    if name in {"oscillator", "pendulum", "vdp"}:
        result["last_half_period"] = (float(np.mean(np.diff(upward_crossings)))
                                      if len(upward_crossings) >= 2 else None)
        result["reference_last_half_period"] = crossing_period(times, reference[:,0], duration/2)
    if name == "oscillator":
        amplitude = float(math.hypot(*z))
        result.update(final_amplitude=amplitude, final_energy_error=(amplitude**2-1)/2,
                      final_phase_error=float(math.remainder(math.atan2(-z[1], z[0])-duration, 2*math.pi)))
    if name == "pendulum":
        energy = 0.5*actual[:,1]**2 + 9.81*(1-np.cos(actual[:,0]))
        result["max_energy_drift"] = float(np.max(np.abs(energy-energy[0])))
    if name == "lorenz":
        early = times <= 2+1e-12
        result["short_horizon_max_error"] = float(np.max(errors[early]))
        result["last_half_mean"] = actual[times >= duration/2].mean(axis=0).tolist()
        result["last_half_std"] = actual[times >= duration/2].std(axis=0).tolist()
        result["max_state_norm"] = float(np.max(np.linalg.norm(actual,axis=1)))
    if name == "vdp":
        tail = actual[times >= duration*0.75, 0]
        result["last_quarter_x_range"] = [float(np.min(tail)), float(np.max(tail))]
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("gate", "full", "period"), default="gate")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    cases = [("oscillator", 0.001, 200, 1.0, None),
             ("oscillator", 0.0005, 200, 1.0, None),
             ("linear", 0.001, 12, 1.0, None),
             ("linear", 0.0005, 12, 1.0, None)]
    if args.suite == "full":
        cases += [("pendulum", h, 100, 1.0, None) for h in (0.001,0.0001)]
        cases += [("vdp", h, d, mu, start) for mu in (1,3)
                  for start in ([0.1,0],[2,0],[4,0])
                  for d in (40,60) for h in (0.001,0.0005)]
        cases += [("lorenz", h, 40, 1.0, start) for start in ([1,1,1],[1.000001,1,1])
                  for h in (0.00025,0.000125)]
        cases += [(name, h, 12, 1.0, None) for name in ("p","pi","saturated_pi")
                  for h in (0.001,0.0005)]
    if args.suite == "period":
        cases = [("pendulum", 0.001, 100, 1.0, None),
                 ("vdp", 0.001, 40, 1.0, [0.1,0]),
                 ("vdp", 0.001, 40, 3.0, [0.1,0])]
    methods = [("euler",0),("backward",1),("backward",2),("backward",4),
               ("backward",8),("backward",-1),("trapezoid",-1)]
    if args.suite == "period":
        methods = [("euler",0),("backward",-1),("trapezoid",-1)]
    results = []
    for name,h,duration,mu,start in cases:
        for method,trials in methods + ([("symplectic",0)] if name in {"oscillator","pendulum"} else []):
            result = run_case(name,h,duration,method,trials,mu,start)
            results.append(result)
            print(name,h,method,trials,"error",result.get("max_error"),"failure",result["failure"],flush=True)
    metadata = dict(branch_commit=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip(),
                    python=platform.python_version(), numpy=np.__version__,
                    scipy=__import__("scipy").__version__, host=platform.node(),
                    suite=args.suite, results=results)
    if args.output:
        args.output.write_text(json.dumps(metadata,indent=2)+"\n")
    else:
        print(json.dumps(metadata,indent=2))


if __name__ == "__main__":
    main()
