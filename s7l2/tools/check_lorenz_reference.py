#!/usr/bin/env python3
"""Independent short-horizon refinement check for the Lorenz reference."""

import json
from pathlib import Path
import argparse

import numpy as np
from scipy.integrate import solve_ivp


def lorenz(t, state):
    x, y, z = state
    return [10*(y-x), x*(28-z)-y, x*y-(8/3)*z]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    times = np.linspace(0, 2, 1001)
    results = []
    for initial in ([1,1,1], [1.000001,1,1]):
        common = dict(method="DOP853", rtol=2e-13, atol=2e-14, dense_output=True)
        first = solve_ivp(lorenz, (0,2), initial, **common)
        refined = solve_ivp(lorenz, (0,2), initial, max_step=0.005, **common)
        if not first.success or not refined.success:
            raise RuntimeError("Lorenz reference integration failed")
        discrepancy = np.linalg.norm(first.sol(times).T-refined.sol(times).T, axis=1)
        results.append(dict(initial=initial, duration=2, max_state_discrepancy=float(np.max(discrepancy)),
                            first_nfev=first.nfev, refined_nfev=refined.nfev))
    args.output.write_text(json.dumps(dict(method="DOP853", rtol=2e-13, atol=2e-14,
                                           refined_max_step=0.005, results=results), indent=2)+"\n")


if __name__ == "__main__":
    main()
