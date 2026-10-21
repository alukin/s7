# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Tests of s7py: canonical JSON, graph semantics, diagram parts and the control protocol.

Run from s7py: python3 -m unittest discover -s tests
"""
import asyncio
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

from s7node import diagram as diagrams  # noqa: E402
from s7node.graph import Graph, SchemaError  # noqa: E402
from s7node.jcs import canonicalize, configuration_digest, loads  # noqa: E402

CERTS = ROOT.parent / "s7l2" / "s7runtime" / "examples" / "certs"
FIXTURE = ROOT.parent / "s7l2" / "s7runtime" / "tests" / "fixtures" / "control_v1.json"
NODE = "11111111-2222-3333-4444-555555555555"
OTHER = "33333333-4444-5555-6666-777777777777"


class CanonicalJsonTest(unittest.TestCase):
    def test_numbers_follow_ecmascript(self):
        # RFC 8785 appendix B and ECMAScript Number::toString corner cases.
        cases = {
            0.0: "0", -0.0: "0", 2.0: "2", 4.5: "4.5", 0.002: "0.002", 1e-7: "1e-7", 1e-6: "0.000001",
            1e20: "100000000000000000000", 1e21: "1e+21", 1e30: "1e+30", 333333333.3333333: "333333333.3333333",
            5e-324: "5e-324", 1.7976931348623157e308: "1.7976931348623157e+308", -1.5e-9: "-1.5e-9",
            123456789012345680000.0: "123456789012345680000",
        }
        for value, text in cases.items():
            self.assertEqual(canonicalize(value), text, value)

    def test_members_sorted_by_utf16(self):
        # U+1F600 (surrogates D83D DE00) sorts before U+FB33 in UTF-16, after it in code points.
        self.assertEqual(canonicalize({"דּ": 1, "\U0001f600": 2, "a": [True, None]}),
                         '{"a":[true,null],"\U0001f600":2,"דּ":1}')

    def test_large_integers_refused(self):
        with self.assertRaises(ValueError):
            canonicalize(2**53 + 1)

    def test_fixture_digests(self):
        """The digests in the conformance script were computed independently (tools/gen_control_fixtures.py)."""
        script = json.loads(FIXTURE.read_text())
        checked = 0
        for step in script["steps"]:
            configuration = step["request"].get("deployment", {}).get("configuration")
            if step["name"] != "prepare_tampered" and isinstance(configuration, dict):
                self.assertEqual(configuration_digest(configuration), configuration["configuration_digest"], step["name"])
                checked += 1
        self.assertGreater(checked, 0)

    def test_duplicate_members_refused(self):
        with self.assertRaises(ValueError):
            loads('{"a": 1, "a": 2}')


def schema(elements, connections=()):
    return {"name": "t", "elements": elements,
            "connections": [{"from_element": a, "from_port": 0, "to_element": b, "to_port": p} for a, b, p in connections]}


class GraphTest(unittest.TestCase):
    def test_feedthrough_runs_in_causal_order(self):
        # Listed sink-first: still computed source-first within one step.
        g = Graph(schema([{"id": "g", "type": "Gain", "params": {"factor": 3}},
                          {"id": "c", "type": "Constant", "params": {"value": 2}}], [("c", "g", 0)]))
        g.step()
        self.assertEqual(g.sample("g"), 6.0)

    def test_delay_publishes_one_step_later(self):
        g = Graph(schema([{"id": "c", "type": "Constant", "params": {"value": 1}},
                          {"id": "d", "type": "Delay", "params": {"initial_value": -1}},
                          {"id": "d2", "type": "PreviousValue", "params": {}}],
                         [("c", "d", 0), ("d", "d2", 0)]))
        seen = []
        for _ in range(3):
            g.step()
            seen.append((g.sample("d"), g.sample("d2")))
        # Outputs read during the next step: d is 1 from step 1, d2 one step after d.
        self.assertEqual(seen, [(1.0, -1.0), (1.0, 1.0), (1.0, 1.0)])

    def test_counter_loop_through_delay(self):
        # y = 1 + z^-1 y: an Adder fed back through a Delay counts steps.
        g = Graph(schema([{"id": "one", "type": "Constant", "params": {"value": 1}},
                          {"id": "sum", "type": "Adder", "params": {}},
                          {"id": "z", "type": "Delay", "params": {}}],
                         [("one", "sum", 0), ("z", "sum", 1), ("sum", "z", 0)]))
        for _ in range(5):
            g.step()
        self.assertEqual(g.sample("sum"), 5.0)

    def test_euler_integrator_closes_a_loop(self):
        # x' = -x, x(0) = 1, forward Euler: x[n] = (1 - dt)^n.
        dt = 0.01
        g = Graph(schema([{"id": "x", "type": "EulerIntegrator", "params": {"dt": dt, "initial_value": 1}},
                          {"id": "neg", "type": "Gain", "params": {"factor": -1}}],
                         [("x", "neg", 0), ("neg", "x", 0)]))
        for _ in range(100):
            g.step()
        self.assertAlmostEqual(g.sample("x"), (1 - dt) ** 100, places=12)

    def test_oscillator(self):
        g = Graph(schema([{"id": "o", "type": "AnalogOscillator",
                           "params": {"amplitude": 2, "frequency_hz": 1, "sample_rate_hz": 4, "phase": 0}}]))
        values = []
        for _ in range(3):
            g.step()
            values.append(g.sample("o"))
        self.assertTrue(all(math.isclose(a, b, abs_tol=1e-12) for a, b in zip(values, [0, 2, 0])))

    def test_invalid_schemas(self):
        cases = {
            "cycle without causal memory": schema(
                [{"id": "a", "type": "Gain"}, {"id": "b", "type": "Gain"}], [("a", "b", 0), ("b", "a", 0)]),
            "is not connected": schema([{"id": "g", "type": "Gain"}]),
            "more than one driver": schema(
                [{"id": "c", "type": "Constant"}, {"id": "g", "type": "Gain"}], [("c", "g", 0), ("c", "g", 0)]),
            "unknown element type": schema([{"id": "s", "type": "Scope"}]),
            "data streams": schema([{"id": "s", "type": "NetSink"}]),
            "F64 only": schema([{"id": "c", "type": "Constant", "params": {"type": "I32", "value": 1}}]),
            "input port 2": schema([{"id": "c", "type": "Constant"}, {"id": "a", "type": "Adder"}],
                                   [("c", "a", 0), ("c", "a", 1), ("c", "a", 2)]),
        }
        for message, bad in cases.items():
            with self.assertRaisesRegex(SchemaError, message):
                Graph(bad)


class DiagramTest(unittest.TestCase):
    def diagram(self, connections, streams=None):
        d = {"schema": schema([{"id": "a", "type": "Constant", "node": NODE},
                               {"id": "b", "type": "Gain", "node": NODE},
                               {"id": "c", "type": "Gain", "node": OTHER}], connections)}
        if streams is not None:
            d["streams"] = streams
        return d

    def test_node_part(self):
        part = diagrams.extract_node_schema(self.diagram([("a", "b", 0)]), NODE)
        self.assertEqual(part["name"], f"t@{NODE}")
        self.assertEqual([e["id"] for e in part["elements"]], ["a", "b"])
        self.assertNotIn("node", part["elements"][0])
        self.assertEqual(len(part["connections"]), 1)
        self.assertEqual(diagrams.placed_nodes(self.diagram([])), [NODE, OTHER])

    def test_connection_between_nodes_needs_a_stream(self):
        with self.assertRaisesRegex(SchemaError, "leaves this node without a stream"):
            diagrams.extract_node_schema(self.diagram([("a", "b", 0), ("b", "c", 0)]), NODE)

    def test_stream_ends_are_refused(self):
        stream = {"stream_id": "5d4c3b2a-1f0e-4d9c-8b7a-6f5e4d3c2b1a", "data_type": "F64",
                  "producer": {"node_uuid": NODE, "element": "b", "port": 0},
                  "consumers": [{"node_uuid": OTHER, "element": "c", "port": 0}]}
        with self.assertRaisesRegex(SchemaError, "no data plane"):
            diagrams.extract_node_schema(self.diagram([("a", "b", 0), ("b", "c", 0)], [stream]), NODE)

    def test_model_time_step(self):
        self.assertEqual(diagrams.model_time_step_ns({"dt": 0.001}), 1_000_000)
        self.assertIsNone(diagrams.model_time_step_ns({}))
        with self.assertRaises(SchemaError):
            diagrams.model_time_step_ns({"dt": 0})


def free_port() -> int:
    import socket

    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class NodeProcess:
    """An s7node process on a free port with its own state directory."""

    def __init__(self, config: dict, state_dir: Path, *args):
        self.port = free_port()
        config.setdefault("websocket", {}).setdefault("server", {})["port"] = self.port
        path = state_dir.parent / f"{state_dir.name}.json"
        path.write_text(json.dumps(config))
        self.process = subprocess.Popen(
            [sys.executable, "-m", "s7node", "--schema", str(path), "--state-dir", str(state_dir), *args],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.lines = []
        for line in self.process.stdout:  # ready once it prints its control URL
            self.lines.append(line)
            if line.startswith("runtime.listen_url="):
                break

    def stop(self) -> str:
        self.process.terminate()
        output, _ = self.process.communicate(timeout=10)
        return "".join(self.lines) + output


def have_cryptography() -> bool:
    try:
        import cryptography  # noqa: F401
    except ImportError:
        return False
    return True


@unittest.skipUnless(have_cryptography() and CERTS.exists(), "needs the cryptography package and s7l2 test certificates")
class ConformanceTest(unittest.TestCase):
    def test_control_fixtures(self):
        """The control protocol conformance script, over mTLS, except its data-stream steps."""
        from check_control_fixtures import run

        from s7node.tls import client_context, load_certificate_file

        studio = load_certificate_file(str(CERTS / "s7_studio.cert.pem"))
        config = {
            "websocket": {"server": {"bind_address": "127.0.0.1", "tls": {
                "cert_file": str(CERTS / "s7_runtime_node.cert.pem"), "key_file": str(CERTS / "s7_runtime_node.key.pem"),
                "ca_file": str(CERTS / "s7_root_ca.cert.pem")}}},
            "control": {"studio_key_purpose_oid": "2.25.4266950079767543379770189370854073200",
                        "studio_certificate_sha256": studio.fingerprint_sha256, "max_configuration_bytes": 2048},
            "clock": {"assume_synchronized": True},
        }
        ca = str(CERTS / "s7_root_ca.cert.pem")
        contexts = {"anonymous": client_context(ca),
                    "studio": client_context(ca, str(CERTS / "s7_studio.cert.pem"), str(CERTS / "s7_studio.key.pem"))}
        with tempfile.TemporaryDirectory() as tmp:
            node = NodeProcess(config, Path(tmp) / "node")
            try:
                failures = asyncio.run(run(f"wss://127.0.0.1:{node.port}", contexts, skip_streams=True))
            finally:
                output = node.stop()
            self.assertEqual(failures, 0, output)
            # The committed revision 1 was persisted and is restored by the next process.
            node = NodeProcess(config, Path(tmp) / "node")
            output = node.stop()
            self.assertIn("deployment.restored deployment_id=fixture-deployment revision=1", output)


class PlainWebSocketTest(unittest.TestCase):
    def test_read_only_requests_and_refused_mutation(self):
        from s7node.websocket import connect

        async def talk(port):
            ws = await connect("127.0.0.1", port, "/control")
            responses = []
            for request in ({"type": "HELLO", "request_id": "h", "protocol_version": 1},
                            {"type": "LIST_DEPLOYMENTS", "request_id": "l", "protocol_version": 1},
                            {"type": "PREPARE_DEPLOYMENT", "request_id": "p", "protocol_version": 1, "deployment": {}}):
                await ws.send(json.dumps(request))
                responses.append(json.loads(await ws.recv()))
            await ws.close()
            return responses

        startup = schema([{"id": "c", "type": "Constant", "params": {"value": 1}},
                          {"id": "acc", "type": "Accumulator", "params": {}}], [("c", "acc", 0)])
        with tempfile.TemporaryDirectory() as tmp:
            node = NodeProcess({**startup, "execution": {"step_rate_hz": 50}}, Path(tmp) / "node")
            try:
                hello, deployments, prepare = asyncio.run(talk(node.port))
            finally:
                node.stop()
        self.assertEqual(hello["type"], "HELLO_OK")
        self.assertEqual(hello["status"], "idle")
        self.assertEqual(deployments["execution"], "stepping")
        self.assertEqual(deployments["timing"]["pacing"], "rate")
        self.assertEqual((prepare["type"], prepare["code"]), ("ERROR", "UNAUTHORIZED"))


if __name__ == "__main__":
    unittest.main()
