#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure which audio sample rates a one-way two-node S7 chain sustains (G2).

For each requested rate, starts two fresh local s7runtime nodes, deploys with
s7ctl: two tones on node A -> stream -> 3-band biquad equalizer on node B,
recording B's input and output. Measures real-time step rate, grid lag, stream
latency, CPU and peak RSS while the nodes run, then stops them and checks:
  * B's recorded input equals the tones computed here (no lost or reordered
    sample),
  * B's recorded output equals an independent biquad cascade of that input.

One step is one audio sample (scalar F64 stream); the nodes pace on a shared
UTC grid. This is a capacity measurement on this host, not a guarantee: the
release qualification runs on two physical hosts. Nothing outside a temporary
directory is changed; no audio device is used.

Example:
  python3 tools/measure_g2_audio_rate.py --build build-release \
      --rates 8000 16000 32000 48000 --seconds 20 --output /tmp/g2-rates.jsonl
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import signal
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time

NODE_A = "11111111-2222-3333-4444-555555555555"
NODE_B = "33333333-4444-5555-6666-777777777777"
OID = "2.25.4266950079767543379770189370854073200"
TONES = ((0.30, 220.0), (0.20, 1000.0), (0.15, 0.3))  # (amplitude, frequency as Hz or fraction of rate)


def tones(rate):
    return [(a, f * rate if f < 1 else f) for a, f in TONES]


def biquads(rate):
    """RBJ audio-EQ cookbook: low shelf +6 dB, peak -6 dB, high shelf +4 dB."""
    def shelf(f0, gain_db, high):
        a = 10 ** (gain_db / 40)
        w = 2 * math.pi * f0 / rate
        cw, alpha = math.cos(w), math.sin(w) / 2 * math.sqrt(2)
        s = 2 * math.sqrt(a) * alpha
        sign = -1 if high else 1
        b0 = a * ((a + 1) - sign * (a - 1) * cw + s)
        b1 = sign * 2 * a * ((a - 1) - sign * (a + 1) * cw)
        b2 = a * ((a + 1) - sign * (a - 1) * cw - s)
        a0 = (a + 1) + sign * (a - 1) * cw + s
        a1 = -sign * 2 * ((a - 1) + sign * (a + 1) * cw)
        a2 = (a + 1) + sign * (a - 1) * cw - s
        return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0]

    def peak(f0, gain_db, q):
        a = 10 ** (gain_db / 40)
        w = 2 * math.pi * f0 / rate
        alpha = math.sin(w) / (2 * q)
        a0 = 1 + alpha / a
        return [(1 + alpha * a) / a0, -2 * math.cos(w) / a0, (1 - alpha * a) / a0, -2 * math.cos(w) / a0, (1 - alpha / a) / a0]

    return [shelf(200.0, 6.0, False), peak(1000.0, -6.0, 1.0), shelf(0.3 * rate, 4.0, True)]


def node_config(certs, cert_name, port, studio_fp, security):
    config = {
        "name": "idle",
        "elements": [{"id": "idle", "type": "Constant", "params": {"value": 0}}],
        "connections": [],
        "dt": 1.0,  # the idle startup graph is not stepped (no execution.step_rate_hz)
        "websocket": {"server": {"enabled": True, "port": port, "bind_address": "127.0.0.1", "tls": {
            "enabled": True, "cert_file": str(certs / f"{cert_name}.cert.pem"), "key_file": str(certs / f"{cert_name}.key.pem"),
            "ca_file": str(certs / "s7_root_ca.cert.pem"), "require_client_cert": True}}},
        "control": {"studio_key_purpose_oid": OID, "studio_certificate_sha256": studio_fp},
        "clock": {"assume_synchronized": True},
    }
    if security != "shared":
        config["websocket"]["data_server"] = {"enabled": True, "port": port + 2, "bind_address": "127.0.0.1", "security": security}
    return config


def diagram(rate, ports, security):
    def element(id, kind, node, **params):
        return {"id": id, "type": kind, "node": node, "params": params}

    def wire(src, dst, port=0):
        return {"from_element": src, "from_port": 0, "to_element": dst, "to_port": port}

    elements, connections = [], []
    for i, (amplitude, frequency) in enumerate(tones(rate)):
        elements.append(element(f"tone{i}", "AnalogOscillator", NODE_A, type="F64", amplitude=amplitude, frequency_hz=frequency))
        connections.append(wire(f"tone{i}", "mix", i))
    elements.append(element("mix", "Adder", NODE_A, weights=[1.0] * len(TONES)))
    previous = "mix"
    # Direct form I biquad from causal primitives: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
    for band, (b0, b1, b2, a1, a2) in enumerate(biquads(rate)):
        p = f"eq{band}_"
        elements += [element(p + "sum", "Adder", NODE_B, weights=[b0, b1, b2, -a1, -a2])]
        elements += [element(p + name, "Delay", NODE_B, initial_value=0.0) for name in ("x1", "x2", "y1", "y2")]
        connections += [wire(previous, p + "sum", 0), wire(previous, p + "x1"), wire(p + "x1", p + "x2"),
                        wire(p + "x1", p + "sum", 1), wire(p + "x2", p + "sum", 2),
                        wire(p + "sum", p + "y1"), wire(p + "y1", p + "y2"),
                        wire(p + "y1", p + "sum", 3), wire(p + "y2", p + "sum", 4)]
        previous = p + "sum"
    for name in ("record_in", "record_out"):
        elements.append(element(name, "RecordingSink", NODE_B, type="F64", file_path=f"{name}.s7b", sample_rate_hz=rate,
                                format="compact-binary"))
    # record_in sits on B, so it records the stream as B receives it.
    connections.append(wire("mix", "record_in"))
    connections.append(wire(previous, "record_out"))
    nodes = []
    for name, node, port in (("a", NODE_A, ports[0]), ("b", NODE_B, ports[1])):
        entry = {"node_uuid": node, "name": name, "endpoint": f"wss://127.0.0.1:{port}"}
        if security != "shared":
            entry["data_endpoint"] = ("wss" if security == "mtls" else "ws") + f"://127.0.0.1:{port + 2}"
            entry["data_security"] = security
        nodes.append(entry)
    return {"document_kind": "s7.diagram", "runtime_nodes": nodes,
            "schema": {"name": f"g2_audio_rate_{rate}", "elements": elements, "connections": connections},
            "dt": 1.0 / rate}


def read_recording(path):
    data = path.read_bytes()
    if data[:4] != b"S7B1" or data[4] != 3:
        raise ValueError(f"{path.name}: not a compact binary v3 recording")
    header = struct.unpack_from("<Q", data, 8)[0]
    payload = memoryview(data)[header:]
    if len(payload) % 8:
        raise ValueError(f"{path.name}: truncated sample")
    return struct.unpack(f"<{len(payload) // 8}d", payload)


def check_recordings(work, rate):
    """Independent references; returns (problems, counts)."""
    problems = []
    try:
        x = read_recording(work / "record_in.s7b")
        y = read_recording(work / "record_out.s7b")
    except (OSError, ValueError) as error:
        return [str(error)], {}
    counts = {"input_samples": len(x), "output_samples": len(y)}
    if not x or len(x) != len(y):
        problems.append(f"recording lengths differ or empty: input {len(x)}, output {len(y)}")
    worst_in = 0.0
    increments = [2 * math.pi * f / rate for _, f in tones(rate)]
    for n, value in enumerate(x):
        expected = sum(a * math.sin(n * w) for (a, _), w in zip(tones(rate), increments))
        worst_in = max(worst_in, abs(value - expected))
    # The oscillators accumulate phase in binary64: allow its rounding growth.
    if worst_in > 1e-7:
        problems.append(f"B's input differs from the generated tones by {worst_in:.3g} (lost, repeated or reordered samples?)")
    signal_in = list(x)
    for b0, b1, b2, a1, a2 in biquads(rate):
        out, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
        for v in signal_in:
            r = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            out.append(r)
            x2, x1, y2, y1 = x1, v, y1, r
        signal_in = out
    worst_out = max((abs(a - b) for a, b in zip(signal_in, y)), default=math.inf)
    if worst_out > 1e-9:
        problems.append(f"B's output differs from the reference equalizer by {worst_out:.3g}")
    counts.update(input_max_error=worst_in, output_max_error=worst_out)
    return problems, counts


def cpu_seconds(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def memory_kib(pid):
    values = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, rest = line.partition(":")
        if key in ("VmRSS", "VmHWM"):
            values[key] = int(rest.split()[0])
    return values


def studio_fingerprint(certs):
    der = ssl.PEM_cert_to_DER_cert((certs / "s7_studio.cert.pem").read_text())
    return hashlib.sha256(der).hexdigest()


def free_port_pair():
    for _ in range(100):
        base = random.randrange(20000, 24990, 4)
        try:
            for port in range(base, base + 4):
                with socket.socket() as s:
                    s.bind(("127.0.0.1", port))
            return base, base + 1
        except OSError:
            continue
    raise RuntimeError("no free local ports")


def wait_port(port, process, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"node on port {port} exited with {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"node on port {port} did not listen")


class Run:
    def __init__(self, args, rate, work):
        self.args, self.rate, self.work = args, rate, work
        self.certs = args.source / "s7runtime" / "examples" / "certs"
        self.tls = ["--cert", str(self.certs / "s7_studio.cert.pem"), "--key", str(self.certs / "s7_studio.key.pem"),
                    "--ca", str(self.certs / "s7_root_ca.cert.pem")]
        self.ports = free_port_pair()
        self.processes = []

    def ctl(self, *command):
        result = subprocess.run([str(self.args.ctl), *command, *self.tls], cwd=self.work, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            raise RuntimeError(f"s7ctl {command[0]} failed: {result.stdout}{result.stderr}")
        return result.stdout

    def status(self, port, what):
        return json.loads(self.ctl(what, f"wss://127.0.0.1:{port}"))

    def prefix(self, cpu):
        command = []
        if self.args.cpu_quota:
            command += ["systemd-run", "--user", "--scope", "--quiet", "-p", f"CPUQuota={self.args.cpu_quota}%"]
        if cpu is not None:
            command += ["taskset", "-c", str(cpu)]
        return command

    def start(self):
        fp = studio_fingerprint(self.certs)
        for name, cert, port, cpu in (("a", "s7_runtime_node", self.ports[0], self.args.cpus[0]),
                                      ("b", "s7_runtime_node_b", self.ports[1], self.args.cpus[1])):
            (self.work / f"{name}.json").write_text(json.dumps(node_config(self.certs, cert, port, fp, self.args.data_security)))
            log = open(self.work / f"{name}.log", "w")
            process = subprocess.Popen(self.prefix(cpu) + [str(self.args.runtime), "--schema", str(self.work / f"{name}.json"),
                                       "--state-dir", str(self.work / f"state-{name}"), "--serve-seconds", str(self.args.seconds * 3 + 120)],
                                       cwd=self.work, stdout=log, stderr=subprocess.STDOUT)
            self.processes.append(process)
        for port, process in zip(self.ports, self.processes):
            wait_port(port, process)

    def stop(self):
        # Consumer first: stopping the producer first would underrun node B.
        for process in reversed(self.processes):
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    pass
        for process in self.processes:
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    def sample(self):
        now = time.time_ns()
        info = [self.status(port, "info")["state"]["timing"] for port in self.ports]
        return {"utc_ns": now, "timing": info, "cpu": [cpu_seconds(p.pid) for p in self.processes]}

    def measure(self):
        d = diagram(self.rate, self.ports, self.args.data_security)
        (self.work / "diagram.json").write_text(json.dumps(d))
        self.ctl_no_tls_check()
        planned = json.loads((self.work / "planned.json").read_text())
        for stream in planned["streams"]:
            stream["max_batch_latency_ms"] = self.args.batch_ms
            stream["max_elements_per_packet"] = self.args.packet
        (self.work / "planned.json").write_text(json.dumps(planned))
        deployed = self.ctl("deploy", str(self.work / "planned.json"))
        activate = next(int(line.split()[1]) for line in deployed.splitlines() if line.startswith("activate_at_ns "))
        delay = (activate - time.time_ns()) / 1e9 + self.args.warmup
        time.sleep(max(delay, 0.0))
        first = self.sample()
        time.sleep(self.args.seconds)
        last = self.sample()
        streams = [self.status(port, "streams")["streams"] for port in self.ports]
        memory = [memory_kib(p.pid) for p in self.processes]
        self.stop()
        logs = [(self.work / f"{name}.log").read_text(errors="replace") for name in ("a", "b")]
        problems, counts = check_recordings(self.work, self.rate)
        return self.summarize(first, last, streams, memory, logs, problems, counts, len(planned["streams"]))

    def ctl_no_tls_check(self):
        result = subprocess.run([str(self.args.ctl), "check", str(self.work / "diagram.json"), "--write", str(self.work / "planned.json")],
                                cwd=self.work, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            raise RuntimeError(f"s7ctl check failed: {result.stdout}{result.stderr}")

    def summarize(self, first, last, streams, memory, logs, problems, counts, stream_count):
        wall = (last["utc_ns"] - first["utc_ns"]) / 1e9
        nodes = []
        for i, name in enumerate(("a", "b")):
            t0, t1 = first["timing"][i], last["timing"][i]
            steps = t1["steps"] - t0["steps"]
            nodes.append({
                "node": name, "step_rate_hz": steps / wall, "rate_ratio": steps / wall / self.rate,
                "lag_ms_start": t0["lag_ns"] / 1e6, "lag_ms_end": t1["lag_ns"] / 1e6, "max_lag_ms": t1["max_lag_ns"] / 1e6,
                "rebases": t1.get("rebases", 0), "cpu_percent": 100 * (last["cpu"][i] - first["cpu"][i]) / wall,
                "rss_kib": memory[i].get("VmRSS"), "peak_rss_kib": memory[i].get("VmHWM"),
                "behind_reports": logs[i].count("runtime.execution.behind"),
                "stopped_on_error": "runtime.execution.stopped=error" in logs[i],
            })
        consumer = next((s for s in streams[1] if s.get("role") == "consumer"), {})
        producer = next((s for s in streams[0] if s.get("role") == "producer"), {})
        latency = consumer.get("latency", {})
        if stream_count != 1:
            problems.append(f"expected one stream, planned {stream_count}")
        for side, stream in (("producer", producer), ("consumer", consumer)):
            if stream.get("fault", "missing") != "NONE":
                problems.append(f"{side} stream fault: {stream.get('fault', 'missing')}")
        # Sustained: both nodes keep the nominal rate, their lag does not grow
        # and stays below the bound, no fault, exact recordings.
        for node in nodes:
            if abs(node["rate_ratio"] - 1) > self.args.rate_tolerance:
                problems.append(f"node {node['node']} stepped at {node['step_rate_hz']:.0f} Hz")
            if node["lag_ms_end"] > self.args.max_lag_ms or node["lag_ms_end"] - node["lag_ms_start"] > self.args.max_lag_ms / 2:
                problems.append(f"node {node['node']} lag {node['lag_ms_start']:.1f} -> {node['lag_ms_end']:.1f} ms")
            # A stall longer than this would need that much playback buffer
            # (max_lag counts from activation, so it includes the start).
            if node["max_lag_ms"] > self.args.max_lag_ms:
                problems.append(f"node {node['node']} fell {node['max_lag_ms']:.0f} ms behind its grid")
            if node["stopped_on_error"]:
                problems.append(f"node {node['node']} stopped on an error")
        return {
            "rate_hz": self.rate, "sustained": not problems, "problems": problems, "window_s": wall,
            "data_security": self.args.data_security, "batch_ms": self.args.batch_ms, "packet": self.args.packet,
            "cpu_quota_percent": self.args.cpu_quota, "cpus": self.args.cpus, "nodes": nodes,
            "stream": {"elements_b": consumer.get("elements"), "latency_p50_ms": latency.get("p50_ns", math.nan) / 1e6,
                       "latency_p99_ms": latency.get("p99_ns", math.nan) / 1e6, "latency_max_ms": latency.get("max_ns", math.nan) / 1e6,
                       "producer_fault": producer.get("fault"), "consumer_fault": consumer.get("fault"),
                       "raw_consumer": consumer, "raw_producer": producer},
            **counts,
        }


def host_info():
    model = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                  if line.startswith("model name")), platform.processor())
    return {"host_cpu": model, "logical_cpus": os.cpu_count(), "kernel": platform.release(), "load_average": os.getloadavg()}


def main():
    source = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--build", type=Path, default=source / "build-release", help="build directory with s7runtime and s7ctl")
    p.add_argument("--rates", type=int, nargs="+", default=[8000, 16000, 24000, 32000, 44100, 48000])
    p.add_argument("--seconds", type=float, default=20.0, help="measurement window per rate")
    p.add_argument("--warmup", type=float, default=3.0, help="seconds after activation before measuring")
    p.add_argument("--data-security", choices=("shared", "mtls", "trusted_plaintext"), default="shared",
                   help="shared: data on the mTLS control listener; otherwise a separate data listener")
    p.add_argument("--batch-ms", type=int, default=10, help="stream max_batch_latency_ms")
    p.add_argument("--packet", type=int, default=1024, help="stream max_elements_per_packet")
    p.add_argument("--cpus", type=int, nargs=2, metavar=("A", "B"), help="pin node A and node B to these CPUs")
    p.add_argument("--cpu-quota", type=int, metavar="PERCENT", help="limit each node with systemd-run --user CPUQuota (emulate a slower host)")
    p.add_argument("--rate-tolerance", type=float, default=0.005, help="allowed step-rate deviation (fraction)")
    p.add_argument("--max-lag-ms", type=float, default=100.0, help="bound on the grid lag, at any time since activation")
    p.add_argument("--output", type=Path, help="JSON Lines results (must not exist)")
    p.add_argument("--keep", action="store_true", help="keep each run's directory (logs, diagrams, recordings)")
    p.add_argument("--dry-run", action="store_true", help="print the first rate's diagram and exit")
    args = p.parse_args()
    args.source = source
    args.cpus = args.cpus or [None, None]
    args.runtime, args.ctl = args.build / "s7runtime" / "s7runtime", args.build / "s7ctl" / "s7ctl"
    if any(r < 1000 or r > 400000 for r in args.rates) or not 1 <= args.seconds <= 3600 or args.warmup < 0.5:
        p.error("rates must be 1000..400000 Hz, seconds 1..3600, warmup >= 0.5")
    if args.cpu_quota is not None and not 1 <= args.cpu_quota <= 100:
        p.error("--cpu-quota must be 1..100")
    if args.dry_run:
        print(json.dumps(diagram(args.rates[0], (20000, 20001), args.data_security), indent=1))
        return 0
    if not args.runtime.is_file() or not args.ctl.is_file():
        p.error(f"no s7runtime/s7ctl under {args.build}")
    if args.output and args.output.exists():
        p.error(f"{args.output} exists")
    build_type = next((line.split("=", 1)[1] for line in (args.build / "CMakeCache.txt").read_text().splitlines()
                       if line.startswith("CMAKE_BUILD_TYPE:")), "?") if (args.build / "CMakeCache.txt").exists() else "?"
    revision = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    common = {"revision": revision, "build_type": build_type, **host_info()}
    print(f"{'rate':>7} {'ok':>3} {'A Hz':>8} {'B Hz':>8} {'lag A/B ms':>13} {'maxlag ms':>10} {'p50/p99 ms':>13} {'CPU A/B %':>11} {'RSS B MiB':>9}")
    results, failed = [], 0
    for rate in args.rates:
        work = Path(tempfile.mkdtemp(prefix=f"s7-g2-rate-{rate}-"))
        run = Run(args, rate, work)
        try:
            run.start()
            result = run.measure()
        except Exception as error:  # report and continue with the next rate
            run.stop()
            result = {"rate_hz": rate, "sustained": False, "problems": [f"{type(error).__name__}: {error}"]}
        result.update(common, work=str(work) if args.keep else None)
        results.append(result)
        if args.output:
            with args.output.open("a") as out:
                out.write(json.dumps(result) + "\n")
        if "nodes" in result:
            a, b = result["nodes"]
            s = result["stream"]
            print(f"{rate:>7} {'yes' if result['sustained'] else 'NO':>3} {a['step_rate_hz']:>8.0f} {b['step_rate_hz']:>8.0f} "
                  f"{a['lag_ms_end']:>6.1f}/{b['lag_ms_end']:<6.1f} {max(a['max_lag_ms'], b['max_lag_ms']):>10.1f} "
                  f"{s['latency_p50_ms']:>6.1f}/{s['latency_p99_ms']:<6.1f} {a['cpu_percent']:>5.0f}/{b['cpu_percent']:<5.0f} "
                  f"{(b['peak_rss_kib'] or 0) / 1024:>9.1f}")
        else:
            print(f"{rate:>7}  NO  {result['problems'][0]}")
        for problem in result["problems"]:
            print(f"        - {problem}")
        failed += not result["sustained"]
        if not args.keep:
            subprocess.run(["rm", "-rf", str(work)])
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
