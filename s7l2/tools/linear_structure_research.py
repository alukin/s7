#!/usr/bin/env python3
"""Isolated F64 linear-system laboratory. No production S7 execution changes.

Run from s7l2: python3 tools/linear_structure_research.py --output build/linear-research
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import platform
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import scipy
from scipy.linalg import expm


@dataclass(frozen=True)
class System:
    name: str
    A: np.ndarray
    B: np.ndarray
    C: np.ndarray
    D: float = 0.0


def rc(tau: float = 1.0) -> System:
    return System("RC", np.array([[-1/tau]]), np.array([[1/tau]]), np.array([[1.0]]))


def lc(omega: float = 1.0) -> System:
    return System("LC", np.array([[0., 1.], [-omega**2, 0.]]),
                  np.array([[0.], [omega**2]]), np.array([[1., 0.]]))


def rlc(R: float = 1.0, L: float = 1.0, C: float = 0.25) -> System:
    return System(f"RLC R={R:g}", np.array([[0., 1.], [-1/(L*C), -R/L]]),
                  np.array([[0.], [1/L]]), np.array([[1/C, 0.]]))


def mechanical(m: float = 1.0, b: float = 1.0, stiffness: float = 4.0) -> System:
    return System("mass-spring-damper", np.array([[0., 1.], [-stiffness/m, -b/m]]),
                  np.array([[0.], [1/m]]), np.array([[stiffness, 0.]]))


def exact(system: System, h: float) -> tuple[np.ndarray, np.ndarray]:
    """Augmented exponential works even for singular A and critical damping."""
    n = system.A.shape[0]
    augmented = np.zeros((n+1, n+1))
    augmented[:n, :n] = system.A
    augmented[:n, n:] = system.B
    sampled = expm(h*augmented)
    return sampled[:n, :n], sampled[:n, n:]


def transition(system: System, h: float, method: str, corrections: int = 0
               ) -> tuple[np.ndarray, np.ndarray]:
    A, B = system.A, system.B
    Q = h*A
    I = np.eye(len(A))
    if method == "exact":
        return exact(system, h)
    if method == "forward":
        return I+Q, h*B
    if method == "backward":
        return np.linalg.solve(I-Q, I), np.linalg.solve(I-Q, h*B)
    if method == "trapezoid":
        return np.linalg.solve(I-Q/2, I+Q/2), np.linalg.solve(I-Q/2, h*B)
    if method == "finite":
        if corrections < 0:
            raise ValueError("negative correction count")
        M, G = I.copy(), np.zeros_like(B)
        power = I.copy()
        for j in range(corrections+1):
            if j <= corrections:
                G += h*power@B
            power = power@Q
            M += power
        return M, G
    if method == "sequential":
        # Gauss-Seidel sweep from the forward-Euler predictor. Input held.
        lower = np.tril(A)
        upper = A-lower
        inv_lower = np.linalg.solve(I-h*lower, I)
        T = inv_lower@(h*upper)
        # p_j = inv_lower (x + h upper p_(j-1) + h B u).
        M, G = I+Q, h*B
        for _ in range(corrections):
            M = inv_lower + T@M
            G = inv_lower@(h*B) + T@G
        return M, G
    if method == "symplectic":
        if len(A) != 2 or not np.isclose(A[0, 0], 0) or not np.isclose(A[0, 1], 1) or not np.isclose(A[1, 1], 0):
            raise ValueError("symplectic formula requires x'=v, v'=-omega² x + B2 u")
        w2 = -A[1, 0]
        if w2 <= 0 or B[0, 0] != 0:
            raise ValueError("unsupported partition")
        return np.array([[1-h*h*w2, h], [-h*w2, 1.]]), np.array([[h*h*B[1, 0]], [h*B[1, 0]]])
    raise ValueError(method)


def sequential_iteration_matrix(A: np.ndarray, h: float) -> np.ndarray:
    lower = np.tril(A)
    return np.linalg.solve(np.eye(len(A))-h*lower, h*(A-lower))


def simultaneous_residual(system: System, x: np.ndarray, u: float, h: float,
                          corrections: int) -> tuple[float, float, int]:
    M, G = transition(system, h, "finite", corrections)
    candidate = M@x+G[:, 0]*u
    root_M, root_G = transition(system, h, "backward")
    root = root_M@x+root_G[:, 0]*u
    residual = candidate-x-h*(system.A@candidate+system.B[:, 0]*u)
    return float(np.linalg.norm(residual)), float(np.linalg.norm(candidate-root)), corrections+2


def _poles(matrix: np.ndarray) -> list[complex]:
    """Recognize a conditioned double root before forming unstable sqrt roots."""
    if len(matrix) == 1:
        return [complex(matrix[0, 0])]
    trace = float(np.trace(matrix))
    determinant = float(np.linalg.det(matrix))
    discriminant = trace*trace-4*determinant
    scale = max(1., np.linalg.norm(matrix, ord=2)**2)
    if abs(discriminant) <= 64*np.finfo(float).eps*scale:
        return [complex(trace/2)]*2
    root = complex(discriminant)**0.5
    return [(trace+root)/2, (trace-root)/2]


def transfer(system: System, M: np.ndarray, G: np.ndarray) -> dict:
    """Return raw and reduced SISO transfer roots, preserving internal poles."""
    n = len(M)
    internal = _poles(M)
    if n == 1:
        # H = (D z + C G - D M)/(z-M).
        numerator = np.array([system.D, (system.C@G).item()-system.D*M[0, 0]])
    elif n == 2:
        tr = float(np.trace(M))
        det = float(np.linalg.det(M))
        numerator = np.array([system.D,
                              (system.C@G).item()-system.D*tr,
                              (system.C@(M-tr*np.eye(2))@G).item()+system.D*det])
    else:
        raise ValueError("laboratory supports one or two states")
    scale = max(1., float(np.max(np.abs(numerator))))
    first = next((i for i, v in enumerate(numerator) if abs(v) > 32*np.finfo(float).eps*scale), len(numerator))
    zeros = [] if first == len(numerator) or len(numerator[first:]) == 1 else list(np.roots(numerator[first:]))
    remaining_poles = internal.copy()
    remaining_zeros = [complex(z) for z in zeros]
    cancellations = []
    separation = abs(internal[0]-internal[1]) if n == 2 else 1.
    root_scale = max(1., np.linalg.norm(M, ord=2))
    # Simple-root sensitivity grows inversely with pole separation. At a
    # repeated root, use the sqrt(eps) scale appropriate to quadratic roots.
    condition_scale = root_scale/max(separation, math.sqrt(np.finfo(float).eps)*root_scale)
    cancellation_tolerance = max(256*np.finfo(float).eps*condition_scale,
                                 64*np.finfo(float).eps*root_scale)
    for zero in zeros:
        if not remaining_poles:
            break
        j = min(range(len(remaining_poles)), key=lambda i: abs(zero-remaining_poles[i]))
        pole = remaining_poles[j]
        if abs(zero-pole) <= cancellation_tolerance*max(1., abs(zero), abs(pole)):
            cancellations.append([complex(pole), complex(zero)])
            remaining_poles.pop(j)
            remaining_zeros.remove(complex(zero))
    return dict(numerator=numerator.tolist(), internal_poles=internal,
                raw_zeros=zeros, transfer_poles=remaining_poles,
                transfer_zeros=remaining_zeros, cancellations=cancellations,
                cancellation_tolerance=cancellation_tolerance)


def pole_metrics(pole: complex, h: float, reference: complex | None = None) -> dict:
    radius = abs(pole)
    eps = 1e-10
    return dict(real=pole.real, imag=pole.imag, magnitude=radius,
                argument=math.atan2(pole.imag, pole.real),
                stability="marginal" if abs(radius-1) <= eps else "stable" if radius < 1 else "unstable",
                damping=-math.log(radius)/h if radius else math.inf,
                frequency=abs(math.atan2(pole.imag, pole.real))/h,
                exact_pole_distance=abs(pole-reference) if reference is not None else None)


def frequency_response(system: System, h: float, method: str, omega: float,
                       corrections: int = 0) -> complex:
    M, G = transition(system, h, method, corrections)
    z = np.exp(1j*omega*h)
    return complex((system.C@np.linalg.solve(z*np.eye(len(M))-M, G)).item()+system.D)


def continuous_frequency_response(system: System, omega: float) -> complex:
    return complex((system.C@np.linalg.solve(1j*omega*np.eye(len(system.A))-system.A,
                                              system.B)).item()+system.D)


def trajectory(system: System, h: float, duration: float, method: str,
               corrections: int, initial: np.ndarray, drive: str
               ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    M, G = transition(system, h, method, corrections)
    times = np.arange(round(duration/h)+1)*h
    states = np.empty((len(times), len(initial)))
    output = np.empty(len(times))
    x = np.array(initial, dtype=float)
    for j, t in enumerate(times):
        u = 0. if drive == "zero" else 1. if drive == "step" else math.sin(2*t)
        states[j] = x
        output[j] = (system.C@x).item()+system.D*u
        if j < len(times)-1:
            x = M@x+G[:, 0]*u
    return times, states, output


def analytical_output(system: System, times: np.ndarray, initial: np.ndarray,
                      drive: str) -> np.ndarray:
    """Closed-form references for the experiment's zero and step inputs."""
    if system.name == "RC":
        tau = -1/system.A[0, 0]
        target = 1. if drive == "step" else 0.
        return target+(initial[0]-target)*np.exp(-times/tau)
    if system.name == "LC" and drive == "zero":
        omega = math.sqrt(-system.A[1, 0])
        return initial[0]*np.cos(omega*times)+initial[1]/omega*np.sin(omega*times)
    if (system.name.startswith("RLC") or system.name == "mass-spring-damper") and drive == "step" and np.all(initial == 0):
        alpha = -system.A[1, 1]/2
        omega2 = -system.A[1, 0]
        equilibrium = system.C[0, 0]/omega2*system.B[1, 0]
        delta = alpha*alpha-omega2
        if abs(delta) < 1e-12:
            normalized = 1-(1+alpha*times)*np.exp(-alpha*times)
        elif delta < 0:
            wd = math.sqrt(-delta)
            normalized = 1-np.exp(-alpha*times)*(np.cos(wd*times)+alpha/wd*np.sin(wd*times))
        else:
            root = math.sqrt(delta)
            lam1, lam2 = -alpha+root, -alpha-root
            normalized = 1+(lam2*np.exp(lam1*times)-lam1*np.exp(lam2*times))/(lam1-lam2)
        return equilibrium*normalized
    raise ValueError("no closed-form output for this configuration")


def analyze(system: System, h: float, method: str, corrections: int = 0) -> dict:
    M, G = transition(system, h, method, corrections)
    poles = _poles(M)
    exact_poles = [complex(np.exp(s*h)) for s in _poles(system.A)]
    # Match conjugate pairs and repeated real roots by minimum total distance.
    if len(poles) == 2:
        if abs(poles[0]-exact_poles[1])+abs(poles[1]-exact_poles[0]) < abs(poles[0]-exact_poles[0])+abs(poles[1]-exact_poles[1]):
            exact_poles.reverse()
    tf = transfer(system, M, G)
    return dict(model=system.name, h=h, method=method, corrections=corrections,
                continuous_poles=[dict(real=s.real, imag=s.imag) for s in _poles(system.A)],
                M=M.tolist(), G=G.tolist(),
                poles=[pole_metrics(p, h, r) for p, r in zip(poles, exact_poles)],
                transfer={key: value for key, value in tf.items()})


def simulate(system: System, h: float, method: str, corrections: int,
             initial: np.ndarray, duration: float, drive: str = "zero") -> dict:
    M, G = transition(system, h, method, corrections)
    steps = round(duration/h)
    x = np.array(initial, dtype=float)
    ys = np.empty(steps+1)
    states = np.empty((steps+1, len(x)))
    started = time.perf_counter()
    cpu_started = time.process_time()
    for j in range(steps+1):
        u = 0. if drive == "zero" else 1. if drive == "step" else math.sin(2*j*h)
        states[j] = x
        ys[j] = (system.C@x).item()+system.D*u
        if j < steps:
            x = M@x+G[:, 0]*u
    elapsed = time.perf_counter()-started
    cpu_elapsed = time.process_time()-cpu_started
    tf = transfer(system, M, G)
    # These are analytical graph-equivalent counts, not calls made by the
    # precomputed matrix recurrence. Implicit direct solves have no comparable
    # scalar graph count; Phase 1's runner measures their iterative costs.
    graph_counted = method in {"forward", "finite", "sequential", "symplectic"}
    n_eval = (1+corrections+(method == "finite")) if graph_counted else None
    result = dict(model=system.name, method=method, corrections=corrections, h=h,
                  duration=duration, drive=drive, initial=initial.tolist(),
                  accepted_steps=steps, rejected_steps=0,
                  base_derivative_evaluations=steps if graph_counted else None,
                  trial_derivative_evaluations=steps*corrections if method in {"finite", "sequential"} else 0,
                  diagnostic_derivative_evaluations=steps if method == "finite" else 0,
                  total_function_evaluations=steps*n_eval if n_eval is not None else None,
                  pure_graph_element_evaluations=None,
                  matrix_vector_updates=steps,
                  wall_seconds=elapsed, cpu_seconds=cpu_elapsed,
                  final_output=float(ys[-1]), max_output=float(np.max(ys)),
                  min_output=float(np.min(ys)),
                  final_state=states[-1].tolist(),
                  transfer_pole_count=len(tf["transfer_poles"]))
    if drive != "sin":
        reference = analytical_output(system, np.arange(steps+1)*h, np.asarray(initial), drive)
        result["max_analytical_output_error"] = float(np.max(np.abs(ys-reference)))
        result["final_analytical_output_error"] = float(abs(ys[-1]-reference[-1]))
    if drive == "step":
        target = float((system.C@np.linalg.solve(-system.A, system.B)).item()+system.D)
        result["overshoot"] = float(np.max(ys)-target)
        result["steady_state_error_at_end"] = float(ys[-1]-target)
    if len(x) == 2:
        stiffness = -system.A[1, 0]
        energy = 0.5*(stiffness*states[:, 0]**2+states[:, 1]**2)
        result["energy_ratio"] = float(energy[-1]/energy[0]) if energy[0] else None
    return result


def _json_default(value):
    if isinstance(value, complex):
        return dict(real=value.real, imag=value.imag)
    if isinstance(value, np.generic):
        return value.item()
    raise TypeError(type(value))


def run(output: Path, plots: bool = True) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    systems = [rc(), lc(), rlc(1), rlc(4), rlc(6), mechanical()]
    methods = [("exact", 0), ("forward", 0), ("backward", 0), ("trapezoid", 0)]
    methods += [("finite", k) for k in (0, 1, 2, 4, 8, 16)]
    methods += [("sequential", k) for k in (1, 2, 4, 8)]
    matrix = []
    responses = []
    for system in systems:
        hs = [0.05, 0.2, 0.5, 1.0, 2.5]
        if system.name == "LC":
            hs += [3.0]
        for h in hs:
            for method, k in methods + ([("symplectic", 0)] if system.name == "LC" else []):
                matrix.append(analyze(system, h, method, k))
        for method, k in methods + ([("symplectic", 0)] if system.name == "LC" else []):
            if system.name in {"RC", "LC", "RLC R=1", "RLC R=4", "RLC R=6", "mass-spring-damper"}:
                initial = np.array([1.]) if system.name == "RC" else np.array([1., 0.]) if system.name == "LC" else np.zeros(2)
                drive = "zero" if system.name in {"RC", "LC"} else "step"
                responses.append(simulate(system, 0.2, method, k, initial, 20., drive))
                if system.name == "RC":
                    responses.append(simulate(system, 0.2, method, k, np.array([0.]), 20., "step"))
    metadata = dict(branch=subprocess.check_output(["git", "branch", "--show-current"], text=True).strip(),
                    git_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                    source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    generated_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                    precision="IEEE-754 binary64", python=sys.version.split()[0], numpy=np.__version__,
                    scipy=scipy.__version__, platform=platform.platform(),
                    tolerances=dict(stability_radius=1e-10,
                                    cancellation="256 eps scaled by inverse pole separation; sqrt(eps) floor",
                                    repeated_root_discriminant="64 eps max(1, ||M||²)"),
                    configuration=dict(models=[s.name for s in systems], h_values="0.05,0.2,0.5,1,2.5; LC also 3",
                                       methods=[f"{m}:{k}" for m, k in methods], response_h=0.2, response_duration=20),
                    reproduce="python3 tools/linear_structure_research.py --output build/linear-research",
                    notes="Matrix laboratory timings compare only runs of this Python implementation; exact matrix setup is outside timed loop.")
    result = dict(metadata=metadata, pole_matrix=matrix, responses=responses)
    frequency = []
    for system in (rlc(1), rlc(4), rlc(6)):
        for omega in (0.5, 1., 2., 3.):
            reference = continuous_frequency_response(system, omega)
            for method in ("exact", "forward", "backward", "trapezoid", "finite"):
                value = frequency_response(system, 0.2, method, omega, 2)
                frequency.append(dict(model=system.name, omega_rad_per_s=omega, method=method,
                                      magnitude=abs(value), phase_rad=np.angle(value),
                                      continuous_magnitude=abs(reference),
                                      continuous_phase_rad=np.angle(reference)))
    result["frequency_response"] = frequency
    iteration_diagnostics = []
    for system, initial, hs in ((rc(), np.array([1.]), (0.5, 1., 2.5)),
                                 (lc(), np.array([1., 0.]), (0.5, 1.5))):
        for h in hs:
            for k in (0, 1, 2, 4, 8, 16):
                residual_norm, solver_error, evaluations = simultaneous_residual(system, initial, 0., h, k)
                iteration_diagnostics.append(dict(model=system.name, h=h, corrections=k,
                                                  spectral_radius_iteration=float(max(abs(np.linalg.eigvals(h*system.A)))),
                                                  residual_norm=residual_norm,
                                                  implicit_solver_error=solver_error,
                                                  total_function_evaluations=evaluations))
    result["iteration_diagnostics"] = iteration_diagnostics
    equal_work = []
    for system, initial in ((rc(), np.array([1.])), (lc(), np.array([1., 0.]))):
        duration, h = 5., 0.1
        reference = trajectory(system, h, duration, "exact", 0, initial, "zero")[1][-1]
        for k in (0, 1, 2, 4, 8):
            work = round(duration/h)*(k+2)  # includes one residual evaluation per step
            finite = trajectory(system, h, duration, "finite", k, initial, "zero")[1][-1]
            euler_h = duration/work
            euler = trajectory(system, euler_h, duration, "forward", 0, initial, "zero")[1][-1]
            equal_work.append(dict(model=system.name, corrections=k, duration=duration,
                                   finite_h=h, euler_h=euler_h,
                                   total_function_evaluations=work,
                                   finite_final_state_error=float(np.linalg.norm(finite-reference)),
                                   euler_final_state_error=float(np.linalg.norm(euler-reference))))
    result["equal_work"] = equal_work
    (output/"summary.json").write_text(json.dumps(result, indent=2, default=_json_default)+"\n")
    if plots:
        make_plots(output, matrix)
    return result


def compact_summary(data: dict) -> dict:
    selected = []
    for row in data["pole_matrix"]:
        h = 0.5 if row["model"] in {"RC", "LC"} else 0.2
        if row["h"] != h or row["method"] == "sequential":
            continue
        if row["method"] == "finite" and row["corrections"] not in (0, 1, 2, 8):
            continue
        selected.append(dict(model=row["model"], h=h, method=row["method"],
                             corrections=row["corrections"], poles=row["poles"],
                             transfer_zeros=row["transfer"]["transfer_zeros"],
                             transfer_pole_count=len(row["transfer"]["transfer_poles"]),
                             cancellation_count=len(row["transfer"]["cancellations"])))
    responses = [dict(model=r["model"], method=r["method"], drive=r["drive"],
                      h=r["h"], max_analytical_output_error=r["max_analytical_output_error"],
                      overshoot=r.get("overshoot"), energy_ratio=r.get("energy_ratio"))
                 for r in data["responses"] if r["method"] in {"exact", "forward", "backward", "trapezoid", "symplectic"}]
    frequencies = [r for r in data["frequency_response"]
                   if r["model"] == "RLC R=1" and r["omega_rad_per_s"] == 2.]
    return dict(metadata=data["metadata"], selected_poles=selected, selected_responses=responses,
                frequency_response=frequencies,
                iteration_diagnostics=data["iteration_diagnostics"], equal_work=data["equal_work"])


def make_plots(output: Path, matrix: list[dict]) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle

    for model in ("RC", "LC", "RLC R=1", "RLC R=4", "RLC R=6"):
        records = [r for r in matrix if r["model"] == model]
        fig, ax = plt.subplots(figsize=(7, 7))
        ax.add_patch(Circle((0, 0), 1, fill=False, linestyle="--", color="grey"))
        for label, subset in [(m, [r for r in records if r["method"] == m and r["corrections"] == 0])
                              for m in ("exact", "forward", "backward", "trapezoid", "symplectic")]:
            if subset:
                ax.scatter([p["real"] for r in subset for p in r["poles"]],
                           [p["imag"] for r in subset for p in r["poles"]], label=label)
        ax.set(xlabel="Re(z)", ylabel="Im(z)", title=f"{model}: poles for h=0.05..2.5 s")
        ax.axis("equal"); ax.grid(True); ax.legend()
        fig.savefig(output/(model.replace(" ", "_").replace("=", "")+"_poles.png"), dpi=150)
        plt.close(fig)
    for model in ("RC", "LC", "RLC R=1"):
        fig, ax = plt.subplots(figsize=(7, 6))
        ax.add_patch(Circle((0, 0), 1, fill=False, linestyle="--", color="grey"))
        for method in ("exact", "forward", "backward", "trapezoid"):
            subset = sorted((r for r in matrix if r["model"] == model and r["method"] == method),
                            key=lambda r: r["h"])
            branch = [max(r["poles"], key=lambda p: p["imag"]) for r in subset]
            ax.plot([p["real"] for p in branch], [p["imag"] for p in branch], marker="o", label=method)
        ax.set(xlabel="Re(z)", ylabel="Im(z)", title=f"{model}: pole trajectory as h grows")
        ax.axis("equal"); ax.grid(True); ax.legend()
        fig.savefig(output/(model.replace(" ", "_").replace("=", "")+"_trajectory.png"), dpi=150)
        plt.close(fig)
    records = [r for r in matrix if r["model"] == "LC" and r["h"] == 0.5 and r["method"] == "finite"]
    fig, ax = plt.subplots()
    ax.add_patch(Circle((0, 0), 1, fill=False, linestyle="--", color="grey"))
    ax.plot([max(r["poles"], key=lambda p: p["imag"])["real"] for r in records],
            [max(r["poles"], key=lambda p: p["imag"])["imag"] for r in records],
            color="grey", linewidth=1)
    for r in records:
        ax.scatter([p["real"] for p in r["poles"]], [p["imag"] for p in r["poles"]], label=f"k={r['corrections']}")
    ax.set(xlabel="Re(z)", ylabel="Im(z)", title="LC finite corrections, h omega0=0.5")
    ax.axis("equal"); ax.grid(True); ax.legend()
    fig.savefig(output/"LC_iterations.png", dpi=150); plt.close(fig)

    # Phase, damping, and timestep trajectories use the exact sampled poles
    # as the reference; plotted branches are the positive-imaginary poles.
    for metric, ylabel, reference_value in (("frequency", "Frequency error (rad/s)", 1.),
                                             ("damping", "Damping error (1/s)", 0.)):
        fig, ax = plt.subplots()
        for method in ("exact", "forward", "backward", "trapezoid", "symplectic"):
            subset = sorted((r for r in matrix if r["model"] == "LC" and r["method"] == method
                             and r["h"] <= 1.), key=lambda r: r["h"])
            ax.plot([r["h"] for r in subset], [max(r["poles"], key=lambda p: p["imag"])[metric]-reference_value
                    for r in subset], marker="o", label=method)
        ax.set(xlabel="h omega0 (dimensionless; omega0=1 rad/s)", ylabel=ylabel,
               title=f"LC {metric} error versus step", xlim=(0, 1.05))
        ax.grid(True); ax.legend()
        fig.savefig(output/f"LC_{metric}_vs_h.png", dpi=150); plt.close(fig)

    system = lc()
    fig, (ax, zoom) = plt.subplots(2, 1, sharex=True, figsize=(8, 7))
    for method, k in (("exact", 0), ("forward", 0), ("backward", 0),
                      ("trapezoid", 0), ("finite", 1), ("symplectic", 0)):
        t, states, _ = trajectory(system, 0.2, 50., method, k, np.array([1., 0.]), "zero")
        energy = 0.5*(states[:, 0]**2+states[:, 1]**2)
        label = f"{method} k={k}" if method == "finite" else method
        ax.plot(t, energy, label=label)
        zoom.plot(t, energy, label=label)
    ax.set(ylabel="Energy (normalized, log scale)", yscale="log", title="LC long-run energy, h omega0=0.2")
    zoom.set(xlabel="Time (s)", ylabel="Energy near 0.5", ylim=(0.35, 0.65))
    ax.grid(True); zoom.grid(True); ax.legend(ncol=3, fontsize=8)
    fig.tight_layout(); fig.savefig(output/"LC_energy.png", dpi=150); plt.close(fig)

    for drive, duration in (("step", 15.), ("sin", 30.)):
        fig, ax = plt.subplots()
        system = rlc(1.)
        for method, k in (("exact", 0), ("forward", 0), ("backward", 0),
                          ("trapezoid", 0), ("finite", 2)):
            t, _, y = trajectory(system, 0.2, duration, method, k, np.zeros(2), drive)
            ax.plot(t, y, label=f"{method} k={k}" if method == "finite" else method)
        ax.set(xlabel="Time (s)", ylabel="Capacitor voltage (V)",
               title=f"RLC R=1 Ω, L=1 H, C=0.25 F: {'held sinusoid' if drive == 'sin' else 'step'} input, h=0.2 s")
        ax.grid(True); ax.legend(); fig.savefig(output/f"RLC_{drive}_response.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for R in (1., 4., 6.):
        t, _, y = trajectory(rlc(R), 0.2, 10., "exact", 0, np.zeros(2), "step")
        ax.plot(t, y, label=f"R={R:g} Ω")
    ax.set(xlabel="Time (s)", ylabel="Capacitor voltage (V)",
           title="Exact ZOH RLC step: underdamped, critical, overdamped")
    ax.grid(True); ax.legend(); fig.savefig(output/"RLC_damping_regimes.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for drive, initial in (("step", np.array([0.])), ("zero", np.array([1.]))):
        for method in ("exact", "forward", "backward", "trapezoid"):
            t, _, y = trajectory(rc(), 0.5, 5., method, 0, initial, drive)
            ax.plot(t, y, label=f"{drive} {method}")
    ax.set(xlabel="Time / RC (dimensionless)", ylabel="Capacitor voltage / input (dimensionless)",
           title="RC charge and discharge, h/RC=0.5")
    ax.grid(True); ax.legend(ncol=2, fontsize=8); fig.savefig(output/"RC_charge_discharge.png", dpi=150); plt.close(fig)

    fig, (amplitude, phase) = plt.subplots(2, 1, sharex=True, figsize=(8, 7))
    system = rlc(1.)
    frequencies = np.linspace(0.1, 5., 200)
    for method in ("continuous", "exact", "forward", "backward", "trapezoid"):
        values = [continuous_frequency_response(system, w) if method == "continuous"
                  else frequency_response(system, 0.2, method, w) for w in frequencies]
        amplitude.plot(frequencies, np.abs(values), label=method)
        phase.plot(frequencies, np.unwrap(np.angle(values)), label=method)
    amplitude.set(ylabel="|H| (V/V)", title="RLC frequency response, h=0.2 s; exact = sampled ZOH")
    phase.set(xlabel="Drive frequency (rad/s)", ylabel="Phase (rad)")
    amplitude.grid(True); phase.grid(True); amplitude.legend()
    fig.tight_layout(); fig.savefig(output/"RLC_frequency_response.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for system in (rc(), lc()):
        for h in (0.5, 1.5):
            ks = list(range(0, 17))
            residuals = [simultaneous_residual(system, np.ones(len(system.A)), 0., h, k)[0] for k in ks]
            ax.semilogy(ks, residuals, marker=".", label=f"{system.name}, h={h:g} s")
    ax.set(xlabel="Simultaneous corrections k", ylabel="Implicit residual norm",
           title="Fixed-point residual, held zero input")
    ax.grid(True); ax.legend(); fig.savefig(output/"solver_residual.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    # Count includes one diagnostic derivative evaluation per finite step.
    for system, initial, duration in ((rc(), np.array([1.]), 5.),
                                      (lc(), np.array([1., 0.]), 5.)):
        exact_state = trajectory(system, 0.01, duration, "exact", 0, initial, "zero")[1][-1]
        for k in (0, 1, 2, 4, 8):
            h = 0.1
            finite = trajectory(system, h, duration, "finite", k, initial, "zero")[1][-1]
            work = round(duration/h)*(k+2)
            euler_h = duration/work
            euler = trajectory(system, euler_h, duration, "forward", 0, initial, "zero")[1][-1]
            ax.scatter(work, np.linalg.norm(finite-exact_state), marker="o", label=f"{system.name} finite k={k}")
            ax.scatter(work, np.linalg.norm(euler-exact_state), marker="x", color="black")
    ax.set(xlabel="Total derivative evaluations (including diagnostics)",
           ylabel="Final state error vs exact sample", yscale="log",
           title="Equal work at 5 s; black x = Euler at same evaluation budget")
    ax.grid(True); ax.legend(fontsize=7); fig.savefig(output/"error_vs_work.png", dpi=150); plt.close(fig)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("build/linear-research"))
    parser.add_argument("--compact-output", type=Path)
    parser.add_argument("--no-plots", action="store_true")
    args = parser.parse_args()
    data = run(args.output, not args.no_plots)
    if args.compact_output:
        args.compact_output.parent.mkdir(parents=True, exist_ok=True)
        args.compact_output.write_text(json.dumps(compact_summary(data), indent=2, default=_json_default)+"\n")
    print(f"{len(data['pole_matrix'])} pole cases, {len(data['responses'])} responses in {args.output}")
