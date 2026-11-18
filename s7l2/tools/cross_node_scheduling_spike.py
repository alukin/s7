#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in single-rate scheduling experiment; never imported by the runtime.

See docs/CROSS_NODE_CAUSALITY_RESEARCH.md for sources and restrictions. Queues
contain scalar (logical index, value) tokens; consumption frees capacity
immediately. This is not an implementation of S7 packet/ACK/replay protocols.
"""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass, field, replace
import hashlib
import json
from pathlib import Path
import platform
import random
import subprocess
import sys


@dataclass(frozen=True)
class Actor:
    name: str
    node: str
    kind: str
    inputs: tuple[str, ...] = ()
    parameter: float = 1.0


@dataclass
class Case:
    name: str
    actors: list[Actor]
    expected: dict[str, list[float]]
    valid: bool = True
    # Explicit extra model delay, used only as a changed-semantics control.
    initial_channels: dict[tuple[str, str], float] = field(default_factory=dict)


@dataclass(frozen=True)
class Task:
    key: str
    node: str
    kind: str
    inputs: tuple[str, ...] = ()
    actor: Actor | None = None
    channels: tuple[tuple[str, str], ...] = ()


def cases(steps: int) -> list[Case]:
    result = [Case("feedforward", [Actor("c", "A", "constant", parameter=2),
                                  Actor("g", "B", "gain", ("c",), 3),
                                  Actor("h", "C", "gain", ("g",), 2)],
                   {"h": [12.0] * steps})]
    for owner, other in (("A", "B"), ("B", "A")):
        actors = [Actor("d", owner, "delay", ("sum",), 0),
                  Actor("g", owner, "gain", ("d",), 1),
                  Actor("one", other, "constant", parameter=1),
                  Actor("sum", other, "add", ("g", "one"))]
        result.append(Case(f"feedback_memory_{owner}", actors,
                           {"sum": [float(n + 1) for n in range(steps)]}))
    nested = result[1]
    result.append(Case("flattened_memory_path", [
        replace(a, name="box/state" if a.name == "d" else a.name,
                inputs=tuple("box/state" if x == "d" else x for x in a.inputs))
        for a in nested.actors], dict(nested.expected)))
    result.append(Case("independent_bidirectional", [
        Actor("a", "A", "constant", parameter=2),
        Actor("b", "B", "constant", parameter=3),
        Actor("on_a", "A", "gain", ("b",), 5),
        Actor("on_b", "B", "gain", ("a",), 7)],
        {"on_a": [15.0] * steps, "on_b": [14.0] * steps}))
    result.append(Case("feedback_fanout", result[1].actors + [
        Actor("monitor", "C", "gain", ("g",), 2)],
        {**result[1].expected, "monitor": [float(2 * n) for n in range(steps)]}))
    result.append(Case("delay_free_cycle", [
        Actor("a", "A", "gain", ("b",)), Actor("b", "B", "gain", ("a",))], {}, False))
    result.append(Case("stateful_direct_is_not_memory", [
        Actor("a", "A", "accumulator", ("b",)), Actor("b", "B", "gain", ("a",))], {}, False))
    result.append(Case("memory_elsewhere_in_scc", [
        Actor("a", "A", "add", ("b", "d")), Actor("b", "B", "gain", ("a",)),
        Actor("d", "A", "delay", ("b",), 0)], {}, False))
    result.append(Case("extra_stream_delay_changes_model", result[1].actors,
                       {"sum": [float(n // 2 + 1) for n in range(steps)]},
                       initial_channels={("sum", "A"): 0.0}))
    return result


def make_tasks(actors: list[Actor]) -> dict[str, Task]:
    by_name = {a.name: a for a in actors}
    tasks = {}
    for a in actors:
        if a.kind != "delay":
            inputs = tuple(x if by_name[x].node == a.node else f"receive:{x}@{a.node}"
                           for x in a.inputs)
            tasks[a.name] = Task(a.name, a.node, a.kind, inputs, a)
    # Match the shape of extraction: original elements, then stream ends
    # in producer-output order. Global grouping is only a research model.
    for a in sorted(actors, key=lambda a: a.name):
        peers = sorted({b.node for b in actors if a.name in b.inputs and b.node != a.node})
        if not peers:
            continue
        channels = tuple((a.name, peer) for peer in peers)
        key = f"send:{a.name}"
        tasks[key] = Task(key, a.node, "send", (a.name,), channels=channels)
        for channel in channels:
            key = f"receive:{a.name}@{channel[1]}"
            tasks[key] = Task(key, channel[1], "receive", channels=(channel,))
    return tasks


def topological_order(tasks: dict[str, Task], global_order: bool) -> list[str] | None:
    dependencies = {key: {x for x in t.inputs if x in tasks} for key, t in tasks.items()}
    for key, task in tasks.items():
        if global_order and task.kind == "receive":
            # An explicitly initialized stream has a previous-step dependency;
            # handled by caller by removing this edge for that control only.
            dependencies[key].add(f"send:{task.channels[0][0]}")
    return order_from_dependencies(dependencies)


def order_from_dependencies(dependencies: dict[str, set[str]]) -> list[str] | None:
    followers: dict[str, list[str]] = {k: [] for k in dependencies}
    degree = {k: len(v) for k, v in dependencies.items()}
    for key, parents in dependencies.items():
        for parent in parents:
            followers[parent].append(key)
    ready = deque(k for k in dependencies if degree[k] == 0)
    order = []
    while ready:
        key = ready.popleft()
        order.append(key)
        for follower in followers[key]:
            degree[follower] -= 1
            if degree[follower] == 0:
                ready.append(follower)
    return order if len(order) == len(dependencies) else None


def simulate(case: Case, approach: str, steps: int, capacity: int,
             slow_period: int, permutation: int) -> dict:
    actors = list(case.actors)
    random.Random(permutation).shuffle(actors)
    tasks = make_tasks(actors)
    dependencies = {key: {x for x in t.inputs if x in tasks} for key, t in tasks.items()}
    for key, task in tasks.items():
        if task.kind == "receive" and task.channels[0] not in case.initial_channels:
            dependencies[key].add(f"send:{task.channels[0][0]}")
    global_order = order_from_dependencies(dependencies)
    semantic_valid = global_order is not None
    assert semantic_valid == case.valid, f"wrong classification: {case.name}"
    if approach == "projected" and global_order is None:
        return {"outcome": "rejected_cycle", "semantic_valid": False,
                "steps": {}, "operations": 0, "readiness_checks": 0, "max_queue": 0}
    if approach == "projected":
        order = global_order
    else:
        order = topological_order(tasks, False)
    if order is None:
        return {"outcome": "rejected_local_cycle", "semantic_valid": semantic_valid,
                "steps": {}, "operations": 0, "readiness_checks": 0, "max_queue": 0}
    nodes = sorted({a.node for a in actors})
    local = {n: [k for k in order if tasks[k].node == n] for n in nodes}
    counts = {n: 0 for n in nodes}
    done: dict[str, set[str]] = {n: set() for n in nodes}
    values: dict[str, dict[str, float]] = {n: {} for n in nodes}
    memory = {a.name: a.parameter for a in actors if a.kind == "delay"}
    direct_state = {a.name: 0.0 for a in actors if a.kind == "accumulator"}
    queues = {c: deque() for t in tasks.values() if t.kind == "send" for c in t.channels}
    for channel, value in case.initial_channels.items():
        queues[channel].append((0, value))
    trace: deque[dict] = deque(maxlen=24)
    observed = {key: [] for key in case.expected}
    high_water = max((len(q) for q in queues.values()), default=0)
    operations = checks = 0

    def begin(node):
        values[node] = {a.name: memory[a.name] for a in actors
                        if a.node == node and a.kind == "delay"}
        done[node] = set()

    def ready(key):
        nonlocal checks
        checks += 1
        task = tasks[key]
        if not all(x in values[task.node] for x in task.inputs):
            return False
        if task.kind == "receive":
            return bool(queues[task.channels[0]])
        if task.kind == "send":
            return all(len(queues[c]) < capacity for c in task.channels)
        return True

    def choose(node):
        pending = [k for k in local[node] if k not in done[node]]
        if approach != "ready":
            pending = pending[:1]
        return next((k for k in pending if ready(k)), None)

    for n in nodes:
        begin(n)
    outcome = "iteration_limit"
    # Speeds are deterministic scheduling opportunities, not wall-clock rates.
    for sweep in range(steps * max(1, len(tasks)) * slow_period * 4):
        moved = False
        for node in nodes:
            if counts[node] == steps or (node == nodes[-1] and sweep % slow_period):
                continue
            key = choose(node)
            if key is None:
                continue
            task = tasks[key]
            vals = values[node]
            if task.kind == "send":
                for channel in task.channels:
                    index = counts[node] + int(channel in case.initial_channels)
                    queues[channel].append((index, vals[task.inputs[0]]))
            elif task.kind == "receive":
                index, value = queues[task.channels[0]].popleft()
                if index != counts[node]:
                    raise AssertionError(f"sequence mismatch {case.name}: {index} vs {counts[node]}")
                vals[key] = value
            elif task.kind == "constant":
                vals[key] = task.actor.parameter
            elif task.kind == "gain":
                vals[key] = vals[task.inputs[0]] * task.actor.parameter
            elif task.kind == "add":
                vals[key] = sum(vals[x] for x in task.inputs)
            elif task.kind == "accumulator":
                direct_state[key] += vals[task.inputs[0]]
                vals[key] = direct_state[key]
            else:
                raise AssertionError(task.kind)
            trace.append({"node": node, "step": counts[node], "task": key})
            operations += 1
            high_water = max(high_water, max((len(q) for q in queues.values()), default=0))
            done[node].add(key)
            moved = True
            if len(done[node]) == len(local[node]):
                for name in observed:
                    if name in vals:
                        observed[name].append(vals[name])
                # Simultaneous commit; memory outputs stay published throughout
                # this node's causal phase. Explicitly exclude general solvers.
                staged = {}
                for a in actors:
                    if a.node == node and a.kind == "delay":
                        source = a.inputs[0]
                        source_actor = next(b for b in actors if b.name == source)
                        binding = source if source_actor.node == node else f"receive:{source}@{node}"
                        staged[a.name] = vals[binding]
                memory.update(staged)
                counts[node] += 1
                begin(node)
        if all(n == steps for n in counts.values()):
            outcome = "completed"
            break
        if not moved and not any(counts[n] < steps and choose(n) is not None for n in nodes):
            outcome = "deadlock"
            break
    mismatches = [name for name, reference in case.expected.items()
                  if observed[name] != reference[:len(observed[name])]]
    if mismatches:
        outcome = "output_mismatch"
    result = {"outcome": outcome, "semantic_valid": semantic_valid,
              "steps": counts, "operations": operations, "readiness_checks": checks,
              "max_queue": high_water, "observed_prefix": {k: v[:8] for k, v in observed.items()}}
    if outcome != "completed":
        result["last_events"] = list(trace)
        result["waiting"] = {n: [k for k in local[n] if k not in done[n]][:3] for n in nodes}
    assert high_water <= capacity
    if approach in ("projected", "ready") and case.valid:
        assert outcome == "completed", (case.name, approach, result)
        assert observed == case.expected
    elif not case.valid:
        assert outcome in ("deadlock", "rejected_cycle", "rejected_local_cycle")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=120)
    parser.add_argument("--permutations", type=int, default=4)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists")
    if not 8 <= args.steps <= 10000 or not 1 <= args.permutations <= 32:
        parser.error("steps must be 8..10000; permutations must be 1..32")
    results = []
    for case in cases(args.steps):
        for capacity in (1, 2, 8):
            for skew in (1, 3, 7):
                for permutation in range(args.permutations):
                    for approach in ("fixed", "projected", "ready"):
                        result = simulate(case, approach, args.steps, capacity, skew, permutation)
                        results.append({"case": case.name, "approach": approach,
                                        "capacity": capacity, "slow_period": skew,
                                        "permutation": permutation, **result})
    summary = {a: dict(Counter(r["outcome"] for r in results if r["approach"] == a))
               for a in ("fixed", "projected", "ready")}
    def git(*command):
        try:
            return subprocess.check_output(["git", *command], cwd=Path(__file__).resolve().parent,
                                           stderr=subprocess.DEVNULL, text=True).strip()
        except (FileNotFoundError, subprocess.CalledProcessError):
            return None
    status = git("status", "--porcelain")
    report = {"schema": "s7_cross_node_spike_v1", "engine": "deterministic_scalar_model",
              "source_revision": git("rev-parse", "HEAD"),
              "working_tree_dirty": None if status is None else bool(status),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "python": platform.python_version(), "platform": platform.platform(),
              "steps": args.steps, "summary": summary, "runs": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps({"runs": len(results), "summary": summary, "output": str(args.output)}, indent=2))


if __name__ == "__main__":
    main()
