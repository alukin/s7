#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import random
import unittest
from cross_node_schedule_phase4 import actors_for, derive, LocalQueue
from cross_node_schedule_plan import validate_projection, InvalidGraph


class Policies(unittest.TestCase):
    def test_egress_and_canonical(self):
        for topology in ('feedback','independent'):
            for work in (1,16,64):
                actors = actors_for(topology,work)
                expected = derive(actors)
                for seed in range(16):
                    random.Random(seed).shuffle(actors)
                    self.assertEqual(derive(actors),expected)
                validate_projection(expected,expected['projected'])
                for node,order in expected['projected'].items():
                    if topology == 'independent':
                        kinds = [expected['tasks'][k]['kind'] for k in order]
                        self.assertLess(kinds.index('send'),kinds.index('receive'))

    def test_blocked_receive_does_not_hold_independent_send(self):
        plan = derive(actors_for('independent',64))
        for node in 'ABC':
            queue = LocalQueue(plan,node)
            queue.begin()
            sent = False
            while (key:=queue.choose(lambda k:plan['tasks'][k]['kind']!='receive')) is not None:
                sent |= plan['tasks'][key]['kind']=='send'
                queue.complete(key)
            self.assertTrue(sent)
            self.assertEqual(len(queue.blocked),1)
            queue.wake(lambda k:True)
            while (key:=queue.choose(lambda k:True)) is not None:
                queue.complete(key)
            self.assertEqual(set(queue.order),queue.finished)
            self.assertEqual(queue.updates,sum(map(len,queue.parents.values())))

    def test_queue_count_and_commit(self):
        plan = derive(actors_for('feedback',64))
        for node in 'ABC':
            q = LocalQueue(plan,node)
            for _ in range(3):
                q.begin()
                visited=[]
                while (key:=q.choose(lambda k:True)) is not None:
                    visited.append(key); q.complete(key)
                self.assertEqual(set(visited),set(q.order))
                if node=='A': self.assertEqual(visited[-1],'commit:state')

    def test_reject_cycles(self):
        from cross_node_schedule_plan import Actor
        with self.assertRaises(InvalidGraph):
            derive([Actor('a','A','identity',('b',)),Actor('b','B','identity',('a',))])


if __name__=='__main__': unittest.main()
