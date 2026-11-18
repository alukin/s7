#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Scheduling fixture using the unchanged s7py WebSocket/mTLS data endpoint.

This owns a hand-scheduled scalar computation, not a deployed Python graph.
Scientific basis and scope: docs/CROSS_NODE_CAUSALITY_RESEARCH.md, L1/L3/L5/L7/T2.
The controller stages s7node sources and per-node credentials beside this file.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import platform
import socket
import ssl
import sys
import time

from s7node import stream, tls, transport, websocket, wire

NODES = ("aaaaaaaa-0000-4000-8000-000000000001", "bbbbbbbb-0000-4000-8000-000000000002",
         "cccccccc-0000-4000-8000-000000000003")
STREAMS = tuple(wire.parse_stream_id(f"00000000-0000-4000-8000-00000000000{n}") for n in (1, 2, 3))
F64 = wire.wire_type("F64")


def config(index, args):
    return stream.StreamConfig(STREAMS[index], F64, max_elements_per_packet=args.packet_frames,
        max_batch_latency_ms=5, send_log_max_packets=args.window, max_block_ms=4000, max_wait_ms=4000,
        producer_node=NODES[index], consumer_nodes=[NODES[(index + 1) % 3]], sample_rate=(1000, 1))


def compute(args, table, tx, rx, disconnect):
    index = "ABC".index(args.role)
    samples, events = [], []
    state = 0.0
    error = None
    probes = max_log = max_buffer = 0
    started = time.monotonic()
    try:
        for step in range(args.steps):
            if args.slow_ms:
                time.sleep(args.slow_ms / 1000)
            value_in = None
            pending = ["receive", "send"]
            if args.approach == "projected" and (args.topology == "independent" or args.role == "A"):
                pending.reverse()
            deadline = time.monotonic() + 4
            while pending:
                selected = pending[0]
                if args.approach == "ready":
                    with table.changed:
                        selected = None
                        for task in pending:
                            probes += 1
                            entry = table.entries[rx if task == "receive" else tx]
                            if entry.fault_code():
                                raise RuntimeError(entry.fault_code())
                            has_value = args.topology == "independent" or args.role == "A" or value_in is not None
                            enabled = (entry.consumer.available() > 0 if task == "receive"
                                       else has_value and not entry.producer.log_full())
                            if enabled:
                                selected = task
                                break
                        if selected is None:
                            remaining = deadline - time.monotonic()
                            if remaining <= 0:
                                raise TimeoutError("no ready operation")
                            table.changed.wait(min(remaining, 0.01))
                            continue
                if selected == "receive":
                    tokens, fault, _ = table.receive(rx, 1)
                    if fault or len(tokens) != 1:
                        raise RuntimeError(fault or "short receive")
                    value_in = wire.values(F64, tokens)[0]
                else:
                    if args.topology == "independent":
                        value = (2.0, 3.0, 5.0)[index]
                    else:
                        value = state if args.role == "A" else value_in + (1 if args.role == "B" else 0)
                    fault = table.publish(tx, wire.from_values(F64, [value]))
                    if fault:
                        raise RuntimeError(fault)
                events.append([step, selected])
                pending.remove(selected)
                with table.changed:
                    max_log = max(max_log, table.entries[tx].producer.stats()["send_log_depth"])
                    max_buffer = max(max_buffer, table.entries[rx].consumer.available())
            expected = ((5.0, 2.0, 3.0)[index] if args.topology == "independent"
                        else float(step if args.role == "B" else step + 1))
            if value_in != expected:
                raise RuntimeError(f"reference mismatch at {step}: {value_in} != {expected}")
            samples.append(value_in)
            if args.role == "B" and args.disconnect_after == len(samples):
                disconnect()
            if args.role == "A" and args.topology == "feedback":
                state = value_in
    except Exception as failure:
        error = str(failure)
    return {"engine": "s7py_websocket_mtls", "execution": "hand_scheduled_scalar_fixture",
            "role": args.role, "run_id": args.run_id, "topology": args.topology,
            "approach": args.approach, "window": args.window, "packet_frames": args.packet_frames,
            "slow_ms": args.slow_ms, "steps": len(samples), "completed": error is None,
            "error": error, "samples": samples, "first_events": events[:8],
            "sampled_max_send_log": max_log, "sampled_max_receive_buffer": max_buffer,
            "readiness_checks": probes, "elapsed_seconds": time.monotonic() - started,
            "platform": platform.platform(), "hostname": socket.gethostname(),
            "python": platform.python_version(), "openssl": ssl.OPENSSL_VERSION,
            "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "s7py_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sorted(Path(transport.__file__).parent.glob("*.py"))}}


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
    p.add_argument("--window", type=int, choices=(1, 8), required=True)
    p.add_argument("--packet-frames", type=int, choices=(1, 4), default=1)
    p.add_argument("--steps", type=int, default=16)
    p.add_argument("--slow-ms", type=int, default=0)
    p.add_argument("--run-id", required=True)
    p.add_argument("--wait-start", action="store_true")
    p.add_argument("--disconnect-after", type=int, default=0)
    args = p.parse_args()
    if not 1 <= args.steps <= 10000 or not 0 <= args.slow_ms <= 100:
        p.error("invalid steps/slow-ms")
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
