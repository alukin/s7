#!/usr/bin/env python3
"""Post-process Phase 3 C++ real-graph output; no integration is performed here.

Run from s7l2:
MPLCONFIGDIR=/tmp/s7-mpl python3 -B tools/plot_numerical_integration_phase3.py \
  --input build-release/s7tests/phase3 --compact docs/NUMERICAL_INTEGRATION_PHASE3_SUMMARY.json
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
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle


def by_case(rows: list[dict], model: str, h: float, method: str) -> dict:
    return next(r for r in rows if r["model"] == model and r["h"] == h and r["method"] == method)


def file_name(model: str, h: float, method: str) -> str:
    return f"{model}_h{round(h*1000)}_{method}.csv"


def trajectory(directory: Path, model: str, h: float, method: str) -> np.ndarray:
    return np.genfromtxt(directory/file_name(model, h, method), delimiter=",", names=True)


def system_matrix(model: str) -> np.ndarray:
    if model == "oscillator":
        return np.array([[0., 1.], [-1., 0.]])
    if model.startswith("rc_"):
        return np.array([[-1.]])
    resistance = float(model.split("r")[-1])
    return np.array([[0., 1.], [-4., -resistance]])


def poles(model: str, h: float, method: str, substeps: int = 1) -> np.ndarray:
    A = system_matrix(model)
    I = np.eye(len(A))
    Q = h*A
    if method == "exact":
        return np.exp(h*np.linalg.eigvals(A))
    if method.startswith("euler_m") or method.startswith("euler_equal"):
        M = np.linalg.matrix_power(I+Q/substeps, substeps)
    elif method == "trapezoid":
        M = np.linalg.solve(I-Q/2, I+Q/2)
    elif method == "gauss_legendre_2":
        P = I-Q/2+Q@Q/12
        M = np.linalg.solve(P, I+Q/2+Q@Q/12)
    elif method == "euler_cromer" and model == "oscillator":
        M = np.array([[1-h*h, h], [-h, 1.]])
    else:
        raise ValueError(method)
    return np.linalg.eigvals(M)


def plot_poles(directory: Path) -> None:
    for model, h in (("oscillator", .1), ("rc_discharge", 3.), ("rlc_r1", .2),
                     ("rlc_r4", .2), ("rlc_r6", .2)):
        fig, ax = plt.subplots(figsize=(6, 6))
        ax.add_patch(Circle((0, 0), 1, fill=False, linestyle="--", color="grey"))
        methods = ["exact", "euler_m1", "euler_m8", "euler_m32", "trapezoid", "gauss_legendre_2"]
        if model == "oscillator":
            methods.append("euler_cromer")
        for method in methods:
            ps = poles(model, h, method, int(method[7:]) if method.startswith("euler_m") else 1)
            ax.scatter(ps.real, ps.imag, label=method)
        ax.set(xlabel="Re(z)", ylabel="Im(z)", title=f"{model}, outer h={h:g} s: discrete poles")
        ax.axis("equal"); ax.grid(True); ax.legend(fontsize=8)
        fig.savefig(directory/f"{model}_poles.png", dpi=150)
        plt.close(fig)


def plot_oscillator(directory: Path, rows: list[dict]) -> None:
    h = .1
    methods = ["euler_m1", "euler_m8", "euler_m32", "trapezoid", "gauss_legendre_2", "euler_cromer"]
    fig, ax = plt.subplots(figsize=(8, 5))
    for method in methods:
        data = trajectory(directory, "oscillator", h, method)
        mask = data["time_seconds"] <= 20
        ax.plot(data["time_seconds"][mask], data["state0"][mask], label=method)
    data = trajectory(directory, "oscillator", h, methods[0])
    mask = data["time_seconds"] <= 20
    ax.plot(data["time_seconds"][mask], data["reference0"][mask], "k--", label="cos(t)")
    ax.set(xlabel="Time (s)", ylabel="x", title="Two-integrator oscillator, first 20 s, h=0.1 s")
    ax.grid(True); ax.legend(ncol=2, fontsize=8)
    fig.savefig(directory/"oscillator_trajectory.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots(figsize=(8, 5))
    for method in methods:
        data = trajectory(directory, "oscillator", h, method)
        energy = data["state0"]**2+data["state1"]**2
        ax.plot(data["time_seconds"], energy, label=method)
    ax.set(xlabel="Time (s)", ylabel="Energy / initial energy (log scale)", yscale="log",
           title="Oscillator energy over 100 s, h=0.1 s")
    ax.grid(True); ax.legend(ncol=2, fontsize=8)
    fig.savefig(directory/"oscillator_energy.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for method in ("euler_m8", "euler_m32", "trapezoid", "gauss_legendre_2", "euler_cromer"):
        subset = sorted((r for r in rows if r["model"] == "oscillator" and r["method"] == method),
                        key=lambda r: r["h"])
        ax.loglog([r["h"] for r in subset], [abs(r["final_phase_error_rad"]) for r in subset],
                  marker="o", label=method)
    ax.set(xlabel="Outer h (s)", ylabel="|Final phase error| (rad)",
           title="Oscillator phase error at 100 s")
    ax.grid(True); ax.legend(fontsize=8)
    fig.savefig(directory/"oscillator_phase_error.png", dpi=150); plt.close(fig)


def plot_rc(directory: Path, rows: list[dict]) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(11, 4), sharey=True)
    for ax, model in zip(axes, ("rc_charge", "rc_discharge")):
        for method in ("euler_m1", "euler_m4", "euler_m16", "trapezoid", "gauss_legendre_2"):
            data = trajectory(directory, model, .5, method)
            mask = data["time_seconds"] <= 5
            ax.plot(data["time_seconds"][mask], data["state0"][mask], label=method)
        data = trajectory(directory, model, .5, "euler_m1")
        mask = data["time_seconds"] <= 5
        ax.plot(data["time_seconds"][mask], data["reference0"][mask], "k--", label="exact")
        ax.set(xlabel="Time / RC", title=model.replace("_", " "))
        ax.grid(True)
    axes[0].set_ylabel("Capacitor voltage / input")
    axes[1].legend(fontsize=8)
    fig.suptitle("Real S7 RC graphs, h/RC=0.5")
    fig.tight_layout(); fig.savefig(directory/"rc_charge_discharge.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for method in ("euler_m1", "euler_m2", "euler_m8", "euler_m32", "trapezoid", "gauss_legendre_2"):
        subset = sorted((r for r in rows if r["model"] == "rc_charge" and r["method"] == method
                         and not r["failure"]), key=lambda r: r["h"])
        ax.loglog([r["h"] for r in subset], [r["max_trajectory_error"] for r in subset],
                  marker="o", label=method)
    ax.set(xlabel="Outer h / RC", ylabel="Max transient voltage error", title="RC error vs outer step")
    ax.grid(True); ax.legend(fontsize=8)
    fig.savefig(directory/"rc_error_vs_h.png", dpi=150); plt.close(fig)


def plot_rlc(directory: Path) -> None:
    fig, axes = plt.subplots(3, 1, figsize=(9, 9), sharex=True)
    for ax, model in zip(axes, ("rlc_r1", "rlc_r4", "rlc_r6")):
        for method in ("euler_m1", "euler_m8", "euler_m32", "trapezoid", "gauss_legendre_2"):
            data = trajectory(directory, model, .2, method)
            mask = data["time_seconds"] <= 10
            ax.plot(data["time_seconds"][mask], 4*data["state0"][mask], label=method)
        data = trajectory(directory, model, .2, "euler_m1")
        mask = data["time_seconds"] <= 10
        ax.plot(data["time_seconds"][mask], 4*data["reference0"][mask], "k--", label="exact ZOH")
        ax.set(ylabel="Vc (V)", title=f"R={model[-1]} Ω")
        ax.grid(True)
    axes[-1].set_xlabel("Time (s)")
    axes[0].legend(ncol=3, fontsize=8)
    fig.suptitle("RLC step response, L=1 H, C=0.25 F, outer h=0.2 s")
    fig.tight_layout(); fig.savefig(directory/"rlc_steps.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for method in ("euler_m1", "euler_m8", "euler_m32", "trapezoid", "gauss_legendre_2"):
        data = trajectory(directory, "rlc_r0", .2, method)
        ratio = (4*data["state0"]**2+data["state1"]**2)/.25
        ax.plot(data["time_seconds"], ratio, label=method)
    ax.set(xlabel="Time (s)", ylabel="Energy / initial energy (log scale)", yscale="log",
           title="Lossless RLC energy, h=0.2 s")
    ax.grid(True); ax.legend(fontsize=8)
    fig.savefig(directory/"rlc_lossless_energy.png", dpi=150); plt.close(fig)


def plot_work(directory: Path, rows: list[dict]) -> None:
    fig, axes = plt.subplots(1, 3, figsize=(13, 4))
    for ax, (model, h) in zip(axes, (("oscillator", .1), ("rc_charge", .5), ("rlc_r1", .2))):
        subset = [r for r in rows if r["model"] == model and r["h"] == h and
                  (r["method"].startswith("euler_m") or r["method"] == "gauss_legendre_2")]
        for r in subset:
            method = r["method"]
            ax.scatter(r["total_derivative_evaluations"], r["max_trajectory_error"],
                       marker="*" if method == "gauss_legendre_2" else "o",
                       s=100 if method == "gauss_legendre_2" else 25,
                       label=method)
        ax.set(xlabel="Total derivative-vector evaluations", ylabel="Max state error",
               xscale="log", yscale="log", title=model)
        ax.grid(True)
    axes[-1].legend(fontsize=7, loc="best")
    fig.suptitle("Equal outer step; measured graph work versus continuous-reference error")
    fig.tight_layout(); fig.savefig(directory/"error_vs_work.png", dpi=150); plt.close(fig)

    fig, ax = plt.subplots()
    for model, h in (("oscillator", .1), ("rc_charge", .5), ("rlc_r1", .2)):
        subset = [by_case(rows, model, h, method) for method in
                  ("euler_m1", "euler_m8", "euler_m16", "euler_m32", "trapezoid", "gauss_legendre_2")]
        ax.plot([r["method"] for r in subset], [r["wall_seconds"]/(r["outer_steps"]*h) for r in subset],
                marker="o", label=model)
    ax.set(xlabel="Method", ylabel="Median step-only wall seconds / simulated second",
           yscale="log", title="Release C++ graph execution; five repetitions")
    ax.tick_params(axis="x", rotation=35); ax.grid(True); ax.legend()
    fig.tight_layout(); fig.savefig(directory/"execution_cost.png", dpi=150); plt.close(fig)


def compact(rows: list[dict], directory: Path) -> dict:
    root = Path(__file__).resolve().parents[1]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    branch = subprocess.check_output(["git", "branch", "--show-current"], text=True).strip()
    binary = root/"build-release/s7tests/s7tests_numerical_integration_research"
    source_files = [root/"s7tests"/name for name in (
        "numerical_integration_research.cpp", "numerical_integration_research.hpp",
        "numerical_integration_models.hpp", "numerical_integration_research_smoke.cpp")]
    sources = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    sources[Path(__file__).name] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    selected = []
    for model, h in (("oscillator", .1), ("rc_charge", .5), ("rc_discharge", 3.),
                     ("rlc_r0", .2), ("rlc_r1", .2), ("rlc_r4", .2), ("rlc_r6", .2)):
        for method in ("euler_m1", "euler_m2", "euler_m4", "euler_m8", "euler_m16", "euler_m32",
                       "trapezoid", "gauss_legendre_2"):
            r = by_case(rows, model, h, method)
            selected.append({key: r[key] for key in ("model", "method", "h", "outer_steps", "failure",
                                                    "max_trajectory_error", "final_trajectory_error",
                                                    "max_relative_energy_drift", "final_phase_error_rad", "overshoot",
                                                    "total_derivative_evaluations", "pure_element_evaluations",
                                                    "solver_iterations", "stage_residual_evaluations",
                                                    "wall_seconds", "cpu_seconds", "temporary_bytes_upper_bound")})
    matching = []
    for model, h in (("oscillator", .1), ("rc_charge", .5), ("rc_discharge", 3.),
                     ("rlc_r0", .2), ("rlc_r1", .2), ("rlc_r4", .2), ("rlc_r6", .2)):
        gl = by_case(rows, model, h, "gauss_legendre_2")
        trap = by_case(rows, model, h, "trapezoid")
        eulers = [by_case(rows, model, h, f"euler_m{m}") for m in (1, 2, 4, 8, 16, 32)]
        def first_match(reference):
            if reference["failure"]:
                return None
            return next((r["euler_substeps"] for r in eulers
                         if r["max_trajectory_error"] <= reference["max_trajectory_error"]), None)
        equal = next(r for r in rows if r["model"] == model and r["h"] == h and
                     r["method"].startswith("euler_equal_gl_work"))
        matching.append(dict(model=model, h=h, euler_m_to_match_trapezoid=first_match(trap),
                             euler_m_to_match_gl=first_match(gl),
                             gl_rhs_evaluations=gl["total_derivative_evaluations"],
                             equal_work_euler_substeps=equal["euler_substeps"],
                             equal_work_euler_rhs_evaluations=equal["total_derivative_evaluations"],
                             equal_work_euler_max_error=equal["max_trajectory_error"],
                             gl_max_error=gl["max_trajectory_error"]))
    pole_rows = []
    for model, h in (("oscillator", .1), ("rc_discharge", 3.), ("rlc_r1", .2),
                     ("rlc_r4", .2), ("rlc_r6", .2)):
        for method, m in (("exact", 1), ("euler_m1", 1), ("euler_m8", 8),
                          ("euler_m32", 32), ("trapezoid", 1), ("gauss_legendre_2", 1)):
            for pole in poles(model, h, method, m):
                pole_rows.append(dict(model=model, h=h, method=method, real=float(pole.real),
                                      imag=float(pole.imag), magnitude=float(abs(pole)),
                                      damping=float(-math.log(abs(pole))/h) if pole != 0 else None,
                                      frequency=float(abs(np.angle(pole))/h)))
    metadata = dict(branch=branch, git_commit=revision,
                    generated_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    source_sha256=sources, precision="IEEE-754 binary64",
                    python=sys.version.split()[0], numpy=np.__version__,
                    matplotlib=matplotlib.__version__, platform=platform.platform(),
                    compiler="GCC 16.2.1 Release (build-release CMake cache; see reproduction command)",
                    configuration=dict(models="oscillator, RC charge/discharge, RLC R=0/1/4/6",
                                       outer_h="oscillator .05/.1/.2; RC .1/.5/2/3; RLC .05/.1/.2/.5",
                                       euler_substeps=[1, 2, 4, 8, 16, 32],
                                       solver="damped finite-difference Newton, 12 iterations max"),
                    tolerances=dict(solver_atol=1e-13, solver_rtol=1e-13,
                                    regression="see C++ smoke test assertions"),
                    reproduce="cmake --build build-release --target s7tests_numerical_integration_research && ./build-release/s7tests/s7tests_numerical_integration_research build-release/s7tests/phase3 && MPLCONFIGDIR=/tmp/s7-mpl python3 -B tools/plot_numerical_integration_phase3.py --input build-release/s7tests/phase3 --compact docs/NUMERICAL_INTEGRATION_PHASE3_SUMMARY.json",
                    notes="C++ stage execution is primary; Python computes figures and compact summaries from CSV/JSON. Timings are five-repeat medians of step-only Release loops.")
    return dict(metadata=metadata, case_count=len(rows),
                failed_cases=[dict(model=r["model"], h=r["h"], method=r["method"], failure=r["failure"])
                              for r in rows if r["failure"]],
                selected_cases=selected, equal_work_and_thresholds=matching, selected_poles=pole_rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path("build-release/s7tests/phase3"))
    parser.add_argument("--compact", type=Path, default=Path("docs/NUMERICAL_INTEGRATION_PHASE3_SUMMARY.json"))
    args = parser.parse_args()
    rows = json.loads((args.input/"results.json").read_text())["cases"]
    plot_poles(args.input)
    plot_oscillator(args.input, rows)
    plot_rc(args.input, rows)
    plot_rlc(args.input)
    plot_work(args.input, rows)
    result = compact(rows, args.input)
    args.compact.parent.mkdir(parents=True, exist_ok=True)
    args.compact.write_text(json.dumps(result, separators=(",", ":"), indent=1)+"\n")
    print(f"{len(rows)} C++ cases, {len(result['failed_cases'])} explicit failures; figures and {args.compact}")


if __name__ == "__main__":
    main()
