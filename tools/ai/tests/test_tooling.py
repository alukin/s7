#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure-stdlib regression tests for S7 AI development tooling."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))
import benchmark
import env_probe
import select_tests
import task_handoff
import validate_diagram


def example_document():
    return {"document_kind": "s7.diagram", "diagram_version": 2,
            "dt": 0.001,
            "schema": {
                "name": "tiny", "elements": [
                    {"id": "a", "type": "Constant", "params": {"value": 1}},
                    {"id": "b", "type": "Gain", "params": {"factor": 2}},
                    {"id": "c", "type": "Delay", "params": {"initial_value": 0}}],
                "connections": [
                    {"from_element": "a", "from_port": 0, "to_element": "b", "to_port": 0},
                    {"from_element": "a", "from_port": 0, "to_element": "c", "to_port": 0}]},
            "diagram": {"nodes": [
                {"id": "a", "position": [1, 1]},
                {"id": "b", "position": [10, 1]},
                {"id": "c", "position": [10, 10]}],
                "wires": [
                    {"id": "w1", "from": {"port": ["a", "out", 0]},
                     "to": {"port": ["b", "in", 0]}},
                    {"id": "w2", "from": {"wire": "w1", "at": [6, 1]},
                     "to": {"port": ["c", "in", 0]}}]}}


class DiagramTests(unittest.TestCase):
    def test_branch_wires_match_schema(self):
        errors, _ = validate_diagram.inspect_document(example_document())
        self.assertEqual(errors, [])

    def test_missing_branch_detected(self):
        value = example_document()
        value["diagram"]["wires"].pop()
        errors, _ = validate_diagram.inspect_document(value)
        self.assertTrue(any("missing functional connections" in e for e in errors), errors)

    def test_junction_cycle_detected(self):
        value = example_document()
        value["diagram"]["wires"][0]["from"] = {"wire": "w2", "at": [1, 1]}
        errors, _ = validate_diagram.inspect_document(value)
        self.assertTrue(any("cycle in wire" in e for e in errors), errors)

    def test_wrong_port_index_detected(self):
        value = example_document()
        value["schema"]["connections"][0]["from_port"] = True
        errors, _ = validate_diagram.inspect_document(value)
        self.assertTrue(any("invalid port" in e for e in errors), errors)

    def test_layout_only_projection(self):
        a = example_document()
        b = json.loads(json.dumps(a))
        b["diagram"]["nodes"][0]["position"] = [2, 2]
        self.assertEqual(validate_diagram.functional_content(a), validate_diagram.functional_content(b))
        b["schema"]["elements"][0]["params"]["value"] = 10
        self.assertNotEqual(validate_diagram.functional_content(a), validate_diagram.functional_content(b))

    def test_existing_oscillator_saved_diagram(self):
        path = ROOT / "s7l2/examples/2int_osc.s7diagram"
        doc = validate_diagram.read_document(path)
        errors, _ = validate_diagram.inspect_document(doc)
        self.assertEqual(errors, [])


class SelectionTests(unittest.TestCase):
    def test_diagram_routes_to_focused_tests(self):
        rules = json.loads(select_tests.DEFAULT_MAP.read_text())["rules"]
        r = select_tests.select(["s7l2/s7studio/src/diagram/diagram_scene.cpp"], rules)
        self.assertTrue(r["gui_patterns_deferred"])
        names, unmatched = select_tests.find_tests(
            ["s7studio_diagram", "s7studio_edit_check", "s7studio_gui_workflows"],
            r["ctest_patterns"])
        self.assertIn("s7studio_diagram", names)
        self.assertNotIn("s7studio_gui_workflows", names)
        self.assertEqual(unmatched, [])

    def test_unknown_file_is_visible(self):
        rules = json.loads(select_tests.DEFAULT_MAP.read_text())["rules"]
        report = select_tests.select(["new_component/unknown.cpp"], rules)
        self.assertEqual(report["unmapped_files"], ["new_component/unknown.cpp"])

    def test_patterns_missing_from_catalog_not_silent(self):
        names, missing = select_tests.find_tests(["s7core_smoke"], ["^s7studio_diagram$"])
        self.assertEqual(names, [])
        self.assertEqual(missing, ["^s7studio_diagram$"])


class HandoffTests(unittest.TestCase):
    def test_requires_acceptance_evidence(self):
        doc = {"version": 1, "id": "test-task", "goal": "Test",
               "baseline_sha": "a" * 40, "scope": ["s7l2/s7studio"],
               "invariants": ["same schema"], "acceptance_checks": ["load diagram"],
               "status": "planned", "evidence": []}
        self.assertEqual(task_handoff.validate(doc), [])
        doc["status"] = "accepted"
        self.assertTrue(task_handoff.validate(doc))
        doc["evidence"] = [{"command": "ctest --test-dir build -R test", "result": "passed"}]
        self.assertEqual(task_handoff.validate(doc), [])


class BenchmarkTests(unittest.TestCase):
    def test_unknown_tokens_are_not_estimated(self):
        rows = [
            {"task": "a", "variant": "vanilla", "status": "pass",
             "duration_s": 20, "input_tokens": None, "output_tokens": None, "attempts": 1},
            {"task": "a", "variant": "vanilla", "status": "fail",
             "duration_s": 30, "input_tokens": 123, "output_tokens": 45, "attempts": 2},
        ]
        summary = benchmark.evaluate(rows)[0]
        self.assertEqual(summary["pass_rate"], 0.5)
        self.assertIsNone(summary["median_success_tokens"])
        self.assertEqual(summary["successful_runs_with_known_tokens"], 0)

    def test_result_jsonl_line_number(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "results.jsonl"
            file.write_text('{"task":"good"}\nnot-json\n')
            with self.assertRaisesRegex(ValueError, "line 2"):
                benchmark.load_results(file)


class EnvironmentTests(unittest.TestCase):
    def test_profile_is_redacted_and_fingerprinted(self):
        profile = env_probe.build_profile()
        self.assertEqual(len(profile["fingerprint_sha256"]), 64)
        self.assertNotIn("hostname", profile)
        self.assertNotIn("username", profile)
        self.assertNotIn("environment", profile)

    def test_benchmarks_do_not_mix_environment_profiles(self):
        rows = [
            {"task": "same", "variant": "skills", "status": "pass",
             "baseline_sha": "a" * 40, "environment_fingerprint": "1" * 64,
             "duration_s": 11, "input_tokens": 30, "output_tokens": 2},
            {"task": "same", "variant": "skills", "status": "pass",
             "baseline_sha": "a" * 40, "environment_fingerprint": "2" * 64,
             "duration_s": 12, "input_tokens": 40, "output_tokens": 3},
        ]
        self.assertEqual(len(benchmark.evaluate(rows)), 2)


class CLITests(unittest.TestCase):
    def test_help_for_all_tools(self):
        for script in ("validate_diagram.py", "select_tests.py", "task_handoff.py", "benchmark.py", "env_probe.py"):
            with self.subTest(script=script):
                p = subprocess.run([sys.executable, str(TOOLS / script), "--help"],
                                   capture_output=True, text=True)
                self.assertEqual(p.returncode, 0, p.stderr)

    def test_diagram_cli_failure_status(self):
        p = subprocess.run([sys.executable, str(TOOLS / "validate_diagram.py"),
                            "--diagram", "/definitely/nonexistent.s7diagram"],
                           capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('"status": "failed"', p.stdout)


if __name__ == "__main__":
    unittest.main()
