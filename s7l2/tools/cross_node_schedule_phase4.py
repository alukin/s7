#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Research policies: egress-priority precedence and incremental local queues.

The Phase 3 planner is retained unchanged. No production scheduling contract.
"""
from collections import deque
from dataclasses import asdict, replace
import heapq

from cross_node_schedule_plan import Actor, InvalidGraph, derive as canonical, digest, fixture, validate_projection


def actors_for(topology, work=1):
    actors = fixture(topology)
    if not 1 <= work <= 1024:
        raise InvalidGraph('invalid work count')
    result = []
    for a in actors:
        if a.name not in 'abc' or work == 1:
            result.append(a)
            continue
        previous = a.name + '_0000'
        result.append(replace(a, name=previous))
        for i in range(1, work):
            name = a.name if i == work-1 else f'{a.name}_{i:04}'
            result.append(Actor(name, a.node, 'identity', (previous,)))
            previous = name
    return result


def derive(actors, policy='egress'):
    actors = list(actors)
    plan = canonical(actors)
    if policy == 'egress':
        tasks = plan['tasks']
        rank = {'send':0, 'receive':2, 'barrier':3, 'commit_memory':4}
        followers = {k:[] for k in tasks}
        degrees = {k:len(t['deps']) for k,t in tasks.items()}
        for k,t in tasks.items():
            for p in t['deps']:
                followers[p].append(k)
        def priority(k):
            return rank.get(tasks[k]['kind'],1), k
        ready = [priority(k) for k in tasks if degrees[k] == 0]
        heapq.heapify(ready)
        order = []
        while ready:
            _,k = heapq.heappop(ready)
            order.append(k)
            for child in followers[k]:
                degrees[child] -= 1
                if degrees[child] == 0:
                    heapq.heappush(ready,priority(child))
        if len(order) != len(tasks):
            raise InvalidGraph('cycle in egress plan')
        plan['global_order'] = order
        plan['projected'] = {n:[k for k in order if tasks[k]['node']==n] for n in plan['projected']}
    elif policy != 'canonical':
        raise InvalidGraph('unknown plan policy')
    validate_projection(plan,plan['projected'])
    plan.pop('sha256')
    plan['actors'] = [asdict(a) for a in sorted(actors,key=lambda a:a.name)]
    plan['policy'] = policy
    plan['sha256'] = digest(plan)
    return plan


class LocalQueue:
    """Dependencies decrement once; blocked I/O is retried only on wakeup.

    Caller owns all execution and readiness checks. This class owns no thread,
    element state or transport; queues contain at most the local task count.
    """
    def __init__(self, plan, node):
        self.tasks = plan['tasks']
        self.order = plan['local_order'][node]
        self.parents = {k:[p for p in self.tasks[k]['deps'] if self.tasks[p]['node']==node] for k in self.order}
        self.followers = {k:[] for k in self.order}
        for k in self.order:
            for p in self.parents[k]:
                self.followers[p].append(k)
        self.updates = self.probes = self.dispatches = 0

    def begin(self):
        self.degree = {k:len(v) for k,v in self.parents.items()}
        self.ready = deque(k for k in self.order if self.degree[k] == 0)
        self.blocked = []
        self.finished = set()

    def choose(self, io_ready):
        while self.ready:
            k = self.ready.popleft()
            if self.tasks[k]['kind'] in ('send','receive'):
                self.probes += 1
                if not io_ready(k):
                    self.blocked.append(k)
                    continue
            return k
        return None

    def wake(self, io_ready):
        blocked, self.blocked = self.blocked, []
        for k in blocked:
            self.probes += 1
            (self.ready if io_ready(k) else self.blocked).append(k)

    def complete(self, key):
        if key in self.finished:
            raise AssertionError('duplicate dispatch')
        self.finished.add(key)
        self.dispatches += 1
        for child in self.followers[key]:
            self.degree[child] -= 1
            self.updates += 1
            if self.degree[child] == 0:
                self.ready.append(child)
