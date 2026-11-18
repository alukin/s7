#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in research planner invariants, including unsafe projection controls."""
from dataclasses import replace
import random
import unittest

from cross_node_schedule_plan import Actor, InvalidGraph, derive, fixture, validate_projection


class PlannerTests(unittest.TestCase):
    def test_canonical_and_projection(self):
        for topology in ('feedback', 'independent'):
            actors = fixture(topology)
            reference = derive(actors)
            for seed in range(32):
                random.Random(seed).shuffle(actors)
                plan = derive(actors)
                self.assertEqual(reference, plan)
                validate_projection(plan, plan['projected'])
            # Reversal violates local dependencies, and must not be accepted.
            with self.assertRaises(InvalidGraph):
                validate_projection(reference, {n: list(reversed(v)) for n, v in reference['projected'].items()})

    def test_memory_and_direct_state(self):
        actors = [Actor('a', 'A', 'delay', ('b',)), Actor('b', 'B', 'identity', ('a',))]
        plan = derive(actors)
        self.assertEqual(plan['tasks']['a']['deps'], [])
        self.assertIn('barrier:A', plan['tasks']['commit:a']['deps'])
        with self.assertRaises(InvalidGraph):
            derive([replace(a, kind='accumulate') if a.kind == 'delay' else a for a in actors])
        # An unrelated memory actor cannot validate a direct cycle.
        with self.assertRaises(InvalidGraph):
            derive([Actor('a', 'A', 'identity', ('b',)), Actor('b', 'B', 'identity', ('a',)),
                    Actor('unused_memory', 'C', 'delay', ('b',))])

    def test_partition_wait_cycle(self):
        plan = derive(fixture('independent'))
        unsafe = {}
        for node, order in plan['projected'].items():
            unsafe[node] = sorted(order, key=lambda k: plan['tasks'][k]['kind'] != 'receive')
        with self.assertRaises(InvalidGraph):
            validate_projection(plan, unsafe)

    def test_schema_errors_and_membership(self):
        for actors in ([], [Actor('x', 'A', 'identity', ('missing',))],
                       [Actor('x', 'A', 'constant'), Actor('x', 'B', 'constant')],
                       [Actor('x', '', 'constant')], [Actor('x', 'A', 'unknown')],
                       [Actor('x', 'A', 'constant', parameter=float('nan'))]):
            with self.assertRaises(InvalidGraph):
                derive(actors)
        plan = derive(fixture('feedback'))
        for mutate in ('missing_task', 'duplicate', 'missing_node'):
            proposal = {n: list(v) for n, v in plan['projected'].items()}
            if mutate == 'missing_task':
                proposal['A'].pop()
            elif mutate == 'duplicate':
                proposal['A'].append(proposal['A'][0])
            else:
                del proposal['C']
            with self.assertRaises(InvalidGraph):
                validate_projection(plan, proposal)

    def test_fanout_and_multiple_memories(self):
        actors = [Actor('state1', 'A', 'delay', ('c',)), Actor('state2', 'A', 'delay', ('b',)),
                  Actor('b', 'B', 'identity', ('state1',)), Actor('c', 'C', 'identity', ('state1',))]
        plan = derive(actors)
        validate_projection(plan, plan['projected'])
        self.assertIn('receive:state1@B', plan['tasks'])
        self.assertIn('receive:state1@C', plan['tasks'])
        self.assertNotIn('commit:state1', plan['tasks']['barrier:A']['deps'])


if __name__ == '__main__':
    unittest.main()
