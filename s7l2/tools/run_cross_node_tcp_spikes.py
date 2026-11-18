#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the isolated TCP fixture on this host and two SSH hosts.

All listeners/forwards bind loopback; no firewall/service settings are changed.
B -> C is relayed through this host's SSH forwards. Timing includes that route.
See docs/CROSS_NODE_CAUSALITY_RESEARCH.md; this is not S7 wire qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import select
import shlex
import socket
import subprocess
import sys
import tempfile
import uuid


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host-b", required=True)
    p.add_argument("--host-c", required=True)
    p.add_argument("--key-alias-b")
    p.add_argument("--key-alias-c")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error("output already exists")
    source = Path(__file__).with_name("cross_node_tcp_spike.py").resolve()
    expected_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    hosts = [args.host_b, args.host_c]
    if any(h.startswith("-") or any(c.isspace() for c in h) for h in hosts):
        p.error("expected SSH host names")
    aliases = [args.key_alias_b, args.key_alias_c]
    results, masters, remote_dirs = [], [], []
    failure = None
    with tempfile.TemporaryDirectory(prefix="s7-crossnode-orchestrator-") as local:
        common = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=10"]
        options = [common + (["-o", "HostKeyAlias=" + a] if a else []) for a in aliases]
        # Distinct loopback endpoints; SSH ExitOnForwardFailure detects bind
        # collisions for all forwarded ports. Node bind failures are fatal.
        ports = random.SystemRandom().sample(range(22000, 55000), 7)
        listen_a, listen_b, listen_c, local_b, local_c, remote_b, remote_c = ports
        try:
            for index, host in enumerate(hosts):
                directory = subprocess.check_output(["ssh", *options[index], host,
                    "mktemp -d /tmp/s7-crossnode-tcp.XXXXXX"], text=True).strip()
                if not directory.startswith("/tmp/s7-crossnode-tcp.") or not directory.replace("/", "").replace("-", "").replace(".", "").isalnum():
                    raise RuntimeError("unexpected temporary path")
                remote_dirs.append(directory)
                subprocess.run(["scp", *options[index], str(source), host + ":" + directory + "/"], check=True)
            forwards = [
                ["-L", f"127.0.0.1:{local_b}:127.0.0.1:{listen_b}",
                 "-R", f"127.0.0.1:{remote_b}:127.0.0.1:{local_c}"],
                ["-L", f"127.0.0.1:{local_c}:127.0.0.1:{listen_c}",
                 "-R", f"127.0.0.1:{remote_c}:127.0.0.1:{listen_a}"]]
            for i, host in enumerate(hosts):
                control = str(Path(local) / f"master-{i}")
                subprocess.run(["ssh", *options[i], "-M", "-S", control, "-f", "-N",
                                "-o", "ExitOnForwardFailure=yes", *forwards[i], host], check=True)
                masters.append(control)
            for topology in ("feedback", "independent"):
                for approach in ("fixed", "projected", "ready"):
                    for window in (1, 8):
                        for slow in (0, 5):
                            run_id = uuid.uuid4().hex
                            children = []
                            try:
                                for role, listen, following in zip("ABC", (listen_a, listen_b, listen_c),
                                                                   (local_b, remote_b, remote_c)):
                                    i = "ABC".index(role) - 1
                                    script = str(source) if role == "A" else remote_dirs[i] + "/" + source.name
                                    command = ["python3", "-B", script, "--role", role,
                                               "--listen", str(listen), "--next", str(following),
                                               "--topology", topology, "--approach", approach,
                                               "--window", str(window), "--slow-ms", str(slow if role == "C" else 0),
                                               "--steps", "32", "--run-id", run_id, "--wait-start"]
                                    if role != "A":
                                        command = ["ssh", *options[i], "-S", masters[i], hosts[i],
                                                   shlex.join(["timeout", "25s", *command])]
                                    children.append(subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
                                for child, role in zip(children, "ABC"):
                                    if not select.select([child.stdout], [], [], 15)[0]:
                                        raise TimeoutError("listener startup")
                                    line = child.stdout.readline()
                                    if json.loads(line) != {"ready": role}:
                                        raise RuntimeError("unexpected startup: " + line)
                                for child in children:
                                    child.stdin.write("start\n")
                                    child.stdin.flush()
                                records = []
                                for child in children:
                                    out, err = child.communicate(timeout=20)
                                    record = json.loads(out.strip())
                                    if record["runner_sha256"] != expected_hash:
                                        raise RuntimeError("source hash mismatch")
                                    if record["max_receive_queue"] > window or record["max_unacknowledged"] > window:
                                        raise RuntimeError("capacity exceeded")
                                    if approach == "fixed":
                                        if record["completed"] or record["steps"] != 0:
                                            raise RuntimeError("baseline failed to reproduce initial wait")
                                    elif not record["completed"] or record["steps"] != 32 or child.returncode:
                                        raise RuntimeError("candidate failed: " + json.dumps(record) + err)
                                    record["ssh_stderr"] = err
                                    records.append(record)
                                results.append({"run_id": run_id, "topology": topology, "approach": approach,
                                                "window": window, "slow_c_ms": slow, "nodes": records})
                                print(f"{topology} / {approach} / window={window} / slow_C={slow} ms: "
                                      + ("initial wait reproduced" if approach == "fixed" else "32 steps matched on all hosts"),
                                      flush=True)
                            finally:
                                for child in children:
                                    if child.poll() is None:
                                        child.terminate()
                                        try:
                                            child.communicate(timeout=3)
                                        except subprocess.TimeoutExpired:
                                            child.kill()
                                            child.communicate()
        except Exception as error:
            failure = str(error)
        finally:
            for i, control in enumerate(masters):
                subprocess.run(["ssh", *options[i], "-S", control, "-O", "exit", hosts[i]],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            report = {"engine": "three_host_ssh_tcp_matrix", "error": failure,
                      "controller": socket.gethostname(), "source_sha256": expected_hash,
                      "orchestrator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      "host_b": hosts[0], "host_c": hosts[1], "remote_temporary_dirs": remote_dirs,
                      "route": "A->B; B->controller relay->C; C->A; loopback TCP through SSH",
                      "runs": results}
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open("x") as output:
                json.dump(report, output, indent=2)
                output.write("\n")
    if failure:
        raise RuntimeError(failure)
    print(json.dumps({"runs": len(results), "node_runs": sum(len(r["nodes"]) for r in results),
                      "output": str(args.output)}))


if __name__ == "__main__":
    main()
