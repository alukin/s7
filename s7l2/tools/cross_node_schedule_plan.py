#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Research-only single-rate scalar precedence planner and CPU benchmark.

Not the S7 compiler or a deployment format. Explicit memory publication cuts
same-step dependencies; memory commits follow the node's causal phase. L1/L3,
L7/L8 and T1/T2 in CROSS_NODE_CAUSALITY_RESEARCH.md motivate these boundaries.
"""
from __future__ import annotations

import argparse
from collections import deque
from dataclasses import asdict, dataclass
import hashlib
import heapq
import json
import math
from pathlib import Path
import platform
import random
import resource
import socket
import statistics
import time
import tracemalloc


@dataclass(frozen=True)
class Actor:
    name: str
    node: str
    kind: str
    inputs: tuple[str, ...] = ()
    parameter: float = 0.0


class InvalidGraph(ValueError):
    pass


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def topological(tasks, *, cross_node=True):
    """Canonical Kahn order: O((V+E) log V), independent of insertion order."""
    degree, followers = {}, {name: [] for name in tasks}
    for name, task in tasks.items():
        parents = task["deps"] if cross_node or task["kind"] != "receive" else []
        degree[name] = len(parents)
        for parent in parents:
            if parent not in tasks:
                raise InvalidGraph("missing task " + parent)
            followers[parent].append(name)
    ready = [name for name, count in degree.items() if count == 0]
    heapq.heapify(ready)
    order = []
    while ready:
        name = heapq.heappop(ready)
        order.append(name)
        for child in followers[name]:
            degree[child] -= 1
            if degree[child] == 0:
                heapq.heappush(ready, child)
    if len(order) != len(tasks):
        # A residual contains cyclic tasks and possibly their descendants; it
        # is deliberately not labelled an exact cycle path/SCC diagnostic.
        raise InvalidGraph("same-step cycle/residual: " + ",".join(sorted(
            name for name, count in degree.items() if count)[:12]))
    return order


def derive(actors):
    actors = list(actors)
    by_name = {a.name: a for a in actors}
    if not actors or len(by_name) != len(actors):
        raise InvalidGraph("empty graph or duplicate actor id")
    peers_by_actor = {a.name: set() for a in actors}
    for a in actors:
        if not a.name or ":" in a.name or "@" in a.name or not a.node:
            raise InvalidGraph("invalid actor id/placement")
        arity = {"constant": 0, "identity": 1, "add_one": 1, "delay": 1,
                 "accumulate": 1}.get(a.kind)
        if arity is None or len(a.inputs) != arity or not math.isfinite(a.parameter):
            raise InvalidGraph("unsupported scalar actor/arity/parameter: " + a.name)
        if any(parent not in by_name for parent in a.inputs):
            raise InvalidGraph("missing input actor: " + a.name)
        for parent in a.inputs:
            if by_name[parent].node != a.node:
                peers_by_actor[parent].add(a.node)
    tasks = {}
    def task(name, node, kind, deps=(), inputs=(), parameter=0.0):
        tasks[name] = {"node": node, "kind": kind, "deps": sorted(set(deps)),
                       "inputs": list(inputs), "parameter": parameter}
    for a in sorted(actors, key=lambda x: x.name):
        inputs = [p if by_name[p].node == a.node else "receive:" + p + "@" + a.node for p in a.inputs]
        task(a.name, a.node, "publish_memory" if a.kind == "delay" else a.kind,
             () if a.kind == "delay" else inputs, () if a.kind == "delay" else inputs, a.parameter)
        peers = sorted(peers_by_actor[a.name])
        if peers:
            task("send:" + a.name, a.node, "send", (a.name,), (a.name,))
            for peer in peers:
                task("receive:" + a.name + "@" + peer, peer, "receive", ("send:" + a.name,))
    for node in sorted({a.node for a in actors if a.kind == "delay"}):
        barrier = "barrier:" + node
        task(barrier, node, "barrier", [name for name, t in tasks.items() if t["node"] == node])
        for a in sorted(actors, key=lambda x: x.name):
            if a.kind == "delay" and a.node == node:
                parent = a.inputs[0]
                binding = parent if by_name[parent].node == node else "receive:" + parent + "@" + node
                task("commit:" + a.name, node, "commit_memory", (barrier, binding), (binding,))
    order = topological(tasks)
    local = topological(tasks, cross_node=False)
    nodes = sorted({a.node for a in actors})
    result = {"schema": "research_scalar_precedence_v1", "tasks": dict(sorted(tasks.items())),
              "global_order": order,
              "projected": {n: [k for k in order if tasks[k]["node"] == n] for n in nodes},
              "local_order": {n: [k for k in local if tasks[k]["node"] == n] for n in nodes}}
    result["sha256"] = digest(result)
    return result


def validate_projection(plan, proposed):
    """Validate an externally proposed per-node order; reject omitted/extra tasks.

    Global DAG plus serial per-node edges must remain acyclic. This establishes
    same-step precedence compatibility, not finite-buffer/multirate liveness.
    """
    if set(proposed) != set(plan["projected"]):
        raise InvalidGraph("node set mismatch")
    tasks = {k: {**t, "deps": list(t["deps"])} for k, t in plan["tasks"].items()}
    for node, order in proposed.items():
        if len(order) != len(set(order)) or set(order) != set(plan["projected"][node]):
            raise InvalidGraph("task membership mismatch on " + node)
        for parent, child in zip(order, order[1:]):
            if parent not in tasks[child]["deps"]:
                tasks[child]["deps"].append(parent)
    topological(tasks)


def fixture(topology):
    if topology == "feedback":
        return [Actor("state", "A", "delay", ("c",)), Actor("a", "A", "identity", ("state",)),
                Actor("b", "B", "add_one", ("a",)), Actor("c", "C", "identity", ("b",))]
    if topology == "independent":
        return [Actor("a", "A", "constant", parameter=2), Actor("b", "B", "constant", parameter=3),
                Actor("c", "C", "constant", parameter=5), Actor("in_a", "A", "identity", ("c",)),
                Actor("in_b", "B", "identity", ("a",)), Actor("in_c", "C", "identity", ("b",))]
    raise InvalidGraph("unknown fixture")


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    def quantile(q):
        return values[max(0, math.ceil(q * len(values)) - 1)]
    return {"n": len(values), "min": values[0], "median": statistics.median(values),
            "p95": quantile(.95), "p99": quantile(.99), "max": values[-1],
            "mean": statistics.mean(values)}


def machine():
    cpu = "unknown"
    try:
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith(('model name', 'Hardware', 'Model')):
                cpu = line.split(':', 1)[1].strip()
                break
    except OSError:
        pass
    return {"hostname": socket.gethostname(), "platform": platform.platform(),
            "python": platform.python_version(), "cpu": cpu,
            "load_average": list(__import__('os').getloadavg()),
            "clock": vars(time.get_clock_info('perf_counter')),
            "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def cpu_execute(tasks, order, approach, steps):
    """Matched pure-Python kernels, all dependencies local; no protocol claim.

    Reverse-topological pending order is an intentional adverse scan control.
    Queue work includes per-step dependency reset; static planning is amortized.
    """
    parents = {k: tasks[k]['deps'] for k in tasks}
    followers = {k: [] for k in tasks}
    for k in tasks:
        for p in parents[k]:
            followers[p].append(k)
    checks = 0
    checksum = 0
    for _ in range(steps):
        values = {}
        if approach == 'projected':
            sequence = iter(order)
        elif approach == 'ready_queue':
            degree = {k: len(parents[k]) for k in tasks}
            queue = deque(k for k in tasks if degree[k] == 0)
        else:
            pending = list(reversed(order))
        for _ in range(len(tasks)):
            if approach == 'projected':
                key = next(sequence)
            elif approach == 'ready_queue':
                key = queue.popleft()
                for child in followers[key]:
                    degree[child] -= 1
                    if degree[child] == 0:
                        queue.append(child)
            else:
                key = None
                for candidate in pending:
                    checks += 1
                    if all(p in values for p in parents[candidate]):
                        key = candidate
                        break
                if key is None:
                    raise AssertionError('ready scan stalled')
                pending.remove(key)
            values[key] = 1 + sum(values[p] for p in parents[key])
        checksum += sum(values.values())
    return checksum, checks


def benchmark(quick=False):
    rows = []
    sizes = (16, 64) if quick else (16, 64, 256, 1024)
    repeats = 2 if quick else 5
    for size in sizes:
        # Broad independent actors contrast with a serial chain's adverse scan.
        for shape in ('chain', 'wide'):
            actors = [Actor(f'x{i:05}', 'A', 'constant' if i == 0 or shape == 'wide' else 'identity',
                            () if i == 0 or shape == 'wide' else (f'x{i-1:05}',)) for i in range(size)]
            derive(actors)  # unmeasured warm-up
            planning = []
            for _ in range(repeats):
                start = time.perf_counter_ns()
                plan = derive(actors)
                validate_projection(plan, plan['projected'])
                planning.append(time.perf_counter_ns() - start)
            # Peak allocation is measured separately so tracemalloc overhead
            # does not contaminate planning or execution timings.
            tracemalloc.start()
            derive(actors)
            _, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()
            task_map, order = plan['tasks'], plan['global_order']
            steps = 4 if quick else 16
            expected = steps * (size * (size + 1) // 2 if shape == 'chain' else size)
            approaches = ['projected', 'ready_scan', 'ready_queue']
            samples = {a: [] for a in approaches}
            for a in approaches:
                cpu_execute(task_map, order, a, 1)
            rng = random.Random(1703 + size)
            for repeat in range(repeats):
                rng.shuffle(approaches)
                for a in approaches:
                    wall, cpu = time.perf_counter_ns(), time.process_time_ns()
                    result, checks = cpu_execute(task_map, order, a, steps)
                    cpu, wall = time.process_time_ns() - cpu, time.perf_counter_ns() - wall
                    assert result == expected, (shape, size, a, result, expected)
                    samples[a].append({'repeat': repeat, 'wall_ns': wall, 'cpu_ns': cpu,
                                       'checks': checks, 'checksum': result,
                                       'dependency_updates': steps * sum(len(t['deps']) for t in task_map.values())
                                       if a == 'ready_queue' else 0})
            rows.append({'actors': size, 'shape': shape, 'steps': steps,
                         'planning_validate_ns': planning, 'planning_peak_bytes': peak,
                         'plan_sha256': plan['sha256'], 'execution': samples})
    admission = []
    for topology in ('feedback', 'independent', 'delay_free', 'direct_state_cycle'):
        actors = (fixture(topology) if topology in ('feedback', 'independent') else
                  [Actor('a', 'A', 'accumulate' if topology == 'direct_state_cycle' else 'identity', ('b',)),
                   Actor('b', 'B', 'identity', ('a',))])
        trials = []
        for _ in range(repeats):
            start = time.perf_counter_ns()
            try:
                plan = derive(actors)
                validate_projection(plan, plan['projected'])
                outcome = 'accepted'
            except InvalidGraph:
                outcome = 'rejected'
            trials.append(time.perf_counter_ns() - start)
        assert (outcome == 'accepted') == (topology in ('feedback', 'independent'))
        admission.append({'case': topology, 'outcome': outcome, 'elapsed_ns': trials})
    return {'schema': 's7_schedule_cpu_research_v1', 'machine': machine(), 'rows': rows, 'admission': admission,
            'limits': 'Python kernel, no NodeRuntime/transport; adverse scan order is intentional.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--benchmark', action='store_true')
    p.add_argument('--quick', action='store_true')
    p.add_argument('--topology', choices=('feedback', 'independent'), default='feedback')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = benchmark(args.quick) if args.benchmark else derive(fixture(args.topology))
    content = json.dumps(result, indent=2, allow_nan=False) + '\n'
    if args.output:
        with args.output.open('x') as out:
            out.write(content)
    else:
        print(content, end='')


if __name__ == '__main__':
    main()
