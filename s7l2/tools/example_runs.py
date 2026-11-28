# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Saved run lengths of the generated general examples.

Every bundled diagram carries "studio_run": its documented local run, which
Studio offers and the bundled-example GUI test executes. Generators apply
with_run() when they write a diagram, so regenerating keeps it.
"""

from pathlib import Path

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"

# First matching prefix (relative to examples/) wins.
SECONDS = [
    ("surfaces/", 5), ("complex_functions/", 5), ("frequency_response/", 2),
    ("time_frequency/pll_acquisition", 6), ("time_frequency/", 8), ("analysis_elements/", 2),
    ("linear/rlc_resonance", 30), ("linear/", 10), ("control/", 12), ("buck/", 1), ("pwm_pid_rlc", 1),
    ("relay/", 1200), ("pendulum/", 100), ("double_pendulum", 20),
    ("van_der_pol/van_der_pol_mu3", 60), ("van_der_pol/", 40), ("lorenz/lorenz_stable", 10), ("lorenz/", 40),
    ("lotka_volterra/", 40), ("pll/", 6), ("signal_measurement/iq_correlation", 8), ("signal_measurement/", 2),
    ("spectrum/", 4), ("audio/two_node_equalizer", 15), ("2int_osc", 10), ("3int_helix", 10), ("symplectic_osc_live", 10), ("demo", 10),
]


def run_seconds(path):
    relative = Path(path).resolve().relative_to(EXAMPLES).as_posix()
    for prefix, seconds in SECONDS:
        if relative.startswith(prefix):
            return seconds
    raise KeyError(f"no saved run length for {relative}; add it to tools/example_runs.py")


def with_run(document, path):
    """The document with its studio_run placed after "sheet"."""
    run = {"runtime_seconds": run_seconds(path), "clear_scope_traces": True}
    result = {}
    for key, value in document.items():
        if key == "studio_run":
            continue
        result[key] = value
        if key == "sheet":
            result["studio_run"] = run
    result.setdefault("studio_run", run)
    return result
