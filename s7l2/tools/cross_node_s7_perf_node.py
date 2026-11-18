#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Performance fixture using derived schedules and unchanged S7 WebSocket/mTLS.

Same Python engine for every approach; this is not a deployed Python graph.
Scientific basis and scope: docs/CROSS_NODE_CAUSALITY_RESEARCH.md, L1/L3/L5/L7/T2.
The controller stages s7node sources and per-node credentials beside this file.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import platform
import resource
import socket
import ssl
import sys
import time

from s7node import stream, tls, transport, websocket, wire
from cross_node_schedule_plan import derive, fixture, validate_projection, distribution, machine

NODES = ("aaaaaaaa-0000-4000-8000-000000000001", "bbbbbbbb-0000-4000-8000-000000000002",
         "cccccccc-0000-4000-8000-000000000003")
STREAMS = tuple(wire.parse_stream_id(f"00000000-0000-4000-8000-00000000000{n}") for n in (1, 2, 3))
F64 = wire.wire_type("F64")


def config(index, args):
    return stream.StreamConfig(STREAMS[index], F64, max_elements_per_packet=args.packet_frames,
        max_batch_latency_ms=args.batch_ms, send_log_max_packets=args.window, max_block_ms=4000, max_wait_ms=4000,
        producer_node=NODES[index], consumer_nodes=[NODES[(index + 1) % 3]], sample_rate=(1000, 1))


def compute(args, table, tx, rx, disconnect):
    setup_started = time.perf_counter_ns()
    plan = derive(fixture(args.topology))
    validate_projection(plan, plan['projected'])
    if plan['sha256'] != args.plan_sha256:
        raise RuntimeError('controller/worker plan mismatch')
    tasks = plan['tasks']
    order = plan['projected' if args.approach == 'projected' else 'local_order'][args.role]
    # Fixed baseline intentionally blocks on receive first, as in Phase 2.
    # It is a negative control, not a claim about this planner's local order.
    if args.approach == 'fixed':
        order = sorted(order, key=lambda k: tasks[k]['kind'] != 'receive')
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
            begin = time.perf_counter_ns()
            if args.slow_ms:
                delay_start = time.perf_counter_ns()
                time.sleep(args.slow_ms / 1000)
                if measuring:
                    categories['injected_delay_wall_ns'] += time.perf_counter_ns() - delay_start
            values, done, value_in = {}, set(), None
            pending = list(order)
            deadline = time.monotonic() + 4
            while pending:
                selection_cpu = time.thread_time_ns()
                selected = pending[0]
                if args.approach == 'ready':
                    with table.changed:
                        selected = None
                        for key in pending:
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
                            table.changed.wait(min(remaining, .01))
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
                pending.remove(selected)
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
    return {'engine': 's7py_websocket_mtls', 'execution': 'derived_scalar_fixture',
            'role': args.role, 'run_id': args.run_id, 'topology': args.topology,
            'approach': args.approach, 'window': args.window, 'packet_frames': args.packet_frames,
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
                'readiness_checks': probes, 'ready_waits': waits, **categories},
            'elapsed_seconds': (time.perf_counter_ns() - started) / 1e9,
            'machine': machine(), 'platform': platform.platform(), 'hostname': socket.gethostname(),
            'python': platform.python_version(), 'openssl': ssl.OPENSSL_VERSION,
            'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'planner_sha256': hashlib.sha256(Path(__file__).with_name('cross_node_schedule_plan.py').read_bytes()).hexdigest(),
            's7py_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(Path(transport.__file__).parent.glob('*.py'))}}

async def run(args):
    index = "ABC".index(args.role)
    credentials = Path(__file__).parent / "credentials"
    cert, key, ca = (str(credentials / name) for name in ("node.cert.pem", "node.key.pem", "ca.cert.pem"))
    context = tls.server_context(cert, key, ca)
    context.verify_mode = ssl.CERT_REQUIRED
    table = transport.StreamTable(stream.MemorySequenceStore())
    table.set_clock_usable(False)  # no cross-host UTC/latency claim
    endpoint = transport.DataEndpoint(table, NODES[index], tls_enabled=True,
        client_ssl=lambda: tls.client_context(ca, cert, key), log=lambda message: None)
    ticks = endpoint.start()
    active = set()
    disconnected = []

    def disconnect():
        def close_data():
            for connection in list(endpoint.connections):
                if connection.outbound and not connection.closed:
                    disconnected.append(connection)
                    endpoint._close(connection)
        endpoint.loop.call_soon_threadsafe(close_data)

    async def serve(reader, writer):
        active.add(asyncio.current_task())
        try:
            ws = await websocket.accept(reader, writer, paths=("/data",),
                max_message_bytes=transport.MAX_DATA_MESSAGE_BYTES,
                response_headers={transport.NODE_HEADER: NODES[index]})
            if ws is not None:
                await endpoint.serve_inbound(ws, ws.headers.get(transport.NODE_HEADER.lower(), ""))
        except (OSError, websocket.WebSocketError) as failure:
            endpoint.record_error(str(failure))
        finally:
            writer.close()
            active.discard(asyncio.current_task())

    server = await asyncio.start_server(serve, "127.0.0.1", args.listen, ssl=context)
    tx = table.open(config(index, args), transport.PRODUCER)
    rx = table.open(config((index - 1) % 3, args), transport.CONSUMER)
    print(json.dumps({"ready": args.role}), flush=True)
    try:
        if args.wait_start and (await asyncio.to_thread(sys.stdin.readline)).strip() != "start":
            raise RuntimeError("missing start")
        endpoint.set_data_peers([transport.DataPeer(f"wss://127.0.0.1:{args.next}", NODES[(index - 1) % 3])])
        result = await asyncio.to_thread(compute, args, table, tx, rx, disconnect)
        # Keep all data endpoints alive until the controller has every report.
        # Drain only consumed ACKs, never fabricate receipt credits.
        drain_started = time.perf_counter_ns()
        if result["completed"]:
            until = time.monotonic() + 5
            while True:
                with table.changed:
                    drained = (not table.entries[tx].producer.pending
                               and table.entries[tx].producer.stats()["send_log_depth"] == 0
                               and table.entries[rx].consumer.consumed_since_ack == 0)
                if drained:
                    break
                if time.monotonic() > until:
                    result["completed"], result["error"] = False, "consumption ACK drain timeout"
                    break
                await asyncio.sleep(0.002)
        result["ack_drain_ns"] = time.perf_counter_ns() - drain_started
        result["stream_statuses"] = table.statuses()
        result["injected_disconnects"] = len(disconnected)
        result["outbound_reconnected"] = bool(disconnected) and any(
            c.outbound and not c.closed and c not in disconnected for c in endpoint.connections)
        result["transport_errors"] = list(endpoint.errors)
        result["messages_sent"], result["messages_received"] = endpoint.messages_sent, endpoint.messages_received
        result["certificate_sha256"] = hashlib.sha256(ssl.PEM_cert_to_DER_cert(Path(cert).read_text())).hexdigest()
        print(json.dumps(result, allow_nan=False), flush=True)
        if args.wait_start:
            await asyncio.to_thread(sys.stdin.readline)
        return 0 if result["completed"] else 2
    finally:
        endpoint.links.clear()
        for connection in list(endpoint.connections):
            endpoint._close(connection)
        server.close()
        await server.wait_closed()
        ticks.cancel()
        await asyncio.gather(ticks, return_exceptions=True)
        for task in list(active):
            task.cancel()
        await asyncio.gather(*active, return_exceptions=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--role", choices=list("ABC"), required=True)
    p.add_argument("--listen", type=int, required=True)
    p.add_argument("--next", type=int, required=True)
    p.add_argument("--approach", choices=("fixed", "projected", "ready"), required=True)
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
    args = p.parse_args()
    if not 1 <= args.steps <= 10000 or not 0 <= args.slow_ms <= 100 or not 0 <= args.warmup <= 1000 or not 1 <= args.batch_ms <= 100:
        p.error("invalid steps/slow-ms")
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
