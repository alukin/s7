#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Three-host S7 WebSocket/mTLS scheduling research, with fresh credentials.

Uses C++ NodeRuntime on A for fixed/projected cases; readiness uses a Python
fixture. B/C always use unchanged s7py data endpoints. No control deployment,
device activation, service/firewall configuration or installed runtime changes.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import ipaddress
import itertools
import json
import os
from pathlib import Path
import random
import select
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import uuid

NODES = ("aaaaaaaa-0000-4000-8000-000000000001", "bbbbbbbb-0000-4000-8000-000000000002",
         "cccccccc-0000-4000-8000-000000000003")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def credentials(stages):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
    now = datetime.now(timezone.utc)
    root_key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "S7 isolated spike " + uuid.uuid4().hex)])
    root = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(root_key.public_key())
        .serial_number(x509.random_serial_number()).not_valid_before(now - timedelta(hours=1))
        .not_valid_after(now + timedelta(days=2)).add_extension(x509.BasicConstraints(ca=True, path_length=0), True)
        .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), False)
        .sign(root_key, hashes.SHA256()))
    manifest = {"ca_sha256": root.fingerprint(hashes.SHA256()).hex(), "nodes": {}}
    for role, node, stage in zip("ABC", NODES, stages):
        key = ec.generate_private_key(ec.SECP256R1())
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "spike-node-" + role)])
        cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - timedelta(hours=1))
            .not_valid_after(now + timedelta(days=1)).add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .add_extension(x509.SubjectAlternativeName([x509.UniformResourceIdentifier("spiffe://s7/node/" + node),
                x509.IPAddress(ipaddress.ip_address("127.0.0.1")), x509.DNSName("localhost")]), False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH, ExtendedKeyUsageOID.CLIENT_AUTH]), False)
            .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), True)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), False)
            .sign(root_key, hashes.SHA256()))
        directory = stage / "credentials"
        directory.mkdir(mode=0o700)
        (directory / "ca.cert.pem").write_bytes(root.public_bytes(serialization.Encoding.PEM))
        (directory / "node.cert.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        private = directory / "node.key.pem"
        # Create with restrictive permissions, rather than chmod after writing.
        fd = os.open(private, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as out:
            out.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                       serialization.NoEncryption()))
        manifest["nodes"][role] = {"node_uuid": node, "certificate_sha256": cert.fingerprint(hashes.SHA256()).hex()}
    # The CA private key is never written or sent to a remote host.
    return manifest


def read_line(child, timeout):
    if not select.select([child.stdout], [], [], timeout)[0]:
        raise TimeoutError("node report timeout")
    line = child.stdout.readline()
    if not line:
        raise RuntimeError("node exited: " + child.stderr.read())
    return json.loads(line)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host-b", required=True)
    p.add_argument("--host-c", required=True)
    p.add_argument("--key-alias-b")
    p.add_argument("--key-alias-c")
    p.add_argument("--python-b", default="python3")
    p.add_argument("--python-c", default="python3")
    p.add_argument("--cpp-node-a", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--smoke", action="store_true", help="two eight-step feedback cases before the full matrix")
    args = p.parse_args()
    if args.output.exists():
        p.error("output already exists")
    cpp = args.cpp_node_a.resolve()
    source = Path(__file__).with_name("cross_node_s7_spike.py").resolve()
    package = source.parents[2] / "s7py" / "s7node"
    if not cpp.is_file() or not package.is_dir():
        p.error("C++ executable and sibling s7py sources are required")
    hosts = [args.host_b, args.host_c]
    if any(h.startswith("-") or any(c.isspace() for c in h) for h in hosts):
        p.error("expected SSH host names")
    aliases = [args.key_alias_b, args.key_alias_c]
    common = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=10"]
    options = [common + (["-o", "HostKeyAlias=" + a] if a else []) for a in aliases]
    python_sources = {f.name: digest(f) for f in package.glob("*.py")}
    manifest = {"cpp_binary_sha256": digest(cpp), "python_fixture_sha256": digest(source),
                "s7py_sha256": python_sources, "orchestrator_sha256": digest(__file__)}
    results, remote_dirs, masters = [], [], []
    error = None
    with tempfile.TemporaryDirectory(prefix="s7-mtls-spikes-") as local:
        stages = [Path(local) / role for role in "ABC"]
        for stage in stages:
            stage.mkdir(mode=0o700)
            shutil.copyfile(source, stage / source.name)
            (stage / "s7node").mkdir()
            for name in python_sources:
                shutil.copyfile(package / name, stage / "s7node" / name)
        manifest["credentials"] = credentials(stages)
        ports = random.SystemRandom().sample(range(22000, 55000), 7)
        a, b, c, forward_b, forward_c, reverse_b, reverse_c = ports
        try:
            for i, host in enumerate(hosts):
                directory = subprocess.check_output(["ssh", *options[i], host,
                    "mktemp -d /tmp/s7-mtls-spikes.XXXXXX"], text=True).strip()
                if not directory.startswith("/tmp/s7-mtls-spikes.") or not directory.replace("/", "").replace("-", "").replace(".", "").isalnum():
                    raise RuntimeError("unexpected remote temporary path")
                remote_dirs.append(directory)
                subprocess.run(["scp", *options[i], "-r", str(stages[i+1]/source.name),
                    str(stages[i+1]/"s7node"), str(stages[i+1]/"credentials"), host+":"+directory+"/"], check=True)
                control = str(Path(local) / f"master-{i}")
                forwards = (["-L", f"127.0.0.1:{forward_b}:127.0.0.1:{b}",
                             "-R", f"127.0.0.1:{reverse_b}:127.0.0.1:{a}"] if i == 0 else
                            ["-L", f"127.0.0.1:{forward_c}:127.0.0.1:{c}",
                             "-R", f"127.0.0.1:{reverse_c}:127.0.0.1:{forward_b}"])
                subprocess.run(["ssh", *options[i], "-M", "-S", control, "-f", "-N",
                    "-o", "ExitOnForwardFailure=yes", *forwards, host], check=True)
                masters.append(control)
            normal = ([('feedback', approach, 1, 1, 0) for approach in ('projected','ready')] if args.smoke else
                      itertools.product(('feedback','independent'), ('fixed','projected','ready'), (1,8), (1,4), (0,5)))
            matrix = [(*case, 'normal') for case in normal]
            if not args.smoke:
                matrix += [('feedback', approach, 1, 1, 0, 'disconnect') for approach in ('projected','ready')]
                matrix += [('feedback', 'projected', 1, 1, 0, 'wrong_peer')]
            steps = 8 if args.smoke else 16
            for topology, approach, window, packet, slow, scenario in matrix:
                children, records = [], []
                run_id = uuid.uuid4().hex
                try:
                    for role, listen, previous in zip("ABC", (a,b,c), (forward_c, reverse_b, reverse_c)):
                        i = "ABC".index(role)-1
                        use_cpp = role == "A" and approach != "ready"
                        script = stages[0]/source.name if role == "A" else Path(remote_dirs[i])/source.name
                        command = ([str(cpp), "--credentials", str(stages[0]/"credentials")] if use_cpp else
                                   [sys.executable if role == "A" else (args.python_b if role == "B" else args.python_c), "-B", str(script)])
                        command += ["--role", role, "--listen", str(listen), "--next", str(previous),
                            "--topology", topology, "--approach", approach, "--window", str(window),
                            "--packet-frames", str(packet), "--slow-ms", str(slow if role == "C" else 0),
                            "--steps", str(steps), "--run-id", run_id, "--wait-start"]
                        if scenario == 'disconnect' and role == 'B':
                            command += ['--disconnect-after','4']
                        if scenario == 'wrong_peer' and role == 'A':
                            command += ['--wrong-peer','1']
                        if role != "A":
                            command = ["ssh", *options[i], "-S", masters[i], hosts[i], shlex.join(["timeout","40s",*command])]
                        children.append(subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                                         stderr=subprocess.PIPE, text=True))
                    for child, role in zip(children,"ABC"):
                        if read_line(child,15) != {"ready":role}:
                            raise RuntimeError("unexpected listener startup")
                    for child in children:
                        child.stdin.write("start\n"); child.stdin.flush()
                    for child, role in zip(children,"ABC"):
                        record = read_line(child,30)
                        records.append(record)
                        expected_hash = manifest["cpp_binary_sha256"] if role=="A" and approach!="ready" else manifest["python_fixture_sha256"]
                        if record["runner_sha256"] != expected_hash:
                            raise RuntimeError("runner hash mismatch")
                        if record["engine"]=="s7py_websocket_mtls" and record["s7py_sha256"]!=python_sources:
                            raise RuntimeError("s7py source mismatch")
                    for child in children:
                        child.stdin.write("stop\n"); child.stdin.flush()
                    for child, record in zip(children,records):
                        out, err = child.communicate(timeout=10)
                        record["stderr"] = err
                        if out.strip():
                            raise RuntimeError("unexpected trailing node output")
                        if scenario=='wrong_peer':
                            if record['completed'] or (record['role']=='A' and not any(
                                'certificate identity mismatch' in text for text in record['transport_errors'])):
                                raise RuntimeError('wrong peer was not rejected: '+json.dumps(record))
                        elif approach=="fixed":
                            if record["completed"] or record["steps"]!=0:
                                raise RuntimeError("baseline did not reproduce initial wait")
                        elif not record["completed"] or record["steps"]!=steps or child.returncode:
                            raise RuntimeError("candidate failed: "+json.dumps(record))
                        if record["transport_errors"] and scenario!='wrong_peer':
                            raise RuntimeError("transport identity/framing error: "+json.dumps(record))
                        if record["sampled_max_send_log"]>window:
                            raise RuntimeError("send-log bound exceeded")
                        if any(s.get('unauthorized',0) or s.get('malformed',0) for s in record['stream_statuses']):
                            raise RuntimeError("unauthorized or malformed traffic")
                    if scenario=='disconnect' and not (records[1]['injected_disconnects']==1 and records[1]['outbound_reconnected']):
                        raise RuntimeError('disconnect/reconnect was not exercised')
                    print(f"{topology}/{approach}/window={window}/packet={packet}/slow_C={slow}/{scenario}: "
                          + ("wrong certificate identity rejected" if scenario=='wrong_peer' else
                             "initial wait reproduced" if approach=="fixed" else f"{steps} exact samples on 3 hosts"),flush=True)
                finally:
                    results.append({"run_id":run_id,"topology":topology,"approach":approach,"window":window,
                                    "packet_frames":packet,"slow_c_ms":slow,"scenario":scenario,"nodes":records})
                    for child in children:
                        if child.poll() is None:
                            child.terminate()
                            try: child.communicate(timeout=3)
                            except subprocess.TimeoutExpired:
                                child.kill(); child.communicate()
        except Exception as failure:
            error = str(failure)
        finally:
            # Remove each node's temporary private key/source payload; preserve
            # the public certificate identities and hashes in the report.
            for i,directory in enumerate(remote_dirs):
                cleanup = shlex.join(["python3","-c","import shutil,sys; shutil.rmtree(sys.argv[1])",directory])
                subprocess.run(["ssh",*options[i],hosts[i],cleanup],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            for i,control in enumerate(masters):
                subprocess.run(["ssh",*options[i],"-S",control,"-O","exit",hosts[i]],
                               stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            report = {"engine":"three_host_s7_websocket_mtls_matrix","error":error,
                "controller":socket.gethostname(),"hosts":{"B":hosts[0],"C":hosts[1]},"manifest":manifest,
                "route":"consumer connections A->C, B->A, C->controller relay->B; loopback SSH forwards; end-to-end mTLS",
                "smoke":args.smoke,"runs":results}
            args.output.parent.mkdir(parents=True,exist_ok=True)
            with args.output.open('x') as output:
                json.dump(report,output,indent=2); output.write('\n')
    if error:
        raise RuntimeError(error)
    print(json.dumps({"configurations":len(results),"node_runs":sum(len(r['nodes']) for r in results),"output":str(args.output)}))


if __name__=="__main__":
    main()
