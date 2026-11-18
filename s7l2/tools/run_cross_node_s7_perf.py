#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Three-host S7 WebSocket/mTLS scheduling research, with fresh credentials.

Uses the same Python engine on all roles for matched scheduler comparisons.
Schedules are derived from the same scalar actor graph on every host. No control deployment,
device activation, service/firewall configuration or installed runtime changes.
"""
import argparse
import json
from pathlib import Path
import random
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from cross_node_schedule_plan import derive, fixture
from run_cross_node_s7_spikes import credentials, digest, read_line


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host-b", required=True)
    p.add_argument("--host-c", required=True)
    p.add_argument("--key-alias-b")
    p.add_argument("--key-alias-c")
    p.add_argument("--python-b", default="python3")
    p.add_argument("--python-c", default="python3")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--smoke", action="store_true")
    p.add_argument("--steps", type=int, default=128)
    p.add_argument("--warmup", type=int, default=16)
    p.add_argument("--repetitions", type=int, default=3)
    p.add_argument("--seed", type=int, default=1703)
    args = p.parse_args()
    if args.output.exists():
        p.error("output already exists")
    source = Path(__file__).with_name("cross_node_s7_perf_node.py").resolve()
    package = source.parents[2] / "s7py" / "s7node"
    if not package.is_dir():
        p.error("sibling s7py sources are required")
    if not 16 <= args.steps <= 4096 or not 0 <= args.warmup <= 256 or not 2 <= args.repetitions <= 10:
        p.error("invalid steps/warmup/repetitions")
    planner = source.with_name('cross_node_schedule_plan.py')
    plans = {topology: derive(fixture(topology)) for topology in ('feedback','independent')}
    hosts = [args.host_b, args.host_c]
    if any(h.startswith("-") or any(c.isspace() for c in h) for h in hosts):
        p.error("expected SSH host names")
    aliases = [args.key_alias_b, args.key_alias_c]
    common = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=10"]
    options = [common + (["-o", "HostKeyAlias=" + a] if a else []) for a in aliases]
    python_sources = {f.name: digest(f) for f in package.glob("*.py")}
    manifest = {"python_fixture_sha256": digest(source), "planner_sha256": digest(planner),
                "credential_helper_sha256": digest(Path(__file__).with_name("run_cross_node_s7_spikes.py")),
                "s7py_sha256": python_sources, "orchestrator_sha256": digest(__file__)}
    results, remote_dirs, masters, cpu_results = [], [], [], []
    cleanup_verified = {}
    error = None
    with tempfile.TemporaryDirectory(prefix="s7-perf-spikes-") as local:
        stages = [Path(local) / role for role in "ABC"]
        for stage in stages:
            stage.mkdir(mode=0o700)
            shutil.copyfile(source, stage / source.name)
            shutil.copyfile(planner, stage / planner.name)
            (stage / "s7node").mkdir()
            for name in python_sources:
                shutil.copyfile(package / name, stage / "s7node" / name)
        manifest["credentials"] = credentials(stages)
        ports = random.SystemRandom().sample(range(22000, 55000), 7)
        a, b, c, forward_b, forward_c, reverse_b, reverse_c = ports
        try:
            for i, host in enumerate(hosts):
                directory = subprocess.check_output(["ssh", *options[i], host,
                    "mktemp -d /tmp/s7-perf-spikes.XXXXXX"], text=True).strip()
                if not directory.startswith("/tmp/s7-perf-spikes.") or not directory.replace("/", "").replace("-", "").replace(".", "").isalnum():
                    raise RuntimeError("unexpected remote temporary path")
                remote_dirs.append(directory)
                subprocess.run(["scp", *options[i], "-r", str(stages[i+1]/source.name),
                    str(stages[i+1]/planner.name), str(stages[i+1]/"s7node"), str(stages[i+1]/"credentials"), host+":"+directory+"/"], check=True)
                control = str(Path(local) / f"master-{i}")
                forwards = (["-L", f"127.0.0.1:{forward_b}:127.0.0.1:{b}",
                             "-R", f"127.0.0.1:{reverse_b}:127.0.0.1:{a}"] if i == 0 else
                            ["-L", f"127.0.0.1:{forward_c}:127.0.0.1:{c}",
                             "-R", f"127.0.0.1:{reverse_c}:127.0.0.1:{forward_b}"])
                subprocess.run(["ssh", *options[i], "-M", "-S", control, "-f", "-N",
                    "-o", "ExitOnForwardFailure=yes", *forwards, host], check=True)
                masters.append(control)
            def cpu_bench(role):
                i = 'ABC'.index(role)-1
                script = stages[0]/planner.name if role == 'A' else Path(remote_dirs[i])/planner.name
                command = [sys.executable if role == 'A' else (args.python_b if role == 'B' else args.python_c),
                           '-B', str(script), '--benchmark'] + (['--quick'] if args.smoke else [])
                if role != 'A':
                    command = ['ssh', *options[i], '-S', masters[i], hosts[i], shlex.join(['timeout','180s',*command])]
                report = json.loads(subprocess.check_output(command, text=True, timeout=200))
                if report['machine']['runner_sha256'] != manifest['planner_sha256']:
                    raise RuntimeError('CPU benchmark source mismatch')
                return {'role':role, **report}
            with ThreadPoolExecutor(max_workers=3) as pool:
                cpu_results = list(pool.map(cpu_bench, 'ABC'))
            profiles = [(1,1,5,0), (8,1,5,0), (64,1,5,0), (8,4,5,0),
                        (64,4,5,0), (8,4,1,0), (8,4,5,2)]
            rng = random.Random(args.seed)
            blocks = [('feedback',profiles[0],0), ('independent',profiles[3],0)] if args.smoke else [
                (topology,profile,repeat) for topology in plans for profile in profiles
                for repeat in range(args.repetitions)]
            rng.shuffle(blocks)
            matrix = []
            for topology, profile, repeat in blocks:
                approaches = ['projected','ready']
                rng.shuffle(approaches)
                for approach in approaches:
                    matrix.append((topology,approach,*profile,repeat,'normal'))
            if not args.smoke:
                matrix += [(topology,'fixed',1,1,5,0,0,'initial_wait') for topology in plans]
                matrix += [('feedback',approach,1,1,5,0,0,'disconnect') for approach in ('projected','ready')]
            for topology, approach, window, packet, batch_ms, slow, repeat, scenario in matrix:
                steps = 16 if args.smoke else args.steps
                warmup = 4 if args.smoke else args.warmup
                if approach == 'fixed':
                    steps, warmup = 1, 0
                children, records = [], []
                run_id = uuid.uuid4().hex
                case_started = time.perf_counter_ns()
                try:
                    for role, listen, previous in zip("ABC", (a,b,c), (forward_c, reverse_b, reverse_c)):
                        i = "ABC".index(role)-1
                        script = stages[0]/source.name if role == "A" else Path(remote_dirs[i])/source.name
                        command = [sys.executable if role == 'A' else (args.python_b if role == 'B' else args.python_c),
                                   '-B', str(script)]
                        command += ["--role", role, "--listen", str(listen), "--next", str(previous),
                            "--topology", topology, "--approach", approach, "--window", str(window),
                            "--packet-frames", str(packet), "--slow-ms", str(slow if role == "C" else 0),
                            "--steps", str(steps), "--warmup", str(warmup), "--batch-ms", str(batch_ms),
                            "--plan-sha256", plans[topology]["sha256"], "--run-id", run_id, "--wait-start"]
                        if scenario == 'disconnect' and role == 'B':
                            command += ['--disconnect-after',str(warmup+4)]
                        if role != "A":
                            command = ["ssh", *options[i], "-S", masters[i], hosts[i], shlex.join(["timeout","120s",*command])]
                        children.append(subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                                         stderr=subprocess.PIPE, text=True))
                    for child, role in zip(children,"ABC"):
                        if read_line(child,15) != {"ready":role}:
                            raise RuntimeError("unexpected listener startup")
                    for child in children:
                        child.stdin.write("start\n"); child.stdin.flush()
                    for child, role in zip(children,"ABC"):
                        record = read_line(child,100)
                        records.append(record)
                        expected_hash = manifest["python_fixture_sha256"]
                        if record["runner_sha256"] != expected_hash:
                            raise RuntimeError("runner hash mismatch")
                        if record['planner_sha256'] != manifest['planner_sha256'] or record['plan_sha256'] != plans[topology]['sha256']:
                            raise RuntimeError('planner hash mismatch')
                        if record['certificate_sha256'] != manifest['credentials']['nodes'][role]['certificate_sha256']:
                            raise RuntimeError('certificate provenance mismatch')
                        if record["s7py_sha256"]!=python_sources:
                            raise RuntimeError("s7py source mismatch")
                    for child in children:
                        child.stdin.write("stop\n"); child.stdin.flush()
                    for child, record in zip(children,records):
                        out, err = child.communicate(timeout=10)
                        record["stderr"] = err
                        if out.strip():
                            raise RuntimeError("unexpected trailing node output")
                        if approach=="fixed":
                            if record["completed"] or record["steps"]!=0:
                                raise RuntimeError("baseline did not reproduce initial wait")
                        elif not record["completed"] or record["steps"]!=steps+warmup or child.returncode:
                            raise RuntimeError("candidate failed: "+json.dumps(record))
                        if approach != 'fixed' and (record['performance']['measured_steps'] != steps or
                                                   any(s['fault'] != 'NONE' for s in record['stream_statuses'])):
                            raise RuntimeError('measurement incomplete or stream faulted')
                        if record["transport_errors"]:
                            raise RuntimeError("transport identity/framing error: "+json.dumps(record))
                        if record["sampled_max_send_log"]>window:
                            raise RuntimeError("send-log bound exceeded")
                        if any(s.get('unauthorized',0) or s.get('malformed',0) for s in record['stream_statuses']):
                            raise RuntimeError("unauthorized or malformed traffic")
                    if scenario=='disconnect' and not (records[1]['injected_disconnects']==1 and records[1]['outbound_reconnected']):
                        raise RuntimeError('disconnect/reconnect was not exercised')
                    print(f"{len(results)+1}/{len(matrix)} {topology}/{approach}/window={window}/packet={packet}/batch={batch_ms}/slow_C={slow}/repeat={repeat}/{scenario}: "
                          + ("initial wait reproduced" if approach=="fixed" else f"{steps} measured samples, min owner rate={min(r['performance']['steps_per_second'] for r in records):.1f}/s"),flush=True)
                finally:
                    results.append({"run_id":run_id,"topology":topology,"approach":approach,"window":window,
                                    "packet_frames":packet,"batch_ms":batch_ms,"slow_c_ms":slow,"repeat":repeat,
                                    "measured_target":steps,"warmup":warmup,"scenario":scenario,"nodes":records,
                                    "controller_case_wall_ns":time.perf_counter_ns()-case_started})
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
                cleaned = subprocess.run(["ssh",*options[i],hosts[i],cleanup],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                cleanup_verified[hosts[i]] = cleaned.returncode == 0
            for i,control in enumerate(masters):
                subprocess.run(["ssh",*options[i],"-S",control,"-O","exit",hosts[i]],
                               stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            report = {"engine":"three_host_s7_performance_matrix","error":error,
                "controller":socket.gethostname(),"hosts":{"B":hosts[0],"C":hosts[1]},"manifest":manifest,
                "route":"consumer connections A->C, B->A, C->controller relay->B; loopback SSH forwards; end-to-end mTLS",
                "smoke":args.smoke,"repetitions":args.repetitions,"random_seed":args.seed,
                "plans":plans,"cpu_benchmarks":cpu_results,"cleanup_verified":cleanup_verified,"runs":results}
            args.output.parent.mkdir(parents=True,exist_ok=True)
            with args.output.open('x') as output:
                json.dump(report,output,indent=2); output.write('\n')
    if error:
        raise RuntimeError(error)
    print(json.dumps({"configurations":len(results),"node_runs":sum(len(r['nodes']) for r in results),"output":str(args.output)}))


if __name__=="__main__":
    main()
