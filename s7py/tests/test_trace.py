# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""python3 -m s7node.trace with the example element library: the developer's local run."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "examples" / "elements" / "smoothing.py"


def trace(*args):
    run = subprocess.run([sys.executable, "-m", "s7node.trace", *map(str, args)], cwd=ROOT,
                         capture_output=True, text=True, timeout=60)
    return run.returncode, run.stdout, run.stderr


def schema_file(directory, elements, connections, **extra):
    path = Path(directory) / "schema.json"
    path.write_text(json.dumps({**extra, "elements": elements, "connections": [
        {"from_element": a, "to_element": b, "to_port": port} for a, b, port in connections]}))
    return path


class TraceTest(unittest.TestCase):
    def test_example(self):
        code, out, err = trace(ROOT / "examples" / "smoothing.json", "--lib", LIBRARY, "--steps", 6, "--order")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(out.splitlines(), [
            "# rate: 100 Hz", "# causal order: input average", "# causal memory: smooth",
            "step,t,input,average,smooth",
            "0,0.0,0.0,0.0,0.0",
            "1,0.01,0.0,0.0,0.0",
            "2,0.02,1.0,0.3333333333333333,0.0",  # the smoother sees the step one step later
            "3,0.03,1.0,0.5,0.5",
            "4,0.04,1.0,0.75,0.75",
            "5,0.05,1.0,1.0,0.875"])

    def test_memory_closes_a_loop_and_feedthrough_does_not(self):
        # e = 1 - y, y the smoothed e, one step behind: y[1] = e[0] / 2 = 1/2, the fixed point.
        elements = [{"id": "one", "type": "Constant", "params": {"value": 1}},
                    {"id": "e", "type": "Adder", "params": {"weights": [1, -1]}},
                    {"id": "y", "type": "ExponentialSmoother", "params": {"alpha": 0.5}}]
        with tempfile.TemporaryDirectory() as tmp:
            path = schema_file(tmp, elements, [("one", "e", 0), ("y", "e", 1), ("e", "y", 0)])
            code, out, err = trace(path, "--lib", LIBRARY, "--rate", 1, "--steps", 4, "--watch", "e", "--watch", "y")
            self.assertEqual((code, err), (0, ""))
            self.assertEqual(out.splitlines()[1:], ["0,0.0,1.0,0.0", "1,1.0,0.5,0.5", "2,2.0,0.5,0.5",
                                                    "3,3.0,0.5,0.5"])
            elements[2] = {"id": "y", "type": "MovingAverage"}
            path = schema_file(tmp, elements, [("one", "e", 0), ("y", "e", 1), ("e", "y", 0)])
            code, out, err = trace(path, "--lib", LIBRARY, "--rate", 1)
            self.assertEqual(code, 1)
            self.assertIn("error: cycle without causal memory", err)

    def test_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = schema_file(tmp, [{"id": "a", "type": "MovingAverage", "params": {"length": 0}},
                                     {"id": "c", "type": "Constant"}], [("c", "a", 0)])
            self.assertEqual(trace(path, "--lib", LIBRARY, "--rate", 1)[2],
                             "error: element 'a': parameter 'length' must be at least 1\n")
            self.assertIn("unknown element type 'MovingAverage'", trace(path, "--rate", 1)[2])
            path = schema_file(tmp, [{"id": "n", "type": "Constant", "params": {"value": 1}},
                                     {"id": "z", "type": "Constant"}, {"id": "d", "type": "Divider"}],
                               [("n", "d", 0), ("z", "d", 1)])
            code, out, err = trace(path, "--rate", 1, "--watch", "d")
            self.assertEqual((code, out, err), (1, "step,t,d\n",
                                                "error at step 0: RuntimeError: Divider denominator is zero.\n"))
            path = schema_file(tmp, [{"id": "c", "type": "Constant"}], [])
            self.assertIn("rate of 'c' cannot be determined", trace(path)[2])
            self.assertIn("--watch x: no element 'x'", trace(path, "--rate", 1, "--watch", "x")[2])

    def test_list_and_describe(self):
        code, out, _ = trace("--list", "--lib", LIBRARY)
        self.assertEqual(code, 0)
        self.assertLessEqual({"Adder", "NetSink", "MovingAverage", "ExponentialSmoother"}, set(out.split()))
        code, out, _ = trace("--describe", "ExponentialSmoother", "--lib", LIBRARY)
        entry = json.loads(out)
        self.assertEqual((entry["type_name"], entry["category"], [p["name"] for p in entry["parameters"]]),
                         ("ExponentialSmoother", "filters", ["type", "alpha", "initial_value"]))


if __name__ == "__main__":
    unittest.main()
