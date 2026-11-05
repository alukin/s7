#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""One stream between two implementations under dropped connections, checked value by value.

Three nodes over mTLS on localhost: a producer A and a consumer B, each C++ (s7runtime), Python
(s7py) or Java (s7java), and an s7py observer C. The diagram:

    A: one (Constant 1) -> count (Accumulator)        count is 1, 2, 3, ...
    B: out (Gain 1); previous = Delay(out); bad = (out - previous != 1); errors = Accumulator(bad)
    C: echo (Gain 1) of errors

A -> B is a stream whose data connection goes through chaos_proxy.py, which resets it every
--drop seconds; B -> C is a second stream without drops. A value lost on A -> B makes one step's
difference 2, a duplicate 0, so errors counts every loss, duplicate or reordering on the
consumer. After --seconds the drops stop, the stream catches up for 3 s, and the script reads
GET_STREAM_STATUS of every end (s7ctl streams; the Java node writes the same form to a file) and
the last watched values, and prints them as JSON.

s7ctl check assigns the streams; deploy_nodes.py prepares and commits the nodes that have a
control plane, and a Java node starts on the same activation time from the deployed diagram.

Needs: a C++ build (--build, with s7runtime and s7ctl), Python with the cryptography package,
openssl (the observer's certificate is issued at run time by the test CA), and for Java nodes
an s7java build (s7java/build.sh) and java 21+.

Usage: run_pair.py <cpp|py|java> <cpp|py|java> [--seconds 20] [--dt 0.001] [--drop 2]
                   [--build ../s7l2/build] [--java java] [--java-tls-resume] [--out result.json]
"""
import argparse
import hashlib
import json
import os
import random
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
CERTS = REPO / "s7l2" / "s7runtime" / "examples" / "certs"  # test certificates, also the test CA
JAVA_BUILD = REPO / "s7java" / "build"
PYTHON = sys.executable
NODE_A, NODE_B = "11111111-2222-3333-4444-555555555555", "33333333-4444-5555-6666-777777777777"  # test node certificates
NODE_C = "55555555-6666-7777-8888-999999999999"  # the observer, certificate issued at run time
OID = "2.25.4266950079767543379770189370854073200"  # the development Studio key purpose
STUDIO_TLS = ["--cert", str(CERTS / "s7_studio.cert.pem"), "--key", str(CERTS / "s7_studio.key.pem"),
              "--ca", str(CERTS / "s7_root_ca.cert.pem")]


def issue_observer_certificate(directory: Path) -> tuple[Path, Path]:
    """A node certificate for NODE_C from the test CA, as the test node certificates are made."""
    key, csr, cert, ext = (directory / name for name in ("c.key.pem", "c.csr", "c.cert.pem", "c.ext"))
    ext.write_text("basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n"
                   f"extendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=URI:spiffe://s7/node/{NODE_C}\n")
    run = lambda *args: subprocess.run(["openssl", *args], check=True, capture_output=True)  # noqa: E731
    run("req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", str(key),
        "-subj", "/CN=s7-lab-observer", "-out", str(csr))
    # -set_serial: no serial file is written next to the CA.
    run("x509", "-req", "-in", str(csr), "-CA", str(CERTS / "s7_root_ca.cert.pem"), "-CAkey", str(CERTS / "s7_root_ca.key.pem"),
        "-set_serial", str(random.getrandbits(63)), "-days", "2", "-extfile", str(ext), "-out", str(cert))
    return cert, key


def studio_fingerprint() -> str:
    return hashlib.sha256(ssl.PEM_cert_to_DER_cert((CERTS / "s7_studio.cert.pem").read_text())).hexdigest()


def node_config(cert: Path, key: Path, port: int) -> dict:
    return {
        "name": "idle", "elements": [{"id": "idle", "type": "Constant", "params": {"value": 0}}], "connections": [],
        # Loopback only: the test certificates and keys are public, in the repository.
        "websocket": {"server": {"enabled": True, "bind_address": "127.0.0.1", "port": port, "tls": {
            "enabled": True, "cert_file": str(cert), "key_file": str(key),
            "ca_file": str(CERTS / "s7_root_ca.cert.pem"), "require_client_cert": True}}},
        "control": {"studio_key_purpose_oid": OID, "studio_certificate_sha256": studio_fingerprint()},
        "clock": {"assume_synchronized": True},
        "execution": {"step_rate_hz": 50},
    }


def lab_diagram(ports: dict, data_endpoint: str, dt: float) -> dict:
    elements = [
        {"id": "one", "type": "Constant", "params": {"value": 1}, "node": NODE_A},
        {"id": "count", "type": "Accumulator", "params": {}, "node": NODE_A},
        {"id": "out", "type": "Gain", "params": {"factor": 1}, "node": NODE_B},
        {"id": "previous", "type": "Delay", "params": {}, "node": NODE_B},
        {"id": "diff", "type": "Adder", "params": {"weights": [1, -1]}, "node": NODE_B},
        {"id": "unit", "type": "Constant", "params": {"value": 1}, "node": NODE_B},
        {"id": "bad", "type": "Comparator", "params": {"operation": "ne"}, "node": NODE_B},
        {"id": "errors", "type": "Accumulator", "params": {}, "node": NODE_B},
        {"id": "echo", "type": "Gain", "params": {"factor": 1}, "node": NODE_C},
    ]
    # No stream goes back to A: with streams both ways each node's step would wait for the
    # other's (s7l2/docs/OPEN_PROBLEMS.md, X1-X3).
    wires = [("one", "count", 0), ("count", "out", 0), ("out", "previous", 0), ("out", "diff", 0), ("previous", "diff", 1),
             ("diff", "bad", 0), ("unit", "bad", 1), ("bad", "errors", 0), ("errors", "echo", 0)]
    return {
        "document_kind": "s7.diagram",
        "runtime_nodes": [
            {"node_uuid": NODE_A, "name": "producer", "endpoint": f"wss://127.0.0.1:{ports['a']}",
             "data_endpoint": data_endpoint, "data_security": "mtls"},
            {"node_uuid": NODE_B, "name": "consumer", "endpoint": f"wss://127.0.0.1:{ports['b']}"},
            {"node_uuid": NODE_C, "name": "observer", "endpoint": f"wss://127.0.0.1:{ports['c']}"},
        ],
        "schema": {"name": "lab", "elements": elements, "connections": [
            {"from_element": a, "from_port": 0, "to_element": b, "to_port": port} for a, b, port in wires]},
        "dt": dt,
    }


class Lab:
    def __init__(self, args, work: Path):
        self.args, self.work = args, work
        self.runtime = Path(args.build) / "s7runtime" / "s7runtime"
        self.ctl_path = Path(args.build) / "s7ctl" / "s7ctl"
        for binary in (self.runtime, self.ctl_path):
            if not binary.exists():
                raise SystemExit(f"{binary} not found: build s7runtime and s7ctl, or pass --build")
        if "java" in (args.producer, args.consumer) and not (JAVA_BUILD / "s7" / "node" / "DataNode.class").exists():
            raise SystemExit(f"no s7java build in {JAVA_BUILD}: run s7java/build.sh")

    def ctl(self, *args) -> str:
        result = subprocess.run([str(self.ctl_path), *args], capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise RuntimeError(f"s7ctl {' '.join(args[:2])} failed: {result.stdout}{result.stderr}")
        return result.stdout

    def start(self, kind: str, name: str, watch: list, deployed: Path | None = None, activate_at: int | None = None):
        config, state, log = self.work / f"{name}.json", self.work / f"state-{name}", self.work / f"{name}.log"
        if kind == "java":  # data plane only: its part comes from the deployed diagram file
            server = json.loads(config.read_text())["websocket"]["server"]
            tls = server["tls"]
            # The lab's nodes all assume a synchronized clock (clock.assume_synchronized), the Java one too.
            command = [self.args.java, f"-Ds7.tls.resume={'true' if self.args.java_tls_resume else 'false'}",
                       "-Ds7.clock.synchronized=true",
                       "-cp", str(JAVA_BUILD), "s7.node.DataNode", "--diagram", str(deployed),
                       "--cert", tls["cert_file"], "--key", tls["key_file"], "--ca", tls["ca_file"],
                       "--port", str(server["port"]), "--state-dir", str(state), "--status-file", f"{state}.status.json",
                       "--activate-at-ns", str(activate_at)]
        elif kind == "cpp":
            command = [str(self.runtime), "--schema", str(config), "--state-dir", str(state), "--serve-forever"]
            watch = []  # the C++ node has no --watch; its values reach the observer
        else:
            command = [PYTHON, "-m", "s7node", "--schema", str(config), "--state-dir", str(state)]
        for element in watch:
            command += ["--watch", element]
        return subprocess.Popen(command, stdout=log.open("w"), stderr=subprocess.STDOUT,
                                cwd=REPO / "s7py" if kind == "py" else None)

    def stream_status(self, port: int, java_name: str | None = None) -> list[dict]:
        if java_name is not None:
            return json.loads((self.work / f"state-{java_name}.status.json").read_text())["streams"]
        text = self.ctl("streams", f"wss://127.0.0.1:{port}", *STUDIO_TLS)
        return json.loads(text[text.index("{"):])["streams"]

    def last_watch(self, name: str) -> dict | None:
        lines = [line for line in (self.work / f"{name}.log").read_text().splitlines() if line.startswith("watch ")]
        return dict(f.split("=", 1) for f in lines[-1].split()[1:] if "=" in f) if lines else None


def end(statuses: list[dict], role: str) -> dict | None:
    found = [s for s in statuses if s["role"] == role]
    return found[0] if found else None


def wait_port(port: int, seconds: float = 10):
    deadline = time.time() + seconds
    while time.time() < deadline:
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError(f"port {port} did not open")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("producer", choices=("cpp", "py", "java"))
    parser.add_argument("consumer", choices=("cpp", "py", "java"))
    parser.add_argument("--seconds", type=float, default=20, help="run time with drops")
    parser.add_argument("--dt", type=float, default=0.001, help="the diagram's step")
    parser.add_argument("--drop", type=float, default=2.0, help="reset the A -> B data connection every this many seconds (0: never)")
    parser.add_argument("--build", default=os.environ.get("S7_BUILD", str(REPO / "s7l2" / "build")), help="C++ build directory")
    parser.add_argument("--java", default="java", help="java executable, 21 or later")
    parser.add_argument("--java-tls-resume", action="store_true", help="Java nodes resume TLS sessions when they reconnect")
    parser.add_argument("--out", help="also write the result to this file")
    args = parser.parse_args()

    work = Path(tempfile.mkdtemp(prefix="s7-lab-"))
    lab = Lab(args, work)
    base = random.randint(21000, 24000)
    ports = {"a": base, "b": base + 1, "proxy": base + 2, "c": base + 3}
    observer_cert, observer_key = issue_observer_certificate(work)
    for name, cert, key in (("a", CERTS / "s7_runtime_node.cert.pem", CERTS / "s7_runtime_node.key.pem"),
                            ("b", CERTS / "s7_runtime_node_b.cert.pem", CERTS / "s7_runtime_node_b.key.pem"),
                            ("c", observer_cert, observer_key)):
        (work / f"{name}.json").write_text(json.dumps(node_config(cert, key, ports[name])))
    data_endpoint = f"wss://127.0.0.1:{ports['proxy'] if args.drop else ports['a']}/data"
    (work / "diagram.json").write_text(json.dumps(lab_diagram(ports, data_endpoint, args.dt)))
    deployed = work / "deployed.json"
    lab.ctl("check", str(work / "diagram.json"), "--write", str(deployed))  # assigns the streams

    kinds = {"a": args.producer, "b": args.consumer, "c": "py"}
    watches = {"a": ["count"], "b": ["out", "errors"], "c": ["echo"]}
    processes = []
    try:
        for name in ("a", "b", "c"):
            if kinds[name] != "java":
                processes.append(lab.start(kinds[name], name, watches[name]))
        proxy = None
        if args.drop:
            proxy = subprocess.Popen([PYTHON, str(HERE / "chaos_proxy.py"), str(ports["proxy"]), str(ports["a"]),
                                      str(args.drop), str(work / "proxy.log")])
            processes.append(proxy)
        for name in [n for n in ("a", "b", "c") if kinds[n] != "java"] + (["proxy"] if args.drop else []):
            wait_port(ports[name])
        started = time.time()
        controlled = [f"wss://127.0.0.1:{ports[n]}" for n in ("a", "b", "c") if kinds[n] != "java"]
        result = subprocess.run([PYTHON, str(HERE / "deploy_nodes.py"), str(deployed), *controlled],
                                capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise RuntimeError(f"deploy failed: {result.stdout}{result.stderr}")
        activate_at = int(result.stdout.split("activate_at_ns=")[1].split()[0])
        for name in ("a", "b"):
            if kinds[name] == "java":
                processes.append(lab.start("java", name, watches[name], deployed, activate_at))
        time.sleep(args.seconds)
        if proxy:  # stop the drops (the proxy keeps forwarding) and let the stream catch up
            proxy.send_signal(signal.SIGUSR1)
        time.sleep(3)
        # The consumer first: what it delivered was produced by then, so produced - delivered was in flight.
        java = {n: n if kinds[n] == "java" else None for n in ("a", "b")}
        status_b = lab.stream_status(ports["b"], java["b"])
        status_c = lab.stream_status(ports["c"])
        status_a = lab.stream_status(ports["a"], java["a"])
        elapsed = time.time() - started
        # The observer prints its watch once a second. Its echo after n steps is the consumer's
        # error count over the first n values, so wait (bounded) until n covers every value the
        # consumer had received when its status was read.
        consumer_now = end(status_b, "consumer")
        target = consumer_now["elements"] if consumer_now else 0
        deadline = time.time() + 10
        while time.time() < deadline:
            watch = lab.last_watch("c")
            if watch and int(watch["step"]) >= target:
                break
            time.sleep(0.2)
    finally:
        for p in processes:
            if p.poll() is None:
                p.send_signal(signal.SIGINT)
        for p in processes:
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()

    producer, consumer = end(status_a, "producer"), end(status_b, "consumer")
    observer_in, consumer_out = end(status_c, "consumer"), end(status_b, "producer")
    proxy_log = (work / "proxy.log").read_text() if (work / "proxy.log").exists() else ""
    drops = sum(int(f[2]) for f in (line.split() for line in proxy_log.splitlines()) if len(f) > 2 and f[1] == "drop" and f[2].isdigit())
    watch_b, watch_c = lab.last_watch("b"), lab.last_watch("c")
    report = {
        "producer_impl": args.producer, "consumer_impl": args.consumer, "dt": args.dt, "seconds": round(elapsed, 1),
        "drop_period_s": args.drop, "connections_dropped": drops, "java_tls_resume": args.java_tls_resume,
        "producer": producer, "consumer": consumer, "work": str(work),
        "errors_stream": {"producer_fault": consumer_out and consumer_out["fault"],
                          "consumer_fault": observer_in and observer_in["fault"],
                          "delivered": observer_in and observer_in["elements"]},
        # errors: the consumer's steps whose out did not follow the previous one by exactly 1,
        # watched on the consumer where it can (s7py, s7java) and always at the observer.
        "value_check": {"consumer_errors": float(watch_b["errors"]) if watch_b else None,
                        "consumer_steps": int(watch_b["step"]) if watch_b else None,
                        "errors_at_observer": float(watch_c["echo"]) if watch_c else None,
                        # Values 1 .. observer_step are checked; covers_delivered: all that the consumer received.
                        "observer_step": int(watch_c["step"]) if watch_c else None,
                        "covers_delivered": bool(watch_c and consumer and int(watch_c["step"]) >= consumer["elements"])},
    }
    if producer and consumer:
        report["summary"] = {
            "produced": producer["elements"], "delivered": consumer["elements"],
            "in_flight_at_read": producer["elements"] - consumer["elements"],
            "replayed_packets": producer["replayed"], "duplicates_discarded": consumer["duplicates_discarded"],
            "producer_fault": producer["fault"], "consumer_fault": consumer["fault"],
            "latency_p50_ms": consumer["latency"]["p50_ns"] / 1e6, "latency_p99_ms": consumer["latency"]["p99_ns"] / 1e6,
            "latency_max_ms": consumer["latency"]["max_ns"] / 1e6}
    text = json.dumps(report, indent=1)
    print(text)
    if args.out:
        Path(args.out).write_text(text)


if __name__ == "__main__":
    main()
