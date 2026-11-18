#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Phase 4 matched scalar task dispatch; reuses the Phase 3 data lifecycle."""
import argparse
import asyncio
import hashlib
from pathlib import Path
import platform
import resource
import socket
import ssl
import time
import cross_node_s7_perf_node as base
from cross_node_s7_perf_node import F64, wire, transport
from cross_node_schedule_plan import validate_projection, distribution, machine
from cross_node_schedule_phase4 import actors_for, derive, LocalQueue

def compute(args, table, tx, rx, disconnect):
    setup_started = time.perf_counter_ns()
    plan = derive(actors_for(args.topology,args.work), 'egress' if args.approach == 'egress' else 'canonical')
    validate_projection(plan, plan['projected'])
    if plan['sha256'] != args.plan_sha256:
        raise RuntimeError('controller/worker plan mismatch')
    tasks = plan['tasks']
    order = plan['projected' if args.approach in ('canonical','egress') else 'local_order'][args.role]
    # Fixed baseline intentionally blocks on receive first, as in Phase 2.
    # It is a negative control, not a claim about this planner's local order.
    if args.approach == 'fixed':
        order = sorted(order, key=lambda k: tasks[k]['kind'] != 'receive')
    queue = LocalQueue(plan,args.role) if args.approach == 'queue' else None
    memory = {name: t['parameter'] for name, t in tasks.items() if t['kind'] == 'publish_memory'}
    setup_ns = time.perf_counter_ns() - setup_started
    index = 'ABC'.index(args.role)
    samples, measured = [], []
    max_log = max_buffer = probes = waits = 0
    categories = {name: 0 for name in ('send_wall_ns', 'receive_wall_ns', 'ready_wait_wall_ns',
                                      'injected_delay_wall_ns', 'selection_cpu_ns')}
    measured_begin = measured_end = None
    process_begin = thread_begin = usage_begin = None
    events = []
    error = None
    started = time.perf_counter_ns()
    try:
        for step in range(args.warmup + args.steps):
            measuring = step >= args.warmup
            if step == args.warmup:
                # No distributed barrier: each owner excludes its local warm-up.
                measured_begin = time.perf_counter_ns()
                process_begin, thread_begin = time.process_time_ns(), time.thread_time_ns()
                usage_begin = resource.getrusage(resource.RUSAGE_SELF)
                if queue:
                    queue.updates = queue.probes = queue.dispatches = 0
            begin = time.perf_counter_ns()
            if args.slow_ms:
                delay_start = time.perf_counter_ns()
                time.sleep(args.slow_ms / 1000)
                if measuring:
                    categories['injected_delay_wall_ns'] += time.perf_counter_ns() - delay_start
            values, done, value_in = {}, set(), None
            pending = list(order) if queue is None else []
            if queue: queue.begin()
            deadline = time.monotonic() + 4
            while len(done) < len(order):
                selection_cpu = time.thread_time_ns()
                selected = pending[0] if queue is None else None
                if args.approach in ('scan','queue'):
                    with table.changed:
                        def io_ready(key):
                            kind = tasks[key]['kind']
                            entry = table.entries[rx if kind == 'receive' else tx]
                            if entry.fault_code(): raise RuntimeError(entry.fault_code())
                            return entry.consumer.available() > 0 if kind == 'receive' else not entry.producer.log_full()
                        selected = queue.choose(io_ready) if queue else None
                        for key in ([] if queue else pending):
                            if measuring:
                                probes += 1
                            task = tasks[key]
                            if not all(parent in done for parent in task['deps']
                                       if tasks[parent]['node'] == args.role):
                                continue
                            kind = task['kind']
                            if kind in ('send', 'receive'):
                                entry = table.entries[rx if kind == 'receive' else tx]
                                if entry.fault_code():
                                    raise RuntimeError(entry.fault_code())
                                if kind == 'receive' and entry.consumer.available() == 0:
                                    continue
                                if kind == 'send' and entry.producer.log_full():
                                    continue
                            selected = key
                            break
                        if selected is None:
                            if measuring:
                                categories['selection_cpu_ns'] += time.thread_time_ns() - selection_cpu
                            remaining = deadline - time.monotonic()
                            if remaining <= 0:
                                raise TimeoutError('no ready operation')
                            wait_start = time.perf_counter_ns()
                            if queue and not queue.blocked: raise RuntimeError('local queue dependency stall')
                            table.changed.wait(min(remaining, .01))
                            if queue: queue.wake(io_ready)
                            if measuring:
                                waits += 1
                                categories['ready_wait_wall_ns'] += time.perf_counter_ns() - wait_start
                            continue
                if measuring:
                    categories['selection_cpu_ns'] += time.thread_time_ns() - selection_cpu
                task = tasks[selected]
                kind = task['kind']
                operation_start = time.perf_counter_ns()
                if kind == 'receive':
                    tokens, fault, _ = table.receive(rx, 1)
                    if fault or len(tokens) != 1:
                        raise RuntimeError(fault or 'short receive')
                    value_in = values[selected] = wire.values(F64, tokens)[0]
                elif kind == 'send':
                    fault = table.publish(tx, wire.from_values(F64, [values[task['inputs'][0]]]))
                    if fault:
                        raise RuntimeError(fault)
                elif kind == 'publish_memory':
                    values[selected] = memory[selected]
                elif kind == 'commit_memory':
                    memory[selected.removeprefix('commit:')] = values[task['inputs'][0]]
                elif kind == 'constant':
                    values[selected] = task['parameter']
                elif kind in ('identity', 'add_one'):
                    values[selected] = values[task['inputs'][0]] + (kind == 'add_one')
                elif kind != 'barrier':
                    raise RuntimeError('unsupported task ' + kind)
                if measuring and kind in ('send', 'receive'):
                    categories[kind + '_wall_ns'] += time.perf_counter_ns() - operation_start
                if len(events) < 16:
                    events.append([step, selected])
                if queue: queue.complete(selected)
                else: pending.remove(selected)
                done.add(selected)
            expected = ((5., 2., 3.)[index] if args.topology == 'independent'
                        else float(step if args.role == 'B' else step + 1))
            if value_in != expected:
                raise RuntimeError(f'reference mismatch at {step}: {value_in} != {expected}')
            samples.append(value_in)
            if measuring:
                measured.append(time.perf_counter_ns() - begin)
            # Same observation cadence/cost for both schedulers. These are
            # sampled bounds, not a complete event-level memory high-water mark.
            with table.changed:
                max_log = max(max_log, table.entries[tx].producer.stats()['send_log_depth'])
                max_buffer = max(max_buffer, table.entries[rx].consumer.available())
            if args.role == 'B' and args.disconnect_after == len(samples):
                disconnect()
    except Exception as failure:
        error = str(failure)
    measured_end = time.perf_counter_ns()
    process_end, thread_end = time.process_time_ns(), time.thread_time_ns()
    usage_end = resource.getrusage(resource.RUSAGE_SELF)
    elapsed_ns = measured_end - measured_begin if measured_begin is not None else None
    return {'engine': 's7py_websocket_mtls', 'execution': 'phase4_derived_scalar_fixture',
            'role': args.role, 'run_id': args.run_id, 'topology': args.topology,
            'approach': args.approach, 'work': args.work, 'window': args.window, 'packet_frames': args.packet_frames,
            'slow_ms': args.slow_ms, 'steps': len(samples), 'completed': error is None,
            'error': error, 'samples': samples, 'first_events': events, 'local_order': order,
            'plan_sha256': plan['sha256'], 'plan_setup_ns': setup_ns,
            'sampled_max_send_log': max_log, 'sampled_max_receive_buffer': max_buffer,
            'performance': {'warmup_steps': args.warmup, 'measured_steps': len(measured),
                'elapsed_ns': elapsed_ns,
                'steps_per_second': len(measured) * 1e9 / elapsed_ns if elapsed_ns and measured else 0,
                'step_wall_ns': distribution(measured), 'step_samples_ns': measured,
                'process_cpu_ns': process_end - process_begin if process_begin is not None else None,
                'owner_cpu_ns': thread_end - thread_begin if thread_begin is not None else None,
                'context_switches': {k: getattr(usage_end, k) - getattr(usage_begin, k)
                                     for k in ('ru_nvcsw', 'ru_nivcsw')} if usage_begin else None,
                'process_lifetime_peak_rss_kib': usage_end.ru_maxrss,
                'readiness_checks': probes, 'queue_io_probes': queue.probes if queue else 0,
                'dependency_updates': queue.updates if queue else 0, 'queue_dispatches': queue.dispatches if queue else 0, 'ready_waits': waits, **categories},
            'elapsed_seconds': (time.perf_counter_ns() - started) / 1e9,
            'machine': machine(), 'platform': platform.platform(), 'hostname': socket.gethostname(),
            'python': platform.python_version(), 'openssl': ssl.OPENSSL_VERSION,
            'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'support_sha256': {name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in ('cross_node_schedule_plan.py','cross_node_schedule_phase4.py','cross_node_s7_perf_node.py')},
            's7py_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(Path(transport.__file__).parent.glob('*.py'))}}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--role", choices=list("ABC"), required=True)
    p.add_argument("--listen", type=int, required=True)
    p.add_argument("--next", type=int, required=True)
    p.add_argument("--approach", choices=("fixed", "canonical", "egress", "scan", "queue"), required=True)
    p.add_argument("--topology", choices=("feedback", "independent"), required=True)
    p.add_argument("--window", type=int, choices=(1, 8, 64), required=True)
    p.add_argument("--packet-frames", type=int, choices=(1, 4), default=1)
    p.add_argument("--steps", type=int, default=128)
    p.add_argument("--warmup", type=int, default=16)
    p.add_argument("--batch-ms", type=int, default=5)
    p.add_argument("--plan-sha256", required=True)
    p.add_argument("--slow-ms", type=int, default=0)
    p.add_argument("--run-id", required=True)
    p.add_argument("--wait-start", action="store_true")
    p.add_argument("--disconnect-after", type=int, default=0)
    p.add_argument("--work", type=int, default=1)
    args = p.parse_args()
    if not 1 <= args.steps <= 10000 or not 0 <= args.slow_ms <= 100 or not 0 <= args.warmup <= 1000 or not 1 <= args.batch_ms <= 100:
        p.error("invalid steps/slow-ms")
    base.compute = compute
    return asyncio.run(base.run(args))


if __name__ == "__main__":
    raise SystemExit(main())
